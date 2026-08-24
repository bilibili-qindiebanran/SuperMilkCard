/**
 * @file ui_theme.h
 * @brief Fluent 2 风格主题：浅色渐变、亚克力卡片、Fluent 蓝强调色
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_COLOR_BG_TOP       lv_color_hex(0xF5F9FF)
#define UI_COLOR_BG_BOTTOM    lv_color_hex(0xE8F1FF)
#define UI_COLOR_SURFACE      lv_color_hex(0xFFFFFF)
#define UI_COLOR_SURFACE_SOFT lv_color_hex(0xF7FAFF)
#define UI_COLOR_PRIMARY      lv_color_hex(0x0F6CBD)
#define UI_COLOR_PRIMARY_DARK lv_color_hex(0x0B5A9C)
#define UI_COLOR_PRIMARY_SOFT lv_color_hex(0xDCEEFF)
#define UI_COLOR_SUCCESS      lv_color_hex(0x107C10)
#define UI_COLOR_WARN         lv_color_hex(0xC77700)
#define UI_COLOR_OFFLINE      lv_color_hex(0x707070)
#define UI_COLOR_TEXT         lv_color_hex(0x1A1A1A)
#define UI_COLOR_TEXT_DIM     lv_color_hex(0x5D6875)
#define UI_COLOR_BORDER       lv_color_hex(0xD7E5F5)

#define UI_RADIUS             16
#define UI_RADIUS_SMALL       10
#define UI_MARGIN             16
#define UI_GAP                12
#define UI_NAV_HEIGHT         56
#define UI_SCREEN_W           480
#define UI_SCREEN_H           320
#define UI_CONTENT_H          (UI_SCREEN_H - UI_NAV_HEIGHT)

/* 定制中文字体子集：覆盖全部 UI 文案（102 字 + 标点 + ASCII） */
LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk);
LV_FONT_DECLARE(lv_font_ui_16);
#define UI_FONT_DEFAULT ui_theme_font()
LV_FONT_DECLARE(lv_font_montserrat_14);
#define UI_FONT_SMALL &lv_font_montserrat_14

void ui_theme_init(void);
const lv_font_t *ui_theme_font(void);
void ui_theme_apply_card(lv_obj_t *obj);
void ui_theme_apply_soft_card(lv_obj_t *obj);
void ui_theme_apply_button(lv_obj_t *obj, bool primary);
void ui_theme_apply_status_text(lv_obj_t *label, int status);
void ui_theme_apply_title(lv_obj_t *label);

#ifdef __cplusplus
}
#endif
