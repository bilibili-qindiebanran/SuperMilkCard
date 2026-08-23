/**
 * @file ui_theme.h
 * @brief 产品 UI 主题：深色背景 + 状态色
 *
 * 视觉变量：深色背景 / 2-3 级表面色 / 主强调色 / 成功/警告/离线色
 * 统一 12px 圆角与 12px 页面边距。
 * 字体使用 LVGL 官方思源黑体简体 CJK（lv_font_source_han_sans_sc_16_cjk）。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 颜色                                                                */
/* ------------------------------------------------------------------ */
#define UI_COLOR_BG        lv_color_hex(0x1A1A2E)   /* 深色背景 */
#define UI_COLOR_SURFACE   lv_color_hex(0x24243E)   /* 表面色 1 */
#define UI_COLOR_SURFACE_2 lv_color_hex(0x2E2E4A)   /* 表面色 2 */
#define UI_COLOR_PRIMARY   lv_color_hex(0xE94560)   /* 主强调色 */
#define UI_COLOR_SUCCESS   lv_color_hex(0x0FA96B)   /* 成功/充电 */
#define UI_COLOR_WARN      lv_color_hex(0xF5A623)   /* 警告/待同步 */
#define UI_COLOR_OFFLINE   lv_color_hex(0x6B6B8A)   /* 离线/未接入 */
#define UI_COLOR_TEXT      lv_color_hex(0xECECF2)   /* 主文字 */
#define UI_COLOR_TEXT_DIM  lv_color_hex(0x9A9AB0)   /* 次要文字 */

/* 布局 */
#define UI_RADIUS 12          /* 卡片圆角 */
#define UI_MARGIN 12          /* 页面边距 */
#define UI_GAP    12          /* 卡片间距 */

/* ------------------------------------------------------------------ */
/* 字体（官方思源黑体简体 CJK，LVGL 内置）                            */
/* ------------------------------------------------------------------ */
LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk);
#define UI_FONT_DEFAULT &lv_font_source_han_sans_sc_16_cjk
/* 小字号（ASCII）备用 */
LV_FONT_DECLARE(lv_font_montserrat_14);
#define UI_FONT_SMALL &lv_font_montserrat_14

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化主题：背景色 + 默认字体（screen 继承）
 */
void ui_theme_init(void);

/**
 * @brief 应用卡片样式（圆角/表面色）
 */
void ui_theme_apply_card(lv_obj_t *obj);

/**
 * @brief 应用状态色文字（成功/警告/离线）
 */
void ui_theme_apply_status_text(lv_obj_t *label, int status);

#ifdef __cplusplus
}
#endif
