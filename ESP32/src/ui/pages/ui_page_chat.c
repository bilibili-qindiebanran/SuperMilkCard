/**
 * @file ui_page_chat.c
 * @brief 聊天页：Fluent 2 数据源占位卡片
 */

#include "lvgl.h"

#include "../ui_strings.h"
#include "../ui_theme.h"

lv_obj_t *ui_page_chat_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
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
    return page;
}
