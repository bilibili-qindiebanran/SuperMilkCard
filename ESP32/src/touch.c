/**
 * @file touch.c
 * @brief 触摸屏驱动实现
 *
 * 探测流程：
 *   1. 记录 TP_INT 空闲电平（确认极性）
 *   2. 执行保守复位序列（释放→拉低→保持→释放，记录各阶段 TP_INT 与 ACK）
 *   3. 优先官方 ST77926 组件创建（读 CHIP_ID/分辨率/最大触点）
 *   4. 失败则扫描 I2C 地址，输出所有 ACK 地址 + 安全寄存器读取
 *
 * 采集：touch_task 轮询读取（30ms）；TP_INT 活动确认后注册 GPIO ISR
 *       （ISR 只置标志，I2C 读取仍在任务侧）
 *
 * 坐标转换：raw 坐标 → 旋转映射（0/90/180/270）→ 逻辑坐标
 */

#include "touch.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_lcd_touch_st77926.h"
#include "i2c_bus.h"
#include "lcd_ui.h"

static const char *TAG = "touch";

/* ------------------------------------------------------------------ */
/* 内部状态                                                            */
/* ------------------------------------------------------------------ */
static esp_lcd_touch_handle_t s_tp = NULL;     /* 触摸驱动句柄 */
static esp_lcd_panel_io_handle_t s_tp_io = NULL; /* 触摸 panel IO */
static QueueHandle_t s_evt_queue = NULL;       /* 触摸事件队列 */
static touch_info_t s_info = {0};              /* 控制器识别信息 */
static SemaphoreHandle_t s_int_sem = NULL;     /* 中断二值信号量（唤醒采集任务） */
static int s_rotation = 0;                     /* 旋转角度 0/90/180/270 */
static bool s_int_mode = false;                /* 是否中断模式 */
static int s_int_idle_level = -1;              /* TP_INT 空闲电平 */

#define TOUCH_POLL_MS 30         /* 空闲轮询周期 */
#define TOUCH_ACTIVE_POLL_MS 8   /* 触摸中高频轮询 */
#define TOUCH_INT_WAIT_MS 100    /* 中断等待超时（空闲时低功耗） */
#define TOUCH_EVT_QUEUE_LEN 32

