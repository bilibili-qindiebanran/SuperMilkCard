/**
 * @file lcd_ui.h
 * @brief ST77926 屏幕封装：基于乐鑫官方 esp_lcd_st77926 组件
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚定义（platformio.ini 已定义） */
#ifndef LCD_PIN_TP_SDA
#define LCD_PIN_TP_SDA 9
#endif
#ifndef LCD_PIN_TP_SCL
#define LCD_PIN_TP_SCL 10
#endif
#ifndef LCD_PIN_TE
#define LCD_PIN_TE 11
#endif
#ifndef LCD_PIN_CS
#define LCD_PIN_CS 12
#endif
#ifndef LCD_PIN_SCLK
#define LCD_PIN_SCLK 13
#endif
#ifndef LCD_PIN_D0
#define LCD_PIN_D0 14
#endif
#ifndef LCD_PIN_D1
#define LCD_PIN_D1 21
#endif
#ifndef LCD_PIN_D2
#define LCD_PIN_D2 47
#endif
#ifndef LCD_PIN_D3
#define LCD_PIN_D3 48
#endif
#ifndef LCD_PIN_TP_INT
#define LCD_PIN_TP_INT 39
#endif
#ifndef LCD_PIN_BL_EN
#define LCD_PIN_BL_EN 40
#endif
#ifndef LCD_PIN_RESET
#define LCD_PIN_RESET 41
#endif
#ifndef LCD_PIN_TP_RST
#define LCD_PIN_TP_RST 42
#endif

/* 屏幕参数 */
#define LCD_UI_W 320
#define LCD_UI_H 480

/* 颜色 */
#define LCD_UI_RGB565(r, g, b) \
    (uint16_t)((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | (((b) & 0xFF) >> 3))
#define LCD_UI_RED   LCD_UI_RGB565(255, 0, 0)
#define LCD_UI_GREEN LCD_UI_RGB565(0, 255, 0)
#define LCD_UI_BLUE  LCD_UI_RGB565(0, 0, 255)
#define LCD_UI_WHITE LCD_UI_RGB565(255, 255, 255)
#define LCD_UI_BLACK LCD_UI_RGB565(0, 0, 0)
#define LCD_UI_YELLOW LCD_UI_RGB565(255, 255, 0)
#define LCD_UI_CYAN   LCD_UI_RGB565(0, 255, 255)

esp_err_t lcd_ui_init(void);
esp_err_t lcd_ui_draw_bitmap(int x0, int y0, int x1, int y1, const void *data);
esp_err_t lcd_ui_fill_rect(int x0, int y0, int x1, int y1, uint16_t color);
esp_err_t lcd_ui_fill_screen(uint16_t color);
esp_err_t lcd_ui_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg);
esp_err_t lcd_ui_flush(void);
esp_err_t lcd_ui_set_backlight(bool on);
esp_err_t lcd_ui_wait_te(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
