/**
 * @file ui_page_voice_mode.c
 * @brief 语音互动模式选择页实现
 *
 * 双模式卡片 + 焦点高亮 + 预检状态机。实体按键与触摸同一流程。
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "../../network/net_tcp.h"
#include "../../network/net_wifi.h"
#include "../app_state.h"
#include "../ui_pages.h"
#include "../ui_strings.h"
#include "../ui_theme.h"
#include "ui_page_voice_mode.h"

#define VM_CARD_W 300
#define VM_CARD_H 104
#define VM_CARD_X (UI_SCREEN_W / 2 - VM_CARD_W / 2)
#define VM_CARD1_Y 58
#define VM_CARD2_Y (VM_CARD1_Y + VM_CARD_H + 10)
#define VM_CHECK_TIMEOUT_MS 5000 /* Live2D 连接预检超时 */

static lv_obj_t *s_page;
static lv_obj_t *s_cards[2];
static lv_obj_t *s_enter_btns[2];
static lv_obj_t *s_status_label;   /* 预检结果/错误 */
static lv_obj_t *s_status_detail;
static lv_obj_t *s_retry_btn;
static lv_obj_t *s_switch_btn;
static uint32_t s_check_start_ms;   /* 预检起始 tick */
static bool s_checking;

/* 预检是否在进行中（供 refresh 轮询） */
static bool vm_checking(void)
{
    return s_checking;
}

/* 应用卡片焦点样式 */
static void apply_focus(void)
{
    int focus = ui_page_voice_mode_focus();
    for (int i = 0; i < 2; i++)
    {
        bool selected = (i == focus);
        lv_obj_set_style_bg_color(s_cards[i],
                                  selected ? UI_COLOR_PRIMARY_SOFT : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_border_width(s_cards[i], selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_cards[i],
                                      selected ? UI_COLOR_PRIMARY : UI_COLOR_BORDER, 0);
    }
}

/* 模式卡片创建（idx=0 Live2D 联动，idx=1 独立角色） */
static lv_obj_t *mode_card_create(lv_obj_t *parent, int idx, const char *title,
                                  const char *desc)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, VM_CARD_X, idx == 0 ? VM_CARD1_Y : VM_CARD2_Y);
    lv_obj_set_size(card, VM_CARD_W, VM_CARD_H);
    ui_theme_apply_card(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(card, UI_GAP, 0);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, UI_GAP, 12);
    lv_obj_set_style_text_color(title_label, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title_label, UI_FONT_DEFAULT, 0);

    lv_obj_t *desc_label = lv_label_create(card);
    lv_label_set_text(desc_label, desc);
    lv_obj_set_pos(desc_label, UI_GAP, 44);
    lv_obj_set_width(desc_label, VM_CARD_W - UI_GAP * 3);
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(desc_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(desc_label, UI_FONT_DEFAULT, 0);

    /* 进入按钮（右下角） */
    lv_obj_t *btn = lv_button_create(card);
    lv_obj_set_size(btn, 88, 32);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -UI_GAP, -UI_GAP);
    ui_theme_apply_button(btn, true);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, UI_STR_VOICE_ENTER);
    lv_obj_center(btn_label);

    s_cards[idx] = card;
    s_enter_btns[idx] = btn;
    return card;
}

/* 卡片点击 → 切换焦点 */
static void card_click_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    for (int i = 0; i < 2; i++)
    {
        if (s_cards[i] == target)
        {
            app_state_publish_session_focus(i);
            apply_focus();
            return;
        }
    }
}

/* 进入按钮点击 → 执行当前模式预检 */
static void enter_click_cb(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_current_target(event);
    for (int i = 0; i < 2; i++)
    {
        if (s_enter_btns[i] == target)
        {
            app_state_publish_session_focus(i);
            apply_focus();
            ui_page_voice_mode_handle_key(1); /* 复用 OK 短按语义 */
            return;
        }
    }
}

/* 重试按钮 */
static void retry_click_cb(lv_event_t *event)
{
    (void)event;
    ui_page_voice_mode_handle_key(1); /* 重新预检当前焦点模式 */
}

/* 切换到独立角色 */
static void switch_direct_cb(lv_event_t *event)
{
    (void)event;
    app_state_publish_session_focus(1);
    apply_focus();
    ui_page_voice_mode_handle_key(1);
}

