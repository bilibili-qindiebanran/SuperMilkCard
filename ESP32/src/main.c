/**
 * @file main.c
 * @brief IP5306-I2C 电源管理 + I2S 音频采集 + ST77926 QSPI 屏幕，UART0 JustFloat 输出
 *
 * 架构：多 FreeRTOS 任务 + 共享缓存（单写单读，无锁安全）
 *
 *  任务1 IP5306 轮询（慢速 500ms）：
 *    状态机 IDLE→READING→UPDATING→IDLE，完整读 4 个寄存器后更新共享缓存，
 *    中途失败不写部分数据；若上次未完成则跳过本次（防 I2C 冲突）
 *
 *  任务2 JustFloat 输出（快速 20ms / 50Hz）：
 *    读共享缓存（IP5306 状态）+ 外部按键 + 麦克风 RMS/peak(dBFS)
 *    → 打包 JustFloat 帧 → UART0
 *
 *  任务3 屏幕刷新（10Hz 帧缓冲刷新）：
 *    ST77926 QSPI 屏：全帧缓冲绘制 → lcd_ui_flush() 分块上屏
 *    （TE 撕裂同步可通过 lcd_ui_wait_te() 启用，demo 暂未使用）
 *
 * JustFloat 帧（7 通道 float + 帧尾 0x00 0x00 0x80 0x7F）：
 *   ch1=charging  ch2=charge_full  ch3=light_load
 *   ch4=麦克风 RMS(dBFS)  ch5=麦克风峰值(dBFS)
 *   ch6=按键1(IO38)  ch7=按键2(IO4)   （按下=1，外部10k上拉）
 *
 * 串口分工：UART0(GPIO43/44)=JustFloat 二进制，USB-Serial/JTAG=日志
 */

#include <math.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_bus.h"
#include "ip5306.h"
#include "i2s_audio.h"
#include "audio_voice.h"
#include "lcd_ui.h"
#include "touch.h"
#include "touch_test_ui.h"
#include "network/net_app.h"
#include "ui/app_state.h"
#include "ui_app.h"
#include "uart_justfloat.h"

static const char *TAG = "main";

/* ================================================================== */
/* 外部按键（IO38 / IO4）                                              */
/* 初始化由 board_keys 模块接管（ui_port_input 调用）；此处仅保留      */
/* 原始电平读取给 JustFloat 通道（ch6/ch7）                            */
/* ================================================================== */
#ifndef KEY1_GPIO
#define KEY1_GPIO 38
#endif
#ifndef KEY2_GPIO
#define KEY2_GPIO 4
#endif

/* 读取按键原始电平：低=按下 → 返回 1 */
static inline int key_read(int gpio)
{
    return (gpio_get_level(gpio) == 0) ? 1 : 0;
}

/* ================================================================== */
/* 共享缓存：IP5306 状态（单写单读，无锁）                             */
/* ================================================================== */
static ip5306_status_t s_ip5306_cache;

/* IP5306 轮询任务状态机 */
typedef enum {
    IP5306_STATE_IDLE,     /* 空闲，可发起新一轮读取 */
    IP5306_STATE_READING,  /* 正在读寄存器（未完成，跳过下次） */
    IP5306_STATE_UPDATING, /* 读取完成，准备写缓存 */
} ip5306_task_state_t;

static volatile ip5306_task_state_t s_ip5306_state = IP5306_STATE_IDLE;

