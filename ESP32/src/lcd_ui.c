/**
 * @file lcd_ui.c
 * @brief ST77926 屏幕封装：乐鑫官方 esp_lcd_st77926 组件 + 全帧缓冲按帧刷新
 *
 * 架构：
 *   - 所有绘制写入 PSRAM 全帧缓冲（320x480x2 = 300KB）
 *   - lcd_ui_flush() 按行分块提交，并在复用 DMA 缓冲前等待传输完成
 *   - 避免碎片窗口写入/对齐问题/刷新闪烁
 *
 * 接线（platformio.ini）：
 *   TE=IO11 CS=IO12 SCLK=IO13 D0=IO14 D1=IO21 D2=IO47 D3=IO48
 *   TP_INT=IO39 BL_EN=IO40 RST=IO41 TP_RST=IO42
 */

#include "lcd_ui.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_st77926.h"
#include "font5x7.h"

static const char *TAG = "lcd_ui";

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static uint16_t *s_fb = NULL; /* 全帧缓冲（PSRAM） */

/* ================================================================== */
/* AW9364DNR 背光（EN 上升沿计数调光，保持高=20mA 满亮）             */
/* ================================================================== */
#define AW9364_READY_US 100

static void aw9364_backlight_on(void)
{
    gpio_set_level(LCD_PIN_BL_EN, 1);
    esp_rom_delay_us(AW9364_READY_US);
}

static void aw9364_backlight_off(void)
{
    gpio_set_level(LCD_PIN_BL_EN, 0);
    esp_rom_delay_us(3000); /* TSHDN ≥2.5ms */
}

/* RGB565 字节序交换：ST77926 期望高字节在前（大端），ESP32 小端需交换 */
static inline uint16_t lcd_swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

/* ================================================================== */
/* 初始化                                                             */
/* ================================================================== */
esp_err_t lcd_ui_init(void)
{
    if (s_panel != NULL)
    {
        return ESP_OK;
    }

    /* 1. 控制引脚：BL_EN/RST 输出，TE 输入 */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << LCD_PIN_BL_EN) | (1ULL << LCD_PIN_RESET) | (1ULL << LCD_PIN_TP_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << LCD_PIN_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);
    aw9364_backlight_off();
    gpio_set_level(LCD_PIN_TP_RST, 1);
    gpio_set_level(LCD_PIN_RESET, 1);

    /* 2. SPI 总线（QSPI 4 线） */
    spi_bus_config_t buscfg = ST77926_PANEL_BUS_QSPI_CONFIG(
        LCD_PIN_SCLK, LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
        320 * 480 * 2);
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 3. Panel IO（官方 QSPI 配置，SPI 时钟提升到 80MHz） */
    esp_lcd_panel_io_spi_config_t io_config = ST77926_PANEL_IO_QSPI_CONFIG(LCD_PIN_CS, NULL, NULL);
    io_config.pclk_hz = 80 * 1000 * 1000; /* 官方默认 40MHz，ST77926 QSPI 支持 80MHz */
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &s_io);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 4. 官方 ST77926 面板驱动 */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st77926(s_io, &panel_config, &s_panel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st77926 failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 5. 复位 + 初始化 + 开显示 */
    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_lcd_panel_reset failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_lcd_panel_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 6. 全帧缓冲（优先 PSRAM，回退内部 RAM） */
    s_fb = heap_caps_malloc(LCD_UI_W * LCD_UI_H * 2, MALLOC_CAP_SPIRAM);
    if (s_fb == NULL)
    {
        s_fb = heap_caps_malloc(LCD_UI_W * LCD_UI_H * 2, MALLOC_CAP_8BIT);
    }
    if (s_fb == NULL)
    {
        ESP_LOGE(TAG, "no framebuffer available");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "framebuffer: %d bytes", LCD_UI_W * LCD_UI_H * 2);

    /* 7. 开背光 */
    aw9364_backlight_on();

    ESP_LOGI(TAG, "ST77926 ready (official component, framebuffer mode)");
    return ESP_OK;
}

/* ================================================================== */
/* 帧缓冲绘制（写入 s_fb，不直接发屏）                                */
/* ================================================================== */
static inline uint16_t *fb_pixel(int x, int y)
{
    return &s_fb[y * LCD_UI_W + x];
}

esp_err_t lcd_ui_fill_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (s_fb == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LCD_UI_W) x1 = LCD_UI_W;
    if (y1 > LCD_UI_H) y1 = LCD_UI_H;
    uint16_t c = lcd_swap16(color);
    for (int y = y0; y < y1; y++)
    {
        uint16_t *row = fb_pixel(x0, y);
        for (int x = x0; x < x1; x++)
        {
            row[x - x0] = c;
        }
    }
    return ESP_OK;
}

esp_err_t lcd_ui_fill_screen(uint16_t color)
{
    return lcd_ui_fill_rect(0, 0, LCD_UI_W, LCD_UI_H, color);
}

