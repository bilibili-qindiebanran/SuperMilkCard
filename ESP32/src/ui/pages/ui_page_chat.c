/**
 * @file ui_page_chat.c
 * @brief 聊天页（首版占位：数据源未接入）
 */

#include "lvgl.h"

#include "../app_state.h"
#include "../ui_strings.h"
#include "../ui_theme.h"

lv_obj_t *ui_page_chat_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, UI_STR_CHAT_TITLE);
    lv_obj_set_pos(title, UI_MARGIN, UI_MARGIN);
    lv_obj_set_style_text_font(title, UI_FONT_DEFAULT, 0);

    /* 占位卡片 */
    lv_obj_t *card = lv_obj_create(page);
    lv_obj_set_pos(card, UI_MARGIN, 48);
    lv_obj_set_size(card, lv_pct(100) - 2 * UI_MARGIN, 200);
    ui_theme_apply_card(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wait = lv_label_create(card);
    lv_label_set_text(wait, UI_STR_CHAT_WAIT);
    lv_obj_center(wait);
    ui_theme_apply_status_text(wait, 0); /* 离线色 */

    return page;
}
