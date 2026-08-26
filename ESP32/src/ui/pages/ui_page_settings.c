/**
 * @file ui_page_settings.c
 * @brief Fluent 2 设置入口页
 */

#include <stdio.h>
#include <string.h>

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

/* 网络状态：Wi-Fi/TCP + 配网按钮 + 局域网 IP/配置网页 */
static lv_obj_t *s_net_wifi_value;
static lv_obj_t *s_net_tcp_value;
static lv_obj_t *s_net_prov_btn;
static lv_obj_t *s_net_lan_ip;
static lv_obj_t *s_net_portal;
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
        if (s_net_prov_btn) lv_obj_add_flag(s_net_prov_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(s_net_wifi_value,
                          state->conn.wifi == APP_CONN_OK ? UI_STR_STATE_OK : UI_STR_STATE_DISC);
        ui_theme_apply_status_text(s_net_wifi_value,
                                   state->conn.wifi == APP_CONN_OK ? 1 : 0);
        if (s_net_prov_btn) lv_obj_clear_flag(s_net_prov_btn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text(s_net_tcp_value,
                      state->live2d.connected ? UI_STR_STATE_OK : UI_STR_STATE_DISC);
    ui_theme_apply_status_text(s_net_tcp_value, state->live2d.connected ? 1 : 0);

    /* 局域网地址与配置网页 URL：仅在内容变化时刷新，避免 LVGL 高频重绘 */
    static char s_last_ip[48] = "";
    char ip[16] = "";
    bool has_ip = net_wifi_get_sta_ip(ip, sizeof(ip));
    if (!has_ip)
    {
        if (strcmp(s_last_ip, UI_STR_SETTINGS_NET_LAN_DISC) != 0)
        {
            lv_label_set_text(s_net_lan_ip, UI_STR_SETTINGS_NET_LAN_DISC);
            ui_theme_apply_status_text(s_net_lan_ip, 0);
            lv_label_set_text(s_net_portal, UI_STR_SETTINGS_NET_PORTAL_AP);
            snprintf(s_last_ip, sizeof(s_last_ip), "%s", UI_STR_SETTINGS_NET_LAN_DISC);
        }
    }
    else
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "http://%s/", ip);
        if (strcmp(s_last_ip, ip) != 0)
        {
            lv_label_set_text(s_net_lan_ip, ip);
            ui_theme_apply_status_text(s_net_lan_ip, 1);
            lv_label_set_text(s_net_portal, buf);
            snprintf(s_last_ip, sizeof(s_last_ip), "%s", ip);
        }
    }
}

lv_obj_t *ui_page_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    s_net_page = page;
    lv_obj_set_size(page, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(page, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_pad_bottom(page, 24, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, UI_STR_SETTINGS);
    lv_obj_set_pos(title, UI_MARGIN, 6);
    ui_theme_apply_title(title);

    lv_obj_t *subtitle = lv_label_create(page);
    lv_label_set_text(subtitle, UI_STR_SETTINGS_SUBTITLE);
    lv_obj_set_pos(subtitle, UI_MARGIN, 28);
    lv_obj_set_width(subtitle, 360);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, 0);

    const touch_info_t *info = touch_get_info();
    char device[48];
    snprintf(device, sizeof(device), "%s  @0x%02X",
             info && info->detected ? info->name : UI_STR_UNKNOWN,
             info ? info->i2c_addr : 0);

    lv_obj_t *display_card = card_create(page, UI_MARGIN, 50, 222, 176);
    lv_obj_t *display_title = lv_label_create(display_card);
    lv_label_set_text(display_title, UI_STR_SETTINGS_DISPLAY);
    lv_obj_set_pos(display_title, UI_GAP, UI_GAP);
    lv_obj_set_style_text_color(display_title, UI_COLOR_TEXT, 0);
    setting_row(display_card, UI_STR_SETTINGS_ROT, "顺时针 90°", 48, 1);
    setting_row(display_card, UI_STR_SETTINGS_TOUCH, UI_STR_STATE_OK, 82, 1);
    setting_row(display_card, UI_STR_SETTINGS_BL, UI_STR_STATE_ON, 116, 1);
    setting_row(display_card, UI_STR_SETTINGS_FW, "LVGL 9", 150, 1);

    lv_obj_t *device_card = card_create(page, 240, 50, 230, 176);
    lv_obj_t *device_title = lv_label_create(device_card);
    lv_label_set_text(device_title, UI_STR_SETTINGS_DEVICE);
    lv_obj_set_pos(device_title, UI_GAP, UI_GAP);
    lv_obj_set_style_text_color(device_title, UI_COLOR_TEXT, 0);

    lv_obj_t *device_label = lv_label_create(device_card);
    lv_label_set_text(device_label, device);
    lv_obj_set_pos(device_label, UI_GAP, 52);
    lv_obj_set_width(device_label, 198);
    lv_label_set_long_mode(device_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *touch_label = lv_label_create(device_card);
    lv_label_set_text_fmt(touch_label, "TP_INT %d · %s", touch_get_int_level(),
                          touch_is_interrupt_mode() ? "中断" : "轮询");
    lv_obj_set_pos(touch_label, UI_GAP, 104);
    lv_obj_set_style_text_color(touch_label, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *tip = lv_label_create(device_card);
    lv_label_set_text(tip, UI_STR_SETTINGS_SYNC_HINT);
    lv_obj_set_pos(tip, UI_GAP, 136);
    lv_obj_set_width(tip, 198);
    lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(tip, UI_COLOR_TEXT_DIM, 0);

    /* 网络与配网卡片（整行） */
    lv_obj_t *net_card = card_create(page, UI_MARGIN, 236, 460, 92);
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

    /* 局域网地址 + 配置网页（只读信息行，由 refresh 更新） */
    lv_obj_t *lan_title = lv_label_create(net_card);
    lv_label_set_text(lan_title, UI_STR_SETTINGS_NET_LAN_IP);
    lv_obj_set_pos(lan_title, UI_GAP, 34);
    lv_obj_set_style_text_color(lan_title, UI_COLOR_TEXT_DIM, 0);

    s_net_lan_ip = lv_label_create(net_card);
    lv_label_set_text(s_net_lan_ip, UI_STR_SETTINGS_NET_LAN_DISC);
    lv_obj_set_pos(s_net_lan_ip, 110, 34);
    lv_obj_set_width(s_net_lan_ip, 220);
    lv_label_set_long_mode(s_net_lan_ip, LV_LABEL_LONG_DOT);
    ui_theme_apply_status_text(s_net_lan_ip, 0);

    lv_obj_t *portal_title = lv_label_create(net_card);
    lv_label_set_text(portal_title, UI_STR_SETTINGS_NET_PORTAL);
    lv_obj_set_pos(portal_title, UI_GAP, 62);
    lv_obj_set_style_text_color(portal_title, UI_COLOR_TEXT_DIM, 0);

    s_net_portal = lv_label_create(net_card);
    lv_label_set_text(s_net_portal, UI_STR_SETTINGS_NET_PORTAL_AP);
    lv_obj_set_pos(s_net_portal, 110, 62);
    lv_obj_set_width(s_net_portal, 220);
    lv_label_set_long_mode(s_net_portal, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_net_portal, UI_COLOR_TEXT_DIM, 0);

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
