/**
 * @file lcd_ui.h
 * @brief ST77926 屏幕封装：基于乐鑫官方 esp_lcd_st77926 组件
 *
 * 使用方式：
 *   1. lcd_ui_init()      初始化（引脚/SPI/面板/帧缓冲/背光）
 *   2. lcd_ui_fill_* / lcd_ui_draw_string()  绘制到帧缓冲
 *   3. lcd_ui_flush()     分块上屏（每次调用刷新一帧）
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 引脚定义（platformio.ini 已定义，这里提供默认值兜底）              */
/* ------------------------------------------------------------------ */
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
#ifndef LCD_PIN_BL_EN
#define LCD_PIN_BL_EN 40
#endif
#ifndef LCD_PIN_RESET
#define LCD_PIN_RESET 41
#endif

/* 触摸屏相关（预留，驱动未实现）：
 *   LCD_PIN_TP_SDA=9 LCD_PIN_TP_SCL=10（与 IP5306 共用 I2C0）
 *   LCD_PIN_TP_INT=39 LCD_PIN_TP_RST=42
 */

/* ------------------------------------------------------------------ */
/* 屏幕参数与颜色                                                      */
/* ------------------------------------------------------------------ */
#define LCD_UI_W 320
#define LCD_UI_H 480

#define LCD_UI_RGB565(r, g, b) \
    (uint16_t)((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | (((b) & 0xFF) >> 3))
#define LCD_UI_RED   LCD_UI_RGB565(255, 0, 0)
#define LCD_UI_GREEN LCD_UI_RGB565(0, 255, 0)
#define LCD_UI_BLUE  LCD_UI_RGB565(0, 0, 255)
#define LCD_UI_WHITE LCD_UI_RGB565(255, 255, 255)
#define LCD_UI_BLACK LCD_UI_RGB565(0, 0, 0)
#define LCD_UI_YELLOW LCD_UI_RGB565(255, 255, 0)
#define LCD_UI_CYAN   LCD_UI_RGB565(0, 255, 255)

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化屏幕：引脚、SPI 总线、QSPI panel_io、官方面板驱动、帧缓冲、背光
 * @return ESP_OK 成功
 */
esp_err_t lcd_ui_init(void);

/**
 * @brief 直接提交一块位图到屏幕（不经帧缓冲，区域需 4 像素对齐）
 */
esp_err_t lcd_ui_draw_bitmap(int x0, int y0, int x1, int y1, const void *data);

/**
 * @brief 帧缓冲填充矩形（不直接上屏，需调用 lcd_ui_flush()）
 */
esp_err_t lcd_ui_fill_rect(int x0, int y0, int x1, int y1, uint16_t color);

/**
 * @brief 帧缓冲填充全屏
 */
esp_err_t lcd_ui_fill_screen(uint16_t color);

/**
 * @brief 帧缓冲绘制字符串（5x7 点阵字体，含背景色）
 */
esp_err_t lcd_ui_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg);

/**
 * @brief 帧缓冲绘制单个像素
 */
esp_err_t lcd_ui_draw_pixel(int x, int y, uint16_t color);

/**
 * @brief 帧缓冲绘制线段（Bresenham）
 */
esp_err_t lcd_ui_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

/**
 * @brief 帧缓冲绘制空心圆
 */
esp_err_t lcd_ui_draw_circle(int cx, int cy, int r, uint16_t color);

/**
 * @brief 帧缓冲绘制十字光标（交叉线）
 */
esp_err_t lcd_ui_draw_crosshair(int x, int y, int r, uint16_t color);

/**
 * @brief 整屏刷新：把帧缓冲分块上屏（每帧调用一次）
 */
esp_err_t lcd_ui_flush(void);

/**
 * @brief 背光控制（AW9364DNR）
 */
esp_err_t lcd_ui_set_backlight(bool on);

/**
 * @brief 等待 TE 撕裂同步边沿（超时自动放行，避免卡死）
 */
esp_err_t lcd_ui_wait_te(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
