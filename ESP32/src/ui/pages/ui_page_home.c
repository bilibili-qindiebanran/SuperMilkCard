/**
 * @file ui_page_home.c
 * @brief Fluent 2 首页：状态总览 + 卡片式快捷入口
 */

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "../app_state.h"
#include "../ui_pages.h"
#include "../ui_port_display.h"
#include "../ui_strings.h"
#include "../ui_theme.h"
#include "ui_page_home.h"

static lv_obj_t *s_clock_value;
static lv_obj_t *s_clock_status;
static lv_obj_t *s_power_value;
static lv_obj_t *s_power_detail;
static lv_obj_t *s_audio_value;
static lv_obj_t *s_audio_bar;
static lv_obj_t *s_conn_value;
static lv_obj_t *s_conn_detail;
static lv_obj_t *s_home_page;
static lv_obj_t *s_fps_label;
static int s_last_audio_bar_value = -1;
static uint32_t s_last_fps = UINT32_MAX;
static int s_clock_status_code = -1;
static int s_power_status_code = -1;
static int s_conn_status_code = -1;

static void label_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL) return;
    const char *current = lv_label_get_text(label);
    if (current == NULL || strcmp(current, text) != 0)
    {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *card_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, lv_coord_t h, bool soft)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    if (soft) ui_theme_apply_soft_card(card);
    else ui_theme_apply_card(card);
    return card;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text, lv_coord_t x,
                              lv_coord_t y, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, UI_FONT_DEFAULT, 0); /* 中文字体 */
    return label;
}

static const char *connection_text(app_conn_state_t state)
{
    switch (state)
    {
    case APP_CONN_OK: return UI_STR_STATE_OK;
    case APP_CONN_DISCONNECTED: return UI_STR_STATE_DISC;
    default: return UI_STR_STATE_OFF;
    }
}

static void update_connection_state(const app_state_t *state)
{
    char detail[64];
    snprintf(detail, sizeof(detail), "%s %s   %s %s",
             UI_STR_CONN_WIFI, connection_text(state->conn.wifi),
             UI_STR_CONN_UART, connection_text(state->conn.uart));
    label_set_text_if_changed(s_conn_value, connection_text(state->conn.win));
    label_set_text_if_changed(s_conn_detail, detail);
    int conn_status = state->conn.win == APP_CONN_OK ? 1 : 0;
    if (conn_status != s_conn_status_code)
    {
        ui_theme_apply_status_text(s_conn_value, conn_status);
        s_conn_status_code = conn_status;
    }
}

void ui_page_home_refresh(void)
{
    if (s_home_page == NULL) return;

    const app_state_t *state = app_state_get();
    char text[48] = {0};

    if (state->clock.synced)
    {
        snprintf(text, sizeof(text), "%02u:%02u", state->clock.hour, state->clock.min);
        label_set_text_if_changed(s_clock_status, UI_STR_TIME_SYNCED);
        if (s_clock_status_code != 1)
        {
            ui_theme_apply_status_text(s_clock_status, 1);
            s_clock_status_code = 1;
        }
    }
    else
    {
        label_set_text_if_changed(s_clock_status, UI_STR_TIME_WAIT);
        if (s_clock_status_code != 2)
        {
            ui_theme_apply_status_text(s_clock_status, 2);
            s_clock_status_code = 2;
        }
    }
    label_set_text_if_changed(s_clock_value, text[0] ? text : UI_STR_TIME_PLACE);

    if (state->power.charging) label_set_text_if_changed(s_power_value, UI_STR_POWER_CHG);
    else if (state->power.charge_full) label_set_text_if_changed(s_power_value, UI_STR_POWER_FULL);
    else label_set_text_if_changed(s_power_value, UI_STR_POWER_IDLE);
    label_set_text_if_changed(s_power_detail, state->power.light_load ? UI_STR_POWER_LIGHT : UI_STR_POWER_STABLE);
    int power_status = state->power.charging || state->power.charge_full ? 1 : 0;
    if (power_status != s_power_status_code)
    {
        ui_theme_apply_status_text(s_power_value, power_status);
        s_power_status_code = power_status;
    }

    snprintf(text, sizeof(text), "%+.1f dB", (double)state->audio.rms_db);
    label_set_text_if_changed(s_audio_value, text);
    int bar_value = (int)((state->audio.rms_db + 60.0f) * 100.0f / 60.0f);
    if (bar_value < 0) bar_value = 0;
    if (bar_value > 100) bar_value = 100;
    if (bar_value != s_last_audio_bar_value)
    {
        lv_bar_set_value(s_audio_bar, bar_value, LV_ANIM_OFF);
        s_last_audio_bar_value = bar_value;
    }

    update_connection_state(state);

    if (s_fps_label != NULL)
    {
        uint32_t fps = ui_port_display_get_fps();
        if (fps != s_last_fps)
        {
            lv_label_set_text_fmt(s_fps_label, "FPS %lu", (unsigned long)fps);
            s_last_fps = fps;
        }
    }
}

