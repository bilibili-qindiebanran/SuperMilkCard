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
#include "../../network/net_wifi.h"

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

/* 网络状态：Wi-Fi/TCP + 配网按钮回调 */
static lv_obj_t *s_net_wifi_value;
static lv_obj_t *s_net_tcp_value;
static lv_obj_t *s_net_prov_hint;
static lv_obj_t *s_net_prov_btn;
static lv_obj_t *s_net_page;

static void net_provision_click_cb(lv_event_t *event)
{
    (void)event;
    net_wifi_start_provisioning();
}

/* 每 500ms 由 ui_app 定时器调用（首页刷新同源） */
void ui_page_settings_refresh(void)
{
    if (!s_net_page) return;
    const app_state_t *state = app_state_get();

    if (net_wifi_is_provisioning())
    {
        lv_label_set_text(s_net_wifi_value, UI_STR_SETTINGS_NET_PROVISIONING);
        ui_theme_apply_status_text(s_net_wifi_value, 2);
        if (s_net_prov_hint)
        {
            lv_label_set_text(s_net_prov_hint, UI_STR_SETTINGS_NET_AP_HINT);
            lv_obj_set_style_text_color(s_net_prov_hint, UI_COLOR_WARN, 0);
        }
        if (s_net_prov_btn) lv_obj_add_flag(s_net_prov_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(s_net_wifi_value,
                          state->conn.wifi == APP_CONN_OK ? UI_STR_STATE_OK : UI_STR_STATE_DISC);
        ui_theme_apply_status_text(s_net_wifi_value,
                                   state->conn.wifi == APP_CONN_OK ? 1 : 0);
        if (s_net_prov_hint)
        {
            lv_label_set_text(s_net_prov_hint, UI_STR_SETTINGS_NET_AP_NAME);
            lv_obj_set_style_text_color(s_net_prov_hint, UI_COLOR_TEXT_DIM, 0);
        }
        if (s_net_prov_btn) lv_obj_clear_flag(s_net_prov_btn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_net_tcp_value,
                      state->live2d.connected ? UI_STR_STATE_OK : UI_STR_STATE_DISC);
    ui_theme_apply_status_text(s_net_tcp_value, state->live2d.connected ? 1 : 0);
}

lv_obj_t *ui_page_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    s_net_page = page;
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

    lv_obj_t *display_card = card_create(page, UI_MARGIN, 62, 214, 176);
    lv_obj_t *display_title = lv_label_create(display_card);
    lv_label_set_text(display_title, UI_STR_SETTINGS_DISPLAY);
    lv_obj_set_pos(display_title, UI_GAP, UI_GAP);
    lv_obj_set_style_text_color(display_title, UI_COLOR_TEXT, 0);
    setting_row(display_card, UI_STR_SETTINGS_ROT, "顺时针 90°", 48, 1);
    setting_row(display_card, UI_STR_SETTINGS_TOUCH, UI_STR_STATE_OK, 82, 1);
    setting_row(display_card, UI_STR_SETTINGS_BL, UI_STR_STATE_ON, 116, 1);
    setting_row(display_card, UI_STR_SETTINGS_FW, "LVGL 9", 150, 1);

    lv_obj_t *device_card = card_create(page, 242, 62, 222, 176);
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

    /* 网络与配网卡片（整行） */
    lv_obj_t *net_card = card_create(page, UI_MARGIN, 250, 448, 42);
    lv_obj_t *net_title = lv_label_create(net_card);
    lv_label_set_text(net_title, UI_STR_SETTINGS_NET);
    lv_obj_set_pos(net_title, UI_GAP, 6);
    lv_obj_set_style_text_color(net_title, UI_COLOR_TEXT, 0);
    lv_obj_set_width(net_title, 90);

    s_net_wifi_value = lv_label_create(net_card);
    lv_label_set_text(s_net_wifi_value, UI_STR_STATE_DISC);
    lv_obj_set_pos(s_net_wifi_value, 110, 6);
    ui_theme_apply_status_text(s_net_wifi_value, 0);

    s_net_tcp_value = lv_label_create(net_card);
    lv_label_set_text(s_net_tcp_value, UI_STR_STATE_DISC);
    lv_obj_set_pos(s_net_tcp_value, 178, 6);
    ui_theme_apply_status_text(s_net_tcp_value, 0);

    s_net_prov_hint = lv_label_create(net_card);
    lv_label_set_text(s_net_prov_hint, UI_STR_SETTINGS_NET_AP_NAME);
    lv_obj_set_pos(s_net_prov_hint, UI_GAP, 24);
    lv_obj_set_width(s_net_prov_hint, 260);
    lv_label_set_long_mode(s_net_prov_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_net_prov_hint, UI_COLOR_TEXT_DIM, 0);

    s_net_prov_btn = lv_button_create(net_card);
    lv_obj_set_size(s_net_prov_btn, 100, 26);
    lv_obj_align(s_net_prov_btn, LV_ALIGN_BOTTOM_RIGHT, -UI_GAP, -7);
    ui_theme_apply_button(s_net_prov_btn, false);
    lv_obj_t *prov_label = lv_label_create(s_net_prov_btn);
    lv_label_set_text(prov_label, UI_STR_SETTINGS_NET_PROVISION);
    lv_obj_center(prov_label);
    lv_obj_add_event_cb(s_net_prov_btn, net_provision_click_cb, LV_EVENT_CLICKED, NULL);

    ui_page_settings_refresh();
    return page;
}
