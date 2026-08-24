/**
 * @file ui_page_chat.c
 * @brief 聊天页：Live2D 语音互动入口
 */

#include "lvgl.h"

#include "../ui_pages.h"
#include "../ui_strings.h"
#include "../ui_theme.h"

/* 进入语音互动模式选择页 */
static void chat_enter_live2d_cb(lv_event_t *event)
{
    (void)event;
    ui_pages_show_voice_mode();
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

    /* 语音互动入口（进入模式选择页） */
    lv_obj_t *live2d_btn = lv_button_create(page);
    lv_obj_set_size(live2d_btn, 220, 52);
    lv_obj_center(live2d_btn);
    ui_theme_apply_button(live2d_btn, true);
    lv_obj_t *live2d_label = lv_label_create(live2d_btn);
    lv_label_set_text(live2d_label, UI_STR_CHAT_ENTER_LIVE2D);
    lv_obj_center(live2d_label);
    lv_obj_add_event_cb(live2d_btn, chat_enter_live2d_cb, LV_EVENT_CLICKED, NULL);

    return page;
}