/* 返回桌面 */
void ui_page_voice_mode_back_cb(lv_event_t *event)
{
    (void)event;
    ui_pages_return_home();
}

/* ------------------------------------------------------------------ */
/* 预检状态机                                                         */
/* ------------------------------------------------------------------ */

/* 执行当前焦点模式的预检并进入 */
static void vm_start_check(void)
{
    int focus = ui_page_voice_mode_focus();
    lv_obj_clear_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_switch_btn, LV_OBJ_FLAG_HIDDEN);

    if (focus == 0)
    {
        app_state_publish_session(APP_MODE_LIVE2D_LINK, APP_SESSION_LINK_CHECKING,
                                  NULL);
        lv_label_set_text(s_status_label, UI_STR_VOICE_CHECKING);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_WARN, 0);
        lv_label_set_text(s_status_detail, "");
    }
    else
    {
        app_state_publish_session(APP_MODE_DIRECT_API, APP_SESSION_DIRECT_CHECKING,
                                  NULL);
        lv_label_set_text(s_status_label, UI_STR_VOICE_CHECKING);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_WARN, 0);
        lv_label_set_text(s_status_detail, "");
    }
    s_checking = true;
    s_check_start_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void vm_check_done(bool ok, const char *ok_text, const char *fail_text)
{
    s_checking = false;
    if (ok)
    {
        lv_label_set_text(s_status_label, ok_text);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_SUCCESS, 0);
        lv_obj_add_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_switch_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        app_state_publish_session_error(fail_text);
        lv_label_set_text(s_status_label, fail_text);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_WARN, 0);
        lv_obj_clear_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_switch_btn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_status_detail, app_state_get()->session.error);
    }
}

/* 周期刷新：预检轮询（Live2D 连接检查 ≤5s） */
void ui_page_voice_mode_refresh(void)
{
    if (!s_page) return;
    if (!ui_pages_voice_mode_active()) return;

    if (!s_checking) return;

    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    int focus = ui_page_voice_mode_focus();

    if (focus == 0)
    {
        /* Live2D 联动：检查 TCP 客户端是否已连接 */
        if (net_tcp_is_client_connected())
        {
            vm_check_done(true, UI_STR_VOICE_LINK_OK, NULL);
            app_state_publish_session(APP_MODE_LIVE2D_LINK, APP_SESSION_LINKED_IDLE, NULL);
            ui_pages_show_live2d();
        }
        else if (now - s_check_start_ms >= VM_CHECK_TIMEOUT_MS)
        {
            vm_check_done(false, NULL, UI_STR_VOICE_LINK_FAIL);
            app_state_publish_session(APP_MODE_LIVE2D_LINK, APP_SESSION_ERROR,
                                      UI_STR_VOICE_LINK_FAIL);
        }
    }
    else
    {
        /* 独立角色：首期仅检查 Wi-Fi 是否在线（API 配置检查留待独立模式实现） */
        if (net_wifi_is_connected())
        {
            vm_check_done(true, UI_STR_VOICE_DIRECT_OK, NULL);
            app_state_publish_session(APP_MODE_DIRECT_API, APP_SESSION_DIRECT_IDLE, NULL);
        }
        else if (now - s_check_start_ms >= VM_CHECK_TIMEOUT_MS)
        {
            vm_check_done(false, NULL, UI_STR_VOICE_DIRECT_FAIL);
            app_state_publish_session(APP_MODE_DIRECT_API, APP_SESSION_ERROR,
                                      UI_STR_VOICE_DIRECT_FAIL);
        }
    }
}

/* 按键分发（由 ui_pages 路由调用）
 * key_event: 0=短按返回 1=短按确认 2=长按返回 3=长按确认 */
void ui_page_voice_mode_handle_key(int key_event)
{
    switch (key_event)
    {
    case 0: /* 短按 BACK：切换焦点 */
        app_state_publish_session_focus(1 - ui_page_voice_mode_focus());
        apply_focus();
        break;
    case 1: /* 短按 OK：预检当前模式 */
        vm_start_check();
        break;
    case 2: /* 长按 BACK：返回桌面 */
        ui_pages_return_home();
        break;
    case 3: /* 长按 OK：无操作（保留） */
        break;
    default:
        break;
    }
}