/* ================================================================== */
/* 任务1：IP5306 慢速轮询（状态机）                                    */
/* ================================================================== */
static void ip5306_poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "IP5306 poll task started (500ms interval)");
    int diag = 0;

    while (1) {
        /* 状态机：仅 IDLE 状态允许发起读取，防止重入/冲突 */
        if (s_ip5306_state == IP5306_STATE_IDLE) {
            s_ip5306_state = IP5306_STATE_READING;

            ip5306_status_t st;
            esp_err_t err = ip5306_get_status(&st);
            bool ok = (err == ESP_OK);

            /* 诊断：前 5 次打印；之后仅在按键事件变化时打印 */
            static uint8_t prev_key;
            if (diag < 5) {
                diag++;
                if (ok) {
                    ESP_LOGI(TAG, "IP5306 poll OK: chg=%d full=%d light=%d key_s=%d key_l=%d key_d=%d",
                             st.charging, st.charge_full, st.light_load,
                             st.key_short, st.key_long, st.key_double);
                } else {
                    ESP_LOGE(TAG, "IP5306 poll failed: %s", esp_err_to_name(err));
                }
            } else if (ok) {
                uint8_t cur_key = (st.key_short ? 1 : 0) | (st.key_long ? 2 : 0) | (st.key_double ? 4 : 0);
                if (cur_key != prev_key) {
                    prev_key = cur_key;
                    ESP_LOGI(TAG, "KEY event: short=%d long=%d double=%d",
                             st.key_short, st.key_long, st.key_double);
                }
            }

            /* 无论成功失败都回到 IDLE，下次再试；失败则缓存保持旧值 */
            s_ip5306_state = IP5306_STATE_UPDATING;
            if (ok) {
                s_ip5306_cache = st; /* 单写：仅此任务写 */
            }
            s_ip5306_state = IP5306_STATE_IDLE;
        }
        /* 若仍处于 READING（理论上不会，因为单任务）也放行，避免卡死 */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ================================================================== */
/* 任务2：JustFloat 快速输出（50Hz）                                   */
/* ================================================================== */
static void justfloat_output_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "JustFloat output task started (50Hz)");

    while (1) {
        /* 读缓存（IP5306 状态）——只读，无锁安全 */
        ip5306_status_t st = s_ip5306_cache;

        /* 读麦克风一帧，计算 RMS 和峰值并转 dBFS（64 帧非阻塞，约 2.7ms 数据） */
        static int32_t mic_buf[64 * 2];
        float mic_rms_db = -100.0f, mic_peak_db = -100.0f;
        if (i2s_audio_read(mic_buf, 64) == ESP_OK)
        {
            int64_t acc = 0;
            int32_t peak = 0;
            for (size_t i = 0; i < 64; i++)
            {
                int32_t l = mic_buf[i * 2] >> 8; /* 24-bit 数据 → 16-bit 有效 */
                int32_t a = (l < 0) ? -l : l;
                if (a > peak) peak = a;
                acc += (int64_t)l * l;
            }
            /* RX 是 24-bit 位宽，数据为 24-bit 值（>>8 后仍可能 >32768）。
             * dBFS 用 24-bit 满幅 8388608 作基准 */
            float rms = sqrtf((float)acc / 64);
            mic_rms_db = 20.0f * log10f((rms + 1.0f) / 8388608.0f);
            mic_peak_db = 20.0f * log10f(((float)peak + 1.0f) / 8388608.0f);
            if (mic_rms_db < -100.0f) mic_rms_db = -100.0f;
            if (mic_peak_db < -100.0f) mic_peak_db = -100.0f;
        }

        /* 读取外部按键状态（GPIO 实时电平，按下=1） */
        int key1 = key_read(KEY1_GPIO);
        int key2 = key_read(KEY2_GPIO);

        float data[7] = {
            st.charging ? 1.0f : 0.0f,
            st.charge_full ? 1.0f : 0.0f,
            st.light_load ? 1.0f : 0.0f,
            mic_rms_db,
            mic_peak_db,
            (float)key1,
            (float)key2,
        };
        /* JustFloat 数据走 UART0（GPIO43/44 → VOFA+），日志仍走 USB */
        uart_justfloat_send(data, 7);

        /* 发布到 app_state（UI 读取） */
        app_state_publish_power(st.charging, st.charge_full, st.light_load);
        app_state_publish_audio(mic_rms_db, mic_peak_db);

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ================================================================== */
/* 任务3：屏幕显示（触摸测试 UI，替代原 Hello World demo）            */
/* 触摸测试 UI 由 touch_test_ui_start() 创建独立任务，无需在此定义    */
/* ================================================================== */

/* ================================================================== */
/* 启动辅助                                                           */
/* ================================================================== */
static void print_config_regs(void)
{
    static const struct
    {
        uint8_t reg;
        const char *name;
    } regs[] = {
        {IP5306_REG_SYS_CTL0, "SYS_CTL0    (0x00)"},
        {IP5306_REG_SYS_CTL1, "SYS_CTL1    (0x01)"},
        {IP5306_REG_SYS_CTL2, "SYS_CTL2    (0x02)"},
        {IP5306_REG_CHG_CTL0, "Charger_CTL0(0x20)"},
        {IP5306_REG_CHG_CTL1, "Charger_CTL1(0x21)"},
        {IP5306_REG_CHG_CTL2, "Charger_CTL2(0x22)"},
        {IP5306_REG_CHG_CTL3, "Charger_CTL3(0x23)"},
        {IP5306_REG_CHG_DIG_CTL0, "CHG_DIG_CTL0(0x24)"},
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
    {
        uint8_t v = 0;
        if (ip5306_read_reg(regs[i].reg, &v) == ESP_OK)
        {
            ESP_LOGI(TAG, "  %s = 0x%02X", regs[i].name, v);
        }
        else
        {
            ESP_LOGE(TAG, "  %s read failed", regs[i].name);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SuperMilkCard ESP32-S3 starting...");

    /* 0. 共享 I2C0 总线（IP5306 + 触摸屏共用，必须先初始化） */
    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_bus_init failed, abort");
        return;
    }

    /* 1. IP5306 初始化 + 在线检测 */
    err = ip5306_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ip5306_init failed, abort");
        return;
    }

    uint8_t reg0 = 0xFF;
    err = ip5306_read_reg(IP5306_REG_READ0, &reg0);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, ">>> IP5306 online! REG_READ0(0x70)=0x%02X (bit3 charge_en=%d)",
                 reg0, (reg0 >> 3) & 1);
    }
    else
    {
        ESP_LOGW(TAG, ">>> IP5306 init read failed: %s (will retry in poll task)",
                 esp_err_to_name(err));
        /* 不 return：即使 IP5306 暂不可达，JustFloat 输出照常运行 */
    }

    /* 2. 出厂配置寄存器（仅启动时打印一次） */
    ESP_LOGI(TAG, "--- factory config registers ---");
    print_config_regs();

    /* 3. I2S 音频初始化（ICS-43434 麦克风） */
    ESP_LOGI(TAG, "--- I2S audio init ---");
    err = i2s_audio_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_audio_init failed: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "I2S audio ready (mic), loopback disabled.");
        /* 语音上行任务（录音 → Windows STT） */
        audio_voice_init();
    }

    /* 4. UART0 JustFloat 初始化 */
    err = uart_justfloat_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_justfloat_init failed: %s", esp_err_to_name(err));
    }

    /* 4.6 ST77926 QSPI 屏幕初始化（乐鑫官方组件）+ 背光点亮 */
    ESP_LOGI(TAG, "--- ST77926 QSPI LCD init (official component) ---");
    bool lcd_ok = (lcd_ui_init() == ESP_OK);
    if (!lcd_ok)
    {
        ESP_LOGE(TAG, "lcd_ui_init failed (screen disabled)");
    }
    else
    {
        esp_err_t bl_err = lcd_ui_set_backlight(true);
        if (bl_err != ESP_OK)
        {
            ESP_LOGE(TAG, "lcd_ui_set_backlight failed: %s", esp_err_to_name(bl_err));
        }
        ESP_LOGI(TAG, "LCD ready (official component)");
    }

    /* 4.7 触摸屏初始化（共享 I2C0 总线，探测控制器） */
    ESP_LOGI(TAG, "--- touch init ---");
    err = touch_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "touch_init failed: %s", esp_err_to_name(err));
    }

    /* 4.8 网络：NVS + Wi-Fi（STA/配网）+ TCP 服务端 + 设备发现 */
    ESP_LOGI(TAG, "--- network init ---");
    err = net_app_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "net_app_start failed: %s", esp_err_to_name(err));
    }

    /* 5. 启动并行任务
     * 优先级：IP5306(7) > JustFloat(6) > touch_task(5) > touch_ui(4)
     * IP5306 最高：保证电源轮询不被触摸高频 I2C 抢占导致超时 */
    xTaskCreatePinnedToCore(ip5306_poll_task, "ip5306_poll", 4096, NULL, 7, NULL, 1);
    /* JustFloat 输出已禁用：其 50Hz I2S 轮询会抢走语音录音的 DMA 数据。
     * 如需恢复，重新启用下面这行并在 uart_justfloat_set_enabled(true) 打开输出。 */
    /* xTaskCreatePinnedToCore(justfloat_output_task, "justfloat_out", 4096, NULL, 6, NULL, 1); */
    if (lcd_ok)
    {
        /* 阶段2：临时切到 LVGL 产品 UI（验证显示端口），
         * 触摸测试 UI 保留为诊断工具（touch_test_ui.h 仍可手动调用） */
        ui_app_start();
    }

    /* 主任务不再做事，挂起 */
    vTaskDelete(NULL);
}
