/**
 * @file ui_theme.c
 * @brief Fluent 2 风格主题实现
 */

#include "ui_theme.h"

static lv_style_t s_card_style;
static lv_style_t s_soft_card_style;
static lv_style_t s_primary_button_style;
static lv_style_t s_secondary_button_style;
static lv_font_t s_ui_font;
static bool s_font_ready;
static bool s_ready;

const lv_font_t *ui_theme_font(void)
{
    if (!s_font_ready)
    {
        /* 主字体用全量 GB2312 字库（覆盖 AI 动态内容），
         * fallback 保留内置 CJK 子集 + 定制 UI 字集兜底 */
        s_ui_font = lv_font_ui_16_full;
        s_ui_font.fallback = &lv_font_source_han_sans_sc_16_cjk;
        s_font_ready = true;
    }
    return &s_ui_font;
}

static void init_card_style(lv_style_t *style, lv_color_t color, lv_opa_t opa)
{
    lv_style_init(style);
    lv_style_set_bg_color(style, color);
    lv_style_set_bg_opa(style, opa);
    lv_style_set_text_font(style, UI_FONT_DEFAULT);
    lv_style_set_radius(style, UI_RADIUS);
    lv_style_set_border_width(style, 1);
    lv_style_set_border_color(style, UI_COLOR_BORDER);
    lv_style_set_border_opa(style, LV_OPA_80);
    lv_style_set_shadow_width(style, 12);
    lv_style_set_shadow_opa(style, LV_OPA_20);
    lv_style_set_shadow_color(style, lv_color_hex(0x6D8FB5));
    /* 卡片内容全部采用显式坐标，避免默认内边距叠加后文字越界。 */
    lv_style_set_pad_all(style, 0);
}

void ui_theme_init(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, UI_COLOR_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(screen, UI_COLOR_BG_BOTTOM, 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_font(screen, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(screen, UI_COLOR_TEXT, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    if (!s_ready)
    {
        init_card_style(&s_card_style, UI_COLOR_SURFACE, LV_OPA_90);
        init_card_style(&s_soft_card_style, UI_COLOR_SURFACE_SOFT, LV_OPA_80);

        lv_style_init(&s_primary_button_style);
        lv_style_set_bg_color(&s_primary_button_style, UI_COLOR_PRIMARY);
        lv_style_set_bg_opa(&s_primary_button_style, LV_OPA_COVER);
        lv_style_set_text_font(&s_primary_button_style, UI_FONT_DEFAULT);
        lv_style_set_text_color(&s_primary_button_style, lv_color_hex(0xFFFFFF));
        lv_style_set_radius(&s_primary_button_style, UI_RADIUS_SMALL);
        lv_style_set_shadow_width(&s_primary_button_style, 8);
        lv_style_set_shadow_opa(&s_primary_button_style, LV_OPA_20);
        lv_style_set_shadow_color(&s_primary_button_style, UI_COLOR_PRIMARY);

        lv_style_init(&s_secondary_button_style);
        lv_style_set_bg_color(&s_secondary_button_style, UI_COLOR_PRIMARY_SOFT);
        lv_style_set_bg_opa(&s_secondary_button_style, LV_OPA_COVER);
        lv_style_set_text_font(&s_secondary_button_style, UI_FONT_DEFAULT);
        lv_style_set_text_color(&s_secondary_button_style, UI_COLOR_PRIMARY_DARK);
        lv_style_set_radius(&s_secondary_button_style, UI_RADIUS_SMALL);
        s_ready = true;
    }
}

void ui_theme_apply_card(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_card_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_theme_apply_soft_card(lv_obj_t *obj)
{
    lv_obj_add_style(obj, &s_soft_card_style, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void ui_theme_apply_button(lv_obj_t *obj, bool primary)
{
    lv_obj_add_style(obj, primary ? &s_primary_button_style : &s_secondary_button_style, 0);
    lv_obj_set_style_pad_left(obj, 14, 0);
    lv_obj_set_style_pad_right(obj, 14, 0);
    lv_obj_set_style_pad_top(obj, 8, 0);
    lv_obj_set_style_pad_bottom(obj, 8, 0);
}

void ui_theme_apply_status_text(lv_obj_t *label, int status)
{
    lv_color_t color = UI_COLOR_OFFLINE;
    if (status == 1) color = UI_COLOR_SUCCESS;
    else if (status == 2) color = UI_COLOR_WARN;
    lv_obj_set_style_text_font(label, UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

void ui_theme_apply_title(lv_obj_t *label)
{
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label, UI_FONT_DEFAULT, 0);
}
