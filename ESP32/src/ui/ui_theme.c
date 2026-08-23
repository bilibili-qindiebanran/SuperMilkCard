/**
 * @file ui_theme.c
 * @brief 产品 UI 主题实现
 */

#include "ui_theme.h"

#include "esp_log.h"

static const char *TAG = "ui_theme";

/* 卡片样式（静态，初始化一次） */
static lv_style_t s_style_card;
static bool s_card_ready;

void ui_theme_init(void)
{
    /* 背景色 */
    lv_obj_set_style_bg_color(lv_scr_act(), UI_COLOR_BG, 0);

    /* 默认字体与文字色：text_font/text_color 可继承，子对象默认使用 */
    lv_obj_set_style_text_font(lv_scr_act(), UI_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(lv_scr_act(), UI_COLOR_TEXT, 0);

    ESP_LOGI(TAG, "theme initialized (dark, font=han_sans_cjk)");
}

void ui_theme_apply_card(lv_obj_t *obj)
{
    if (!s_card_ready)
    {
        lv_style_init(&s_style_card);
        lv_style_set_bg_color(&s_style_card, UI_COLOR_SURFACE);
        lv_style_set_radius(&s_style_card, UI_RADIUS);
        lv_style_set_pad_all(&s_style_card, UI_GAP);
        s_card_ready = true;
    }
    lv_obj_add_style(obj, &s_style_card, 0);
}

void ui_theme_apply_status_text(lv_obj_t *label, int status)
{
    /* status: 0=离线/未接入(灰) 1=正常/成功(绿) 2=警告(橙) */
    lv_color_t c;
    switch (status)
    {
    case 1:  c = UI_COLOR_SUCCESS; break;
    case 2:  c = UI_COLOR_WARN;    break;
    default: c = UI_COLOR_OFFLINE; break;
    }
    lv_obj_set_style_text_color(label, c, 0);
}