esp_err_t lcd_ui_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    if (s_fb == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    uint16_t f = lcd_swap16(fg);
    uint16_t b = lcd_swap16(bg);
    int cx = x;
    while (*s)
    {
        char c = *s++;
        if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E)
        {
            c = '?';
        }
        const uint8_t *glyph = font5x7[(uint8_t)c - 0x20];
        for (int row = 0; row < 7; row++)
        {
            for (int col = 0; col < 5; col++)
            {
                int px = cx + col;
                int py = y + row;
                if (px >= 0 && px < LCD_UI_W && py >= 0 && py < LCD_UI_H)
                {
                    *fb_pixel(px, py) = ((glyph[row] >> (6 - col)) & 1) ? f : b;
                }
            }
        }
        cx += 6; /* 5 像素 + 1 间距 */
    }
    return ESP_OK;
}

/* ================================================================== */
/* 绘制辅助（帧缓冲）                                                  */
/* ================================================================== */
esp_err_t lcd_ui_draw_pixel(int x, int y, uint16_t color)
{
    if (s_fb == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (x >= 0 && x < LCD_UI_W && y >= 0 && y < LCD_UI_H)
    {
        *fb_pixel(x, y) = lcd_swap16(color);
    }
    return ESP_OK;
}

esp_err_t lcd_ui_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (s_fb == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    /* Bresenham 直线 */
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (1)
    {
        *fb_pixel(x0, y0) = lcd_swap16(color);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
    return ESP_OK;
}

esp_err_t lcd_ui_draw_circle(int cx, int cy, int r, uint16_t color)
{
    if (s_fb == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    /* 中点画圆（仅轮廓） */
    int x = r, y = 0, err = 1 - r;
    while (x >= y)
    {
        lcd_ui_draw_pixel(cx + x, cy + y, color);
        lcd_ui_draw_pixel(cx - x, cy + y, color);
        lcd_ui_draw_pixel(cx + x, cy - y, color);
        lcd_ui_draw_pixel(cx - x, cy - y, color);
        lcd_ui_draw_pixel(cx + y, cy + x, color);
        lcd_ui_draw_pixel(cx - y, cy + x, color);
        lcd_ui_draw_pixel(cx + y, cy - x, color);
        lcd_ui_draw_pixel(cx - y, cy - x, color);
        y++;
        if (err < 0)
        {
            err += 2 * y + 1;
        }
        else
        {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
    return ESP_OK;
}

esp_err_t lcd_ui_draw_crosshair(int x, int y, int r, uint16_t color)
{
    if (s_fb == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    lcd_ui_draw_line(x - r, y, x + r, y, color);
    lcd_ui_draw_line(x, y - r, x, y + r, color);
    return ESP_OK;
}

/* ================================================================== */
/* 整屏刷新：按行分块经内部 RAM 发送（规避 PSRAM DMA underflow）      */
/* ================================================================== */
#define FB_CHUNK_ROWS 64 /* 每块 64 行 = 320*64*2 = 40KB，480/64=8 次事务 */
static uint16_t s_fb_chunk[LCD_UI_W * FB_CHUNK_ROWS] __attribute__((aligned(4)));

esp_err_t lcd_ui_flush(void)
{
    if (s_panel == NULL || s_fb == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    /* 分块：每块 16 行，从 PSRAM 搬到内部 RAM 再发送。
     * 官方驱动要求 x 起点/宽度 4 对齐（320 满足），且规避
     * PSRAM 直接做 DMA 源触发 ESP32-S3 underflow。
     *
     * esp_lcd_panel_draw_bitmap() 的颜色数据通过 DMA 异步发送；
     * s_fb_chunk 必须在传输结束后才可覆写。以空参数事务作为
     * ESP-IDF 提供的队列同步屏障，等待当前分块完成。 */
    for (int y = 0; y < LCD_UI_H; y += FB_CHUNK_ROWS)
    {
        int rows = (LCD_UI_H - y) > FB_CHUNK_ROWS ? FB_CHUNK_ROWS : (LCD_UI_H - y);
        memcpy(s_fb_chunk, &s_fb[y * LCD_UI_W], rows * LCD_UI_W * 2);
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_UI_W, y + rows, s_fb_chunk);
        if (err != ESP_OK)
        {
            return err;
        }
        err = esp_lcd_panel_io_tx_param(s_io, -1, NULL, 0);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t lcd_ui_draw_bitmap(int x0, int y0, int x1, int y1, const void *data)
{
    if (s_panel == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, data);
}

esp_err_t lcd_ui_set_backlight(bool on)
{
    if (on)
    {
        aw9364_backlight_on();
    }
    else
    {
        aw9364_backlight_off();
    }
    return ESP_OK;
}

/* 等待 TE 帧同步（超时放行） */
esp_err_t lcd_ui_wait_te(uint32_t timeout_ms)
{
    static int last = -1;
    TickType_t start = xTaskGetTickCount();
    while (1)
    {
        int lvl = gpio_get_level(LCD_PIN_TE);
        if (lvl != last)
        {
            last = lvl;
            return ESP_OK;
        }
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms))
        {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