void ui_page_home_refresh_fps(void)
{
    if (s_fps_label == NULL) return;

    uint32_t fps = ui_port_display_get_fps();
    if (fps != s_last_fps)
    {
        lv_label_set_text_fmt(s_fps_label, "FPS %lu", (unsigned long)fps);
        s_last_fps = fps;
    }
}

lv_obj_t *ui_page_home_create(lv_obj_t *parent)
{
    s_home_page = lv_obj_create(parent);
    lv_obj_set_size(s_home_page, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_style_bg_opa(s_home_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_home_page, 0, 0);
    lv_obj_set_style_border_width(s_home_page, 0, 0);
    lv_obj_clear_flag(s_home_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = label_create(s_home_page, UI_STR_STATUS_BAR, UI_MARGIN, 6, UI_COLOR_TEXT);
    ui_theme_apply_title(title);
    lv_obj_t *subtitle = label_create(s_home_page, UI_STR_HOME_SUBTITLE, UI_MARGIN, 28, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(subtitle, 330);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);

    lv_obj_t *ready = lv_label_create(s_home_page);
    lv_label_set_text(ready, UI_STR_READY);
    lv_obj_align(ready, LV_ALIGN_TOP_RIGHT, -UI_MARGIN, 8);
    ui_theme_apply_status_text(ready, 1);

    s_fps_label = lv_label_create(s_home_page);
    lv_label_set_text(s_fps_label, "FPS --");
    lv_obj_align(s_fps_label, LV_ALIGN_TOP_RIGHT, -UI_MARGIN, 32);
    lv_obj_set_style_text_color(s_fps_label, UI_COLOR_TEXT_DIM, 0);

    lv_obj_t *clock_card = card_create(s_home_page, UI_MARGIN, 50, 205, 130, false);
    label_create(clock_card, UI_STR_TIME_TITLE, UI_GAP, UI_GAP, UI_COLOR_TEXT_DIM);
    s_clock_value = label_create(clock_card, UI_STR_TIME_PLACE, UI_GAP, 42, UI_COLOR_TEXT);
    lv_obj_set_style_text_font(s_clock_value, UI_FONT_DEFAULT, 0);
    s_clock_status = label_create(clock_card, UI_STR_TIME_WAIT, UI_GAP, 95, UI_COLOR_WARN);
    label_create(clock_card, UI_STR_TIME_HINT, UI_GAP, 105, UI_COLOR_TEXT_DIM);

    lv_obj_t *power_card = card_create(s_home_page, 229, 50, 241, 60, true);
    label_create(power_card, UI_STR_POWER_TITLE, UI_GAP, 10, UI_COLOR_TEXT_DIM);
    s_power_value = label_create(power_card, UI_STR_POWER_IDLE, 95, 10, UI_COLOR_TEXT);
    s_power_detail = label_create(power_card, UI_STR_POWER_STABLE, UI_GAP, 36, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(s_power_detail, 205);
    lv_label_set_long_mode(s_power_detail, LV_LABEL_LONG_DOT);

    lv_obj_t *audio_card = card_create(s_home_page, 229, 116, 241, 60, true);
    label_create(audio_card, UI_STR_AUDIO_TITLE, UI_GAP, 10, UI_COLOR_TEXT_DIM);
    s_audio_value = label_create(audio_card, "-- dB", 95, 10, UI_COLOR_TEXT);
    s_audio_bar = lv_bar_create(audio_card);
    lv_obj_set_size(s_audio_bar, 195, 8);
    lv_obj_set_pos(s_audio_bar, UI_GAP, 38);
    lv_obj_set_style_bg_color(s_audio_bar, UI_COLOR_PRIMARY_SOFT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_audio_bar, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_audio_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_audio_bar, 4, LV_PART_INDICATOR);
    lv_bar_set_range(s_audio_bar, 0, 100);

    lv_obj_t *conn_card = card_create(s_home_page, UI_MARGIN, 184, 292, 56, true);
    label_create(conn_card, UI_STR_CONN_TITLE, UI_GAP, 9, UI_COLOR_TEXT_DIM);
    s_conn_value = label_create(conn_card, UI_STR_STATE_OFF, 80, 9, UI_COLOR_OFFLINE);
    s_conn_detail = label_create(conn_card, "Wi-Fi 未接入   串口 未接入", UI_GAP, 34, UI_COLOR_TEXT_DIM);
    lv_obj_set_width(s_conn_detail, 262);
    lv_label_set_long_mode(s_conn_detail, LV_LABEL_LONG_DOT);

    lv_obj_t *settings = lv_button_create(s_home_page);
    lv_obj_set_size(settings, 160, 56);
    lv_obj_set_pos(settings, 310, 184);
    ui_theme_apply_button(settings, true);
    lv_obj_t *settings_label = lv_label_create(settings);
    lv_label_set_text(settings_label, UI_STR_OPEN_SETTINGS);
    lv_obj_center(settings_label);
    lv_obj_add_event_cb(settings, ui_page_home_settings_event, LV_EVENT_CLICKED, NULL);

    ui_page_home_refresh();
    return s_home_page;
}

void ui_page_home_settings_event(lv_event_t *event)
{
    (void)event;
    ui_pages_show(UI_PAGE_SETTINGS);
}
