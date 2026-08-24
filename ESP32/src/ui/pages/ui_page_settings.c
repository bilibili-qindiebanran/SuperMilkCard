/**
 * @file ui_page_settings.c
 * @brief Fluent 2 设置入口页
 */

#include <stdio.h>

#include "lvgl.h"

#include "../app_state.h"
#include "../touch.h"
#include "../ui_strings.h"
#include "../ui_theme.h"

static lv_obj_t *card_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    ui_theme_apply_card(card);
    return card;
}

static void setting_row(lv_obj_t *card, const char *title, const char *value,
                        lv_coord_t y, int status)
{
    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, UI_GAP, y);
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, value);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, -UI_GAP, y);
    ui_theme_apply_status_text(value_label, status);
}

lv_obj_t *ui_page_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, UI_STR_SETTINGS);
    lv_obj_set_pos(title, UI_MARGIN, 10);
    ui_theme_apply_title(title);

    lv_obj_t *subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, UI_STR_SETTINGS_SUBTITLE);
    lv_obj_set_pos(subtitle, UI_MARGIN, 35);
    lv_obj_set_width(subtitle, 360);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, 0);

    const touch_info_t *info = touch_get_info();
    char device[48];
    snprintf(device, sizeof(device), "%s  @0x%02X",
             info && info->detected ? info->name : UI_STR_UNKNOWN,
             info ? info->i2c_addr : 0);

    lv_obj_t *display_card = card_create(page, UI_MARGIN, 62, 214, 184);
    lv_obj_t *display_title = lv_label_create(display_card);
    lv_label_set_text(display_title, UI_STR_SETTINGS_DISPLAY);
    lv_obj_set_pos(display_title, UI_GAP, UI_GAP);
    lv_obj_set_style_text_color(display_title, UI_COLOR_TEXT, 0);
    setting_row(display_card, UI_STR_SETTINGS_ROT, "顺时针 90°", 48, 1);
    setting_row(display_card, UI_STR_SETTINGS_TOUCH, UI_STR_STATE_OK, 82, 1);
    setting_row(display_card, UI_STR_SETTINGS_BL, UI_STR_STATE_ON, 116, 1);
    setting_row(display_card, UI_STR_SETTINGS_FW, "LVGL 9", 150, 1);

    lv_obj_t *device_card = card_create(page, 242, 62, 222, 184);
    lv_obj_t *device_title = lv_label_create(device_card);
    lv_label_set_text(device_title, UI_STR_SETTINGS_DEVICE);
    lv_obj_set_pos(device_title, UI_GAP, UI_GAP);
    lv_obj_set_style_text_color(device_title, UI_COLOR_TEXT, 0);

    lv_obj_t *device_label = lv_label_create(device_card);
    lv_label_set_text(device_label, device);
    lv_obj_set_pos(device_label, UI_GAP, 52);
    lv_obj_set_width(device_label, 190);
    lv_label_set_long_mode(device_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *touch_label = lv_label_create(device_card);
    lv_label_set_text_fmt(touch_label, "TP_INT %d · %s", touch_get_int_level(),
                          touch_is_interrupt_mode() ? "中断" : "轮询");
    lv_obj_set_pos(touch_label, UI_GAP, 104);
    lv_obj_set_style_text_color(touch_label, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *tip = lv_label_create(device_card);
    lv_label_set_text(tip, UI_STR_SETTINGS_SYNC_HINT);
    lv_obj_set_pos(tip, UI_GAP, 136);
    lv_obj_set_width(tip, 190);
    lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(tip, UI_COLOR_TEXT_DIM, 0);

    return page;
}
