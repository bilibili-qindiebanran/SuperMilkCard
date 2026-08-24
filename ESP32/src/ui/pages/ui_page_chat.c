/**
 * @file ui_page_chat.c
 * @brief 聊天页：Fluent 2 数据源占位卡片 + Live2D 互动入口
 */

#include "lvgl.h"

#include "../ui_pages.h"
#include "../ui_strings.h"
#include "../ui_theme.h"

/* 进入全屏 Live2D 互动页 */
static void chat_enter_live2d_cb(lv_event_t *event)
{
    (void)event;
    ui_pages_show_live2d();
}

lv_obj_t *ui_page_chat_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, UI_STR_CHAT_TITLE);
    lv_obj_set_pos(title, UI_MARGIN, 12);
    ui_theme_apply_title(title);

    lv_obj_t *subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, UI_STR_CHAT_SUBTITLE);
    lv_obj_set_pos(subtitle, UI_MARGIN, 38);
    lv_obj_set_width(subtitle, 320);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *card = lv_obj_create(page);
    lv_obj_set_pos(card, UI_MARGIN, 72);
    lv_obj_set_size(card, 448, 164);
    ui_theme_apply_card(card);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, "···");
    lv_obj_set_pos(icon, 24, 28);
    lv_obj_set_style_text_color(icon, UI_COLOR_PRIMARY, 0);

    lv_obj_t *wait = lv_label_create(card);
    lv_label_set_text(wait, UI_STR_CHAT_WAIT);
    lv_obj_set_pos(wait, 80, 34);
    lv_obj_set_style_text_color(wait, UI_COLOR_TEXT, 0);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, UI_STR_CHAT_HINT);
    lv_obj_set_pos(hint, 80, 72);
    lv_obj_set_width(hint, 340);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *status = lv_label_create(card);
    lv_label_set_text(status, UI_STR_STATE_OFF);
    lv_obj_align(status, LV_ALIGN_BOTTOM_RIGHT, -UI_GAP, -UI_GAP);
    ui_theme_apply_status_text(status, 0);

    /* Live2D 互动入口按钮 */
    lv_obj_t *live2d_btn = lv_button_create(page);
    lv_obj_set_size(live2d_btn, 200, 44);
    lv_obj_align(live2d_btn, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_GAP);
    ui_theme_apply_button(live2d_btn, true);
    lv_obj_t *live2d_label = lv_label_create(live2d_btn);
    lv_label_set_text(live2d_label, UI_STR_CHAT_ENTER_LIVE2D);
    lv_obj_center(live2d_label);
    lv_obj_add_event_cb(live2d_btn, chat_enter_live2d_cb, LV_EVENT_CLICKED, NULL);

    return page;
}