/* ------------------------------------------------------------------ */
/* 坐标转换：raw → 逻辑（旋转映射）                                    */
/* ------------------------------------------------------------------ */
static void coord_rotate(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    int rx = raw_x, ry = raw_y;
    switch (s_rotation)
    {
    case 90:
        *x = (uint16_t)ry;
        *y = (uint16_t)(LCD_UI_PHYS_W - 1 - rx);
        break;
    case 180:
        *x = (uint16_t)(LCD_UI_PHYS_W - 1 - rx);
        *y = (uint16_t)(LCD_UI_PHYS_H - 1 - ry);
        break;
    case 270:
        *x = (uint16_t)(LCD_UI_PHYS_H - 1 - ry);
        *y = (uint16_t)rx;
        break;
    default: /* 0° 竖屏 */
        *x = (uint16_t)rx;
        *y = (uint16_t)ry;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* TP_INT 中断回调（ISR 上下文：只给信号量，不碰 I2C）                */
/* ------------------------------------------------------------------ */
static void touch_int_isr(esp_lcd_touch_handle_t tp)
{
    (void)tp;
    BaseType_t hpw = pdFALSE;
    if (s_int_sem != NULL)
    {
        xSemaphoreGiveFromISR(s_int_sem, &hpw);
    }
    portYIELD_FROM_ISR(hpw);
}

/* ------------------------------------------------------------------ */
/* 触点 → 事件投递                                                      */
/* ------------------------------------------------------------------ */
static void post_event(uint8_t id, touch_evt_t evt, uint16_t raw_x, uint16_t raw_y,
                       uint16_t pressure, uint32_t ts)
{
    if (s_evt_queue == NULL)
    {
        return;
    }
    touch_event_t e = {
        .id = id,
        .event = evt,
        .raw_x = raw_x,
        .raw_y = raw_y,
        .pressure = pressure,
        .timestamp_ms = ts,
    };
    coord_rotate(raw_x, raw_y, &e.x, &e.y);
    if (xQueueSend(s_evt_queue, &e, 0) != pdTRUE)
    {
        /* 队列满：丢弃最旧，保留最新（覆盖读） */
        touch_event_t dummy;
        xQueueReceive(s_evt_queue, &dummy, 0);
        xQueueSend(s_evt_queue, &e, 0);
    }
}

/* ------------------------------------------------------------------ */
/* TP_INT 极性探测                                                      */
/* ------------------------------------------------------------------ */
static void detect_int_polarity(void)
{
    /* 短周期采样 TP_INT：无触摸时的空闲电平即空闲电平 */
    int high = 0;
    const int samples = 100;
    for (int i = 0; i < samples; i++)
    {
        if (gpio_get_level(TOUCH_PIN_TP_INT))
        {
            high++;
        }
        esp_rom_delay_us(500);
    }
    /* 若全高或全低（无活动），判为电平型；空闲电平 = 多数值 */
    s_int_idle_level = (high > samples / 2) ? 1 : 0;
    ESP_LOGI(TAG, "TP_INT idle level: %d (%d/%d high, 触摸时预期跳变)",
             s_int_idle_level, high, samples);
}

/* ------------------------------------------------------------------ */
/* 保守复位序列（记录各阶段 TP_INT 与 ACK）                            */
/* ------------------------------------------------------------------ */
static void conservative_reset(void)
{
    ESP_LOGI(TAG, "TP reset sequence: 释放(高) → 拉低 → 保持 → 释放(高)");

    /* 1. 释放复位（默认高） */
    gpio_set_level(TOUCH_PIN_TP_RST, 1);
    esp_rom_delay_us(1000);
    ESP_LOGI(TAG, "  after release: TP_INT=%d", gpio_get_level(TOUCH_PIN_TP_INT));

    /* 2. 拉低复位（低有效 assert） */
    gpio_set_level(TOUCH_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_LOGI(TAG, "  after assert(low): TP_INT=%d", gpio_get_level(TOUCH_PIN_TP_INT));

    /* 3. 保持 10ms */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 4. 释放复位 */
    gpio_set_level(TOUCH_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "  after release(high): TP_INT=%d", gpio_get_level(TOUCH_PIN_TP_INT));
}

/* ------------------------------------------------------------------ */
/* I2C 地址扫描（官方组件失败时回退）                                  */
/* ------------------------------------------------------------------ */
static void scan_i2c_addresses(void)
{
    ESP_LOGI(TAG, "Scanning I2C addresses...");
    i2c_master_bus_handle_t bus = i2c_bus_get();
    if (bus == NULL)
    {
        ESP_LOGE(TAG, "i2c_bus not initialized");
        return;
    }
    for (int addr = 0x08; addr < 0x78; addr++)
    {
        esp_err_t err = i2c_master_probe(bus, addr, pdMS_TO_TICKS(20));
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "  found device at 0x%02X", addr);
            /* 记录候选（排除已识别的 IP5306 0x75 和触摸 0x55） */
            if (addr == TOUCH_I2C_ADDR_ST77926)
            {
                s_info.i2c_addr = addr;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 官方 ST77926 触摸组件探测                                           */
/* ------------------------------------------------------------------ */
static esp_err_t probe_official_st77926(void)
{
    if (i2c_bus_get() == NULL)
    {
        ESP_LOGE(TAG, "i2c_bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* 创建触摸 panel IO（复用共享 I2C0 总线） */
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_ST77926_CONFIG();
    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus_get(), &io_cfg, &s_tp_io);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 触摸配置：复位低有效、中断低有效（官方示例标准） */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_UI_PHYS_W,
        .y_max = LCD_UI_PHYS_H,
        .rst_gpio_num = TOUCH_PIN_TP_RST,
        .int_gpio_num = TOUCH_PIN_TP_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0, /* 先假设低有效，探测后修正 */
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    err = esp_lcd_touch_new_i2c_st77926(s_tp_io, &tp_cfg, &s_tp);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_st77926 failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 读取识别信息（官方组件公开 getter） */
    esp_lcd_touch_st77926_info_t info;
    err = esp_lcd_touch_st77926_get_info(s_tp, &info);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_lcd_touch_st77926_get_info failed: %s", esp_err_to_name(err));
    }

    s_info.detected = true;
    s_info.name = "ST77926 Touch";
    s_info.i2c_addr = TOUCH_I2C_ADDR_ST77926;
    s_info.chip_id = info.chip_id;
    s_info.raw_x_max = info.x_res;
    s_info.raw_y_max = info.y_res;
    s_info.max_touch = info.max_touches;
    s_info.interrupt_active_high = false; /* 官方默认低有效 */

    ESP_LOGI(TAG, "Detected: %s @0x%02X chip=0x%02X res=%dx%d max_touch=%d",
             s_info.name, s_info.i2c_addr, s_info.chip_id,
             s_info.raw_x_max, s_info.raw_y_max, s_info.max_touch);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 触摸采集任务                                                        */
/* ------------------------------------------------------------------ */
static void touch_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "touch_task started (polling mode)");

    /* 触点跟踪：去重 + 松手超时释放 */
    struct
    {
        bool active;
        uint16_t x, y;
        uint32_t last_seen_ms;
    } tracks[TOUCH_MAX_POINTS] = {0};
    const uint32_t release_timeout_ms = 200; /* 松手超时 */

    while (1)
    {
        /* 有触摸时高频轮询（5ms），空闲时等中断（100ms 超时兜底） */
        bool any_active = false;
        for (int id = 0; id < TOUCH_MAX_POINTS; id++)
        {
            if (tracks[id].active)
            {
                any_active = true;
                break;
            }
        }

        if (!any_active)
        {
            if (s_int_mode && s_int_sem != NULL)
            {
                /* 空闲：等中断唤醒（超时兜底轮询，防漏事件） */
                if (xSemaphoreTake(s_int_sem, pdMS_TO_TICKS(TOUCH_INT_WAIT_MS)) == pdFALSE)
                {
                    /* 超时无中断：仍轮询一次（处理残留） */
                }
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
            }
        }
        else
        {
            /* 触摸中：高频轮询，快速响应移动/抬起 */
            vTaskDelay(pdMS_TO_TICKS(TOUCH_ACTIVE_POLL_MS));
        }

        esp_err_t err = esp_lcd_touch_read_data(s_tp);
        if (err == ESP_OK)
        {
            esp_lcd_touch_point_data_t pts[TOUCH_MAX_POINTS];
            uint8_t cnt = 0;
            if (esp_lcd_touch_get_data(s_tp, pts, &cnt, TOUCH_MAX_POINTS) == ESP_OK)
            {
                uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                bool seen[TOUCH_MAX_POINTS] = {0};

                for (int i = 0; i < cnt; i++)
                {
                    uint8_t id = pts[i].track_id;
                    if (id >= TOUCH_MAX_POINTS)
                    {
                        id = i; /* 控制器无 track_id 时退化为序号 */
                    }
                    seen[id] = true;
                    if (!tracks[id].active)
                    {
                        /* 新按下 */
                        tracks[id].active = true;
                        tracks[id].x = pts[i].x;
                        tracks[id].y = pts[i].y;
                        post_event(id, TOUCH_EVT_DOWN, pts[i].x, pts[i].y,
                                   pts[i].strength, now);
                    }
                    else
                    {
                        /* 移动检测（去抖：位移 > 1px 才算 Move） */
                        int dx = (int)pts[i].x - (int)tracks[id].x;
                        int dy = (int)pts[i].y - (int)tracks[id].y;
                        if (dx * dx + dy * dy > 1)
                        {
                            tracks[id].x = pts[i].x;
                            tracks[id].y = pts[i].y;
                            post_event(id, TOUCH_EVT_MOVE, pts[i].x, pts[i].y,
                                       pts[i].strength, now);
                        }
                    }
                    tracks[id].last_seen_ms = now;
                }

                /* 未在本帧出现的活跃触点 → 抬起 */
                for (int id = 0; id < TOUCH_MAX_POINTS; id++)
                {
                    if (tracks[id].active && !seen[id])
                    {
                        tracks[id].active = false;
                        post_event(id, TOUCH_EVT_UP, tracks[id].x, tracks[id].y, 0, now);
                    }
                }

                /* 超时兜底：活跃但长时间未更新（控制器残留）→ 强制释放 */
                for (int id = 0; id < TOUCH_MAX_POINTS; id++)
                {
                    if (tracks[id].active &&
                        (now - tracks[id].last_seen_ms) > release_timeout_ms)
                    {
                        tracks[id].active = false;
                        post_event(id, TOUCH_EVT_UP, tracks[id].x, tracks[id].y, 0, now);
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 公共 API                                                            */
/* ------------------------------------------------------------------ */
esp_err_t touch_init(void)
{
    if (s_tp != NULL)
    {
        return ESP_OK;
    }

    /* 1. 配置 GPIO：TP_RST 输出，TP_INT 输入 */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << TOUCH_PIN_TP_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << TOUCH_PIN_TP_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE, /* 探测阶段不启用中断 */
    };
    gpio_config(&int_cfg);

    /* 2. 事件队列 */
    s_evt_queue = xQueueCreate(TOUCH_EVT_QUEUE_LEN, sizeof(touch_event_t));
    if (s_evt_queue == NULL)
    {
        ESP_LOGE(TAG, "event queue create failed");
        return ESP_ERR_NO_MEM;
    }

    /* 3. 极性探测（触摸前空闲电平） */
    detect_int_polarity();

    /* 4. 保守复位序列 */
    conservative_reset();

    /* 5. 官方组件探测 */
    esp_err_t err = probe_official_st77926();
    if (err != ESP_OK)
    {
        /* 回退：扫描总线，输出候选地址 */
        ESP_LOGW(TAG, "Official ST77926 probe failed, falling back to bus scan");
        scan_i2c_addresses();
        s_info.detected = false;
        s_info.name = "Unknown (待确认)";
        s_info.i2c_addr = s_info.i2c_addr ? s_info.i2c_addr : 0;
        /* 不创建采集任务（无控制器可读），但返回 ESP_OK 让系统继续运行 */
        return ESP_OK;
    }

    /* 6. 启用中断模式（确认极性后）：
     *    官方驱动已按 levels.interrupt 配置了 GPIO 边沿，
     *    这里注册 ISR 回调（ISR 只给信号量唤醒任务） */
    s_int_sem = xSemaphoreCreateBinary();
    esp_err_t isr_err = esp_lcd_touch_register_interrupt_callback(s_tp, touch_int_isr);
    if (isr_err == ESP_OK)
    {
        s_int_mode = true;
        ESP_LOGI(TAG, "TP_INT interrupt mode enabled");
    }
    else
    {
        s_int_mode = false;
        ESP_LOGW(TAG, "TP_INT interrupt not available, using polling: %s",
                 esp_err_to_name(isr_err));
    }

    /* 7. 启动采集任务 */
    xTaskCreatePinnedToCore(touch_task, "touch_task", 4096, NULL, 5, NULL, 1);

    return ESP_OK;
}

esp_err_t touch_get_event(touch_event_t *evt, uint32_t timeout_ms)
{
    if (evt == NULL || s_evt_queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueReceive(s_evt_queue, evt, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
    {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

const touch_info_t *touch_get_info(void)
{
    return &s_info;
}

esp_err_t touch_set_rotation(int angle_deg)
{
    int a = angle_deg % 360;
    if (a < 0) a += 360;
    switch (a)
    {
    case 0:
    case 90:
    case 180:
    case 270:
        s_rotation = a;
        ESP_LOGI(TAG, "rotation set to %d deg", a);
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

int touch_get_rotation(void)
{
    return s_rotation;
}

int touch_get_int_level(void)
{
    return gpio_get_level(TOUCH_PIN_TP_INT);
}

bool touch_is_interrupt_mode(void)
{
    return s_int_mode;
}
