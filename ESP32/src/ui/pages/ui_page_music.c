/**
 * @file ui_page_music.c
 * @brief 音乐页：Fluent 2 媒体占位卡片
 */

#include "lvgl.h"

#include "../ui_strings.h"
#include "../ui_theme.h"

lv_obj_t *ui_page_music_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, UI_STR_MUSIC);
    lv_obj_set_pos(title, UI_MARGIN, 12);
    ui_theme_apply_title(title);

    lv_obj_t *subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, UI_STR_MUSIC_SUBTITLE);
    lv_obj_set_pos(subtitle, UI_MARGIN, 38);
    lv_obj_set_width(subtitle, 360);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *card = lv_obj_create(page);
    lv_obj_set_pos(card, UI_MARGIN, 72);
    lv_obj_set_size(card, 448, 164);
    ui_theme_apply_card(card);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, UI_STR_MUSIC_PLACE);
    lv_obj_set_pos(title_label, 24, 34);
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT, 0);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, UI_STR_MUSIC_HINT);
    lv_obj_set_pos(hint, 24, 72);
    lv_obj_set_width(hint, 400);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *button = lv_button_create(card);
    lv_obj_set_size(button, 100, 40);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -UI_GAP, -UI_GAP);
    ui_theme_apply_button(button, false);
    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, UI_STR_MUSIC_CONNECT);
    lv_obj_center(button_label);
    return page;
}