int ui_page_voice_mode_focus(void)
{
    return app_state_get()->session.focus;
}

void ui_page_voice_mode_show(void)
{
    s_checking = false;
    app_state_publish_session(APP_MODE_NONE, APP_SESSION_IDLE, NULL);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_style_text_color(s_status_label, UI_COLOR_TEXT_DIM, 0);
    lv_label_set_text(s_status_detail, "");
    lv_obj_add_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_switch_btn, LV_OBJ_FLAG_HIDDEN);
    apply_focus();
}

/* ------------------------------------------------------------------ */
/* 创建                                                               */
/* ------------------------------------------------------------------ */

lv_obj_t *ui_page_voice_mode_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    s_page = page;

    /* 顶部栏：返回桌面 + 标题 */
    lv_obj_t *bar = lv_obj_create(page);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, UI_SCREEN_W, 48);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_button_create(bar);
    lv_obj_set_size(back_btn, 110, 36);
    lv_obj_set_pos(back_btn, UI_MARGIN, 6);
    ui_theme_apply_button(back_btn, false);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, UI_STR_LIVE2D_BACK);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, ui_page_voice_mode_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, UI_STR_VOICE_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    ui_theme_apply_title(title);

    /* 两个模式卡片 */
    lv_obj_t *card0 = mode_card_create(page, 0, UI_STR_VOICE_MODE_LINK,
                                       UI_STR_VOICE_MODE_LINK_DESC);
    lv_obj_t *card1 = mode_card_create(page, 1, UI_STR_VOICE_MODE_DIRECT,
                                       UI_STR_VOICE_MODE_DIRECT_DESC);
    (void)card0;
    (void)card1;

    lv_obj_add_event_cb(s_cards[0], card_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_cards[1], card_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_enter_btns[0], enter_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_enter_btns[1], enter_click_cb, LV_EVENT_CLICKED, NULL);

    /* 状态/错误区（卡片下方） */
    s_status_label = lv_label_create(page);
    lv_label_set_text(s_status_label, "");
    lv_obj_set_pos(s_status_label, UI_MARGIN, VM_CARD2_Y + VM_CARD_H + 6);
    lv_obj_set_style_text_color(s_status_label, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_status_label, UI_FONT_DEFAULT, 0);

    s_status_detail = lv_label_create(page);
    lv_label_set_text(s_status_detail, "");
    lv_obj_set_pos(s_status_detail, UI_MARGIN, VM_CARD2_Y + VM_CARD_H + 28);
    lv_obj_set_width(s_status_detail, 460);
    lv_label_set_long_mode(s_status_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_status_detail, UI_COLOR_TEXT_DIM, 0);

    /* 错误操作按钮（重试 / 切换到独立角色） */
    s_retry_btn = lv_button_create(page);
    lv_obj_set_size(s_retry_btn, 90, 30);
    lv_obj_set_pos(s_retry_btn, UI_MARGIN, VM_CARD2_Y + VM_CARD_H + 6);
    ui_theme_apply_button(s_retry_btn, false);
    lv_obj_t *retry_label = lv_label_create(s_retry_btn);
    lv_label_set_text(retry_label, UI_STR_VOICE_RETRY);
    lv_obj_center(retry_label);
    lv_obj_add_event_cb(s_retry_btn, retry_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_retry_btn, LV_OBJ_FLAG_HIDDEN);

    s_switch_btn = lv_button_create(page);
    lv_obj_set_size(s_switch_btn, 150, 30);
    lv_obj_set_pos(s_switch_btn, UI_MARGIN + 100, VM_CARD2_Y + VM_CARD_H + 6);
    ui_theme_apply_button(s_switch_btn, false);
    lv_obj_t *switch_label = lv_label_create(s_switch_btn);
    lv_label_set_text(switch_label, UI_STR_VOICE_SWITCH_DIRECT);
    lv_obj_center(switch_label);
    lv_obj_add_event_cb(s_switch_btn, switch_direct_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_switch_btn, LV_OBJ_FLAG_HIDDEN);

    /* 操作提示（底部） */
    lv_obj_t *hint = lv_label_create(page);
    lv_label_set_text(hint, UI_STR_VOICE_HINT);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, UI_FONT_DEFAULT, 0);

    return page;
}
