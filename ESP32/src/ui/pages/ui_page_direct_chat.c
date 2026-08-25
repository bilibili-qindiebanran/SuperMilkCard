/**
 * @file ui_page_direct_chat.c
 * @brief 独立角色对话页实现
 *
 * 数据流（跨任务，全部经加锁暂存区，LVGL 只在刷新回调里访问）：
 *   audio_voice 任务(STT) → on_stt_text → 暂存用户文本 + llm_role_start_chat
 *   llm_role 任务         → llm_done_cb  → 暂存回复/错误
 *   LVGL 刷新            → 消费暂存区 → 重建气泡列表/状态行
 *
 * 历史：仅 UI 侧内存保留最近 DIRECT_UI_HIST_MAX 条；LLM 上下文由 llm_role 维护。
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#include "../../audio_voice.h"
#include "../../llm_role.h"
#include "../../network/net_wifi.h"
#include "../app_state.h"
#include "../ui_pages.h"
#include "../ui_strings.h"
#include "../ui_theme.h"
#include "ui_page_direct_chat.h"

#define TAG "direct_chat"

#define DIRECT_UI_HIST_MAX 12  /* 页面内气泡条数上限 */
#define DIRECT_MSG_MAX 512     /* 单条气泡文本上限（字节） */
#define DIRECT_AREA_TOP 52
#define DIRECT_AREA_BOTTOM 262
#define DIRECT_BUBBLE_W 340

typedef struct {
    bool is_user;
    char text[DIRECT_MSG_MAX];
} direct_msg_t;

static lv_obj_t *s_page;
static lv_obj_t *s_history;      /* 可滚动容器 */
static lv_obj_t *s_status_label; /* 状态/错误行 */
static lv_obj_t *s_record_btn;
static lv_obj_t *s_record_label;
static lv_obj_t *s_clear_btn;
static lv_obj_t *s_net_dot;      /* Wi-Fi/LLM 状态点（顶部） */

/* 跨任务暂存区（audio_voice / llm_role 任务写，LVGL 刷新读） */
static SemaphoreHandle_t s_lock;
static volatile bool s_has_user_text;
static char s_pending_user[DIRECT_MSG_MAX];
static volatile bool s_has_reply;
static char s_pending_reply[DIRECT_MSG_MAX];
static volatile bool s_has_status;
static char s_pending_status[96];

/* UI 侧对话历史 */
static direct_msg_t s_msgs[DIRECT_UI_HIST_MAX];
static int s_msg_count;

static bool lock_take(void)
{
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void lock_give(void)
{
    xSemaphoreGive(s_lock);
}

/* 追加一条 UI 气泡（挤掉最早一条） */
static void ui_msg_add(bool is_user, const char *text)
{
    if (!text || !text[0]) return;
    if (s_msg_count >= DIRECT_UI_HIST_MAX)
    {
        memmove(&s_msgs[0], &s_msgs[1], (size_t)(s_msg_count - 1) * sizeof(direct_msg_t));
        s_msg_count--;
    }
    direct_msg_t *m = &s_msgs[s_msg_count];
    m->is_user = is_user;
    snprintf(m->text, sizeof(m->text), "%s", text);
    s_msg_count++;
}

/* 状态点颜色：绿=可用，橙=忙碌，灰=离线 */
static void set_net_dot(int status)
{
    if (!s_net_dot) return;
    lv_obj_set_style_bg_color(s_net_dot,
                              status == 1 ? UI_COLOR_SUCCESS
                                          : (status == 2 ? UI_COLOR_WARN : UI_COLOR_OFFLINE),
                              LV_PART_MAIN);
}

/* 更新 Wi-Fi / LLM 状态点 */
static void refresh_net_dot(void)
{
    if (!net_wifi_is_connected()) { set_net_dot(0); return; }
    if (llm_role_is_busy()) { set_net_dot(2); return; }
    if (llm_role_is_ready()) { set_net_dot(1); return; }
    set_net_dot(0); /* Wi-Fi 在、LLM 未配置 */
}

/* 状态行文案（通用映射；LLM 错误 detail 优先） */
static void set_status_text(const char *text, int status)
{
    if (!s_status_label) return;
    lv_label_set_text(s_status_label, text ? text : "");
    ui_theme_apply_status_text(s_status_label, status); /* 0=off 1=ok 2=warn */
}

/* 重建气泡列表（最多 12 条，自动滚动到底部） */
static void rebuild_history(void)
{
    if (!s_history) return;
    lv_obj_clean(s_history);

    if (s_msg_count == 0)
    {
        lv_obj_t *hint = lv_label_create(s_history);
        lv_label_set_text(hint, UI_STR_DIRECT_EMPTY_HINT);
        lv_obj_set_pos(hint, UI_MARGIN, UI_MARGIN);
        lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(hint, UI_FONT_DEFAULT, 0);
        return;
    }

    lv_coord_t y = UI_MARGIN;
    for (int i = 0; i < s_msg_count; i++)
    {
        const direct_msg_t *m = &s_msgs[i];

        lv_obj_t *who = lv_label_create(s_history);
        lv_label_set_text(who, m->is_user ? UI_STR_DIRECT_ME : UI_STR_DIRECT_ROLE);
        lv_obj_set_pos(who, m->is_user ? UI_SCREEN_W - UI_MARGIN - 100 : UI_MARGIN, y);
        lv_obj_set_style_text_color(who, m->is_user ? UI_COLOR_PRIMARY_DARK : UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(who, UI_FONT_DEFAULT, 0);

        lv_obj_t *bubble = lv_label_create(s_history);
        lv_label_set_text(bubble, m->text);
        lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(bubble, DIRECT_BUBBLE_W);
        lv_obj_set_pos(bubble, m->is_user ? UI_SCREEN_W - UI_MARGIN - DIRECT_BUBBLE_W : UI_MARGIN,
                       y + 18);
        lv_obj_set_style_bg_color(bubble, m->is_user ? UI_COLOR_PRIMARY_SOFT : UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(bubble, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(bubble, UI_FONT_DEFAULT, 0);
        lv_obj_set_style_radius(bubble, UI_RADIUS_SMALL, 0);
        lv_obj_set_style_pad_all(bubble, 6, 0);
        lv_obj_set_style_pad_left(bubble, 10, 0);
        lv_obj_set_style_pad_right(bubble, 10, 0);
        lv_obj_set_style_border_width(bubble, 1, 0);
        lv_obj_set_style_border_color(bubble, UI_COLOR_BORDER, 0);
        /* 「我」的气泡靠右，左侧留出与名称「我」对齐的空位 */
        if (m->is_user) lv_obj_set_style_pad_left(bubble, 46, 0);

        y += 44; /* 名称行 + 气泡行；超长文本后续可再调整间距 */
    }
    lv_obj_scroll_to_y(s_history, LV_COORD_MAX, LV_ANIM_OFF);
    lv_obj_update_layout(s_history);
    lv_obj_scroll_to_y(s_history, LV_COORD_MAX, LV_ANIM_OFF);
}

/* ------------------------------------------------------------------ */
/* 跨任务回调（禁止操作 LVGL）                                         */
/* ------------------------------------------------------------------ */

static void llm_done_cb(llm_role_result_t result, const char *reply, const char *detail,
                        void *user_ctx);

/* STT 最终文本 → 暂存用户气泡 + 发起 LLM 请求 */
static void on_stt_text(const char *text, void *ctx)
{
    (void)ctx;
    if (!text || !text[0]) return;

    if (lock_take())
    {
        snprintf(s_pending_user, sizeof(s_pending_user), "%s", text);
        s_has_user_text = true;
        lock_give();
    }

    esp_err_t err = llm_role_start_chat(text, llm_done_cb, NULL);
    if (err != ESP_OK)
    {
        if (lock_take())
        {
            if (err == (esp_err_t)LLM_ROLE_ERR_WIFI)
                snprintf(s_pending_status, sizeof(s_pending_status), "%s", "未连接 Wi-Fi");
            else if (err == (esp_err_t)LLM_ROLE_ERR_CONFIG)
                snprintf(s_pending_status, sizeof(s_pending_status), "%s", UI_STR_DIRECT_STATUS_ERR_CONFIG);
            else if (err == (esp_err_t)LLM_ROLE_ERR_BUSY)
                snprintf(s_pending_status, sizeof(s_pending_status), "%s", "请求进行中");
            else
                snprintf(s_pending_status, sizeof(s_pending_status), "%s", UI_STR_DIRECT_STATUS_ERR_MEM);
            s_has_status = true;
            lock_give();
        }
    }
}

/* LLM 完成 → 暂存回复/错误 */
static void llm_done_cb(llm_role_result_t result, const char *reply, const char *detail,
                        void *user_ctx)
{
    (void)user_ctx;
    if (!lock_take()) return;
    if (result == LLM_ROLE_OK && reply && reply[0])
    {
        snprintf(s_pending_reply, sizeof(s_pending_reply), "%s", reply);
        s_has_reply = true;
    }
    else
    {
        const char *d = (detail && detail[0]) ? detail : "请求失败，可重试";
        snprintf(s_pending_status, sizeof(s_pending_status), "%s", d);
        s_has_status = true;
    }
    lock_give();
}

/* ------------------------------------------------------------------ */
/* 交互回调                                                           */
/* ------------------------------------------------------------------ */

static void record_press_cb(lv_event_t *event)
{
    (void)event;
    if (llm_role_is_busy())
    {
        set_status_text("思考中…请稍候", 2);
        return;
    }
    esp_err_t err = audio_voice_start();
    if (err == ESP_OK)
    {
        lv_label_set_text(s_record_label, UI_STR_DIRECT_RECORDING);
        set_status_text(UI_STR_DIRECT_STATUS_STT, 2);
        app_state_publish_session(APP_MODE_DIRECT_API, APP_SESSION_DIRECT_LISTENING, NULL);
    }
    else if (err == ESP_ERR_NOT_FOUND)
    {
        set_status_text("未连接 Wi-Fi", 0);
    }
    else
    {
        set_status_text(UI_STR_DIRECT_STATUS_ERR_CONFIG, 0);
    }
}

static void record_release_cb(lv_event_t *event)
{
    (void)event;
    if (!audio_voice_is_recording()) return;
    audio_voice_stop();
    lv_label_set_text(s_record_label, UI_STR_DIRECT_RECORD);
    app_state_publish_session(APP_MODE_DIRECT_API, APP_SESSION_DIRECT_STT, NULL);
}

static void clear_click_cb(lv_event_t *event)
{
    (void)event;
    if (llm_role_is_busy())
    {
        set_status_text("思考中…清空将在结束后生效", 2);
    }
    else if (lock_take())
    {
        s_msg_count = 0;
        lock_give();
        llm_role_clear_history();
        rebuild_history();
        set_status_text(UI_STR_DIRECT_STATUS_IDLE, 1);
    }
}

/* 返回桌面（不进入 Windows Live2D 页面） */
static void back_click_cb(lv_event_t *event)
{
    (void)event;
    ui_pages_return_home();
}

/* ------------------------------------------------------------------ */
/* 刷新（LVGL 定时器调用）                                            */
/* ------------------------------------------------------------------ */

void ui_page_direct_chat_refresh(void)
{
    if (!s_page) return;
    if (!ui_pages_direct_chat_active()) return;

    /* 消费暂存区（audio_voice/llm_role 任务写入） */
    bool consume_user = false, consume_reply = false, consume_status = false;
    if (lock_take())
    {
        if (s_has_user_text) { consume_user = true; s_has_user_text = false; }
        if (s_has_reply) { consume_reply = true; s_has_reply = false; }
        if (s_has_status) { consume_status = true; s_has_status = false; }
        lock_give();
    }

    if (consume_user)
    {
        const char *t = s_pending_user;
        if (t[0]) ui_msg_add(true, t);
        else set_status_text(UI_STR_DIRECT_STATUS_EMPTY, 2);
        rebuild_history();
    }
    if (consume_status)
    {
        set_status_text(s_pending_status, 0);
    }
    if (consume_reply)
    {
        ui_msg_add(false, s_pending_reply);
        rebuild_history();
        set_status_text(UI_STR_DIRECT_STATUS_IDLE, 1);
    }

    /* LLM 进行中：禁用录音按钮并显示思考状态 */
    bool busy = llm_role_is_busy();
    if (busy)
    {
        lv_obj_add_state(s_record_btn, LV_STATE_DISABLED);
        set_status_text(UI_STR_DIRECT_STATUS_LLM, 2);
    }
    else
    {
        lv_obj_clear_state(s_record_btn, LV_STATE_DISABLED);
        if (!s_has_status && !s_has_reply)
        {
            /* 空闲且无新状态时，显示默认提示 */
            set_status_text(UI_STR_DIRECT_STATUS_IDLE, 1);
        }
    }

    /* Wi-Fi/LLM 状态点 */
    refresh_net_dot();
}

/* ------------------------------------------------------------------ */
/* show / hide                                                         */
/* ------------------------------------------------------------------ */

void ui_page_direct_chat_show(void)
{
    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateMutex();
    }
    if (lock_take())
    {
        s_has_user_text = false;
        s_has_reply = false;
        s_has_status = false;
        s_pending_user[0] = '\0';
        s_pending_reply[0] = '\0';
        s_pending_status[0] = '\0';
        lock_give();
    }

    /* 注册 STT 文本去向（独立角色模式） */
    audio_voice_set_text_sink(on_stt_text, NULL);

    app_state_publish_session(APP_MODE_DIRECT_API, APP_SESSION_DIRECT_IDLE, NULL);
    set_status_text(UI_STR_DIRECT_STATUS_IDLE, 1);
    lv_label_set_text(s_record_label, UI_STR_DIRECT_RECORD);
    rebuild_history();
}

void ui_page_direct_chat_hide(void)
{
    /* 取消未完成的 LLM 请求；忽略后续回调（s_page 隐藏后 refresh 不再消费） */
    llm_role_cancel();
    if (audio_voice_is_recording()) audio_voice_stop();
    audio_voice_set_text_sink(NULL, NULL);
    app_state_publish_session(APP_MODE_NONE, APP_SESSION_IDLE, NULL);
}

/* ------------------------------------------------------------------ */
/* 创建                                                               */
/* ------------------------------------------------------------------ */

lv_obj_t *ui_page_direct_chat_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
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
    lv_obj_add_event_cb(back_btn, back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, UI_STR_DIRECT_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    ui_theme_apply_title(title);

    /* 右上角状态点（Wi-Fi/LLM） */
    s_net_dot = lv_obj_create(bar);
    lv_obj_remove_style_all(s_net_dot);
    lv_obj_set_size(s_net_dot, 12, 12);
    lv_obj_align(s_net_dot, LV_ALIGN_TOP_RIGHT, -UI_MARGIN, 16);
    lv_obj_set_style_bg_color(s_net_dot, UI_COLOR_OFFLINE, 0);
    lv_obj_set_style_bg_opa(s_net_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_net_dot, LV_RADIUS_CIRCLE, 0);

    /* 对话历史（可滚动） */
    s_history = lv_obj_create(page);
    lv_obj_set_pos(s_history, 0, DIRECT_AREA_TOP);
    lv_obj_set_size(s_history, UI_SCREEN_W, DIRECT_AREA_BOTTOM - DIRECT_AREA_TOP);
    lv_obj_set_style_bg_opa(s_history, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_history, 0, 0);
    lv_obj_set_style_pad_all(s_history, 0, 0);
    lv_obj_add_flag(s_history, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_history, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_history, LV_SCROLLBAR_MODE_AUTO);

    /* 状态/错误行 */
    s_status_label = lv_label_create(page);
    lv_label_set_text(s_status_label, UI_STR_DIRECT_STATUS_IDLE);
    lv_obj_set_pos(s_status_label, UI_MARGIN, DIRECT_AREA_BOTTOM + 4);
    lv_obj_set_width(s_status_label, UI_SCREEN_W - UI_MARGIN * 2 - 80);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    ui_theme_apply_status_text(s_status_label, 1);

    /* 清空记忆按钮 */
    s_clear_btn = lv_button_create(page);
    lv_obj_set_size(s_clear_btn, 84, 32);
    lv_obj_set_pos(s_clear_btn, UI_MARGIN, DIRECT_AREA_BOTTOM + 26);
    ui_theme_apply_button(s_clear_btn, false);
    lv_obj_t *clear_label = lv_label_create(s_clear_btn);
    lv_label_set_text(clear_label, UI_STR_DIRECT_CLEAR);
    lv_obj_center(clear_label);
    lv_obj_add_event_cb(s_clear_btn, clear_click_cb, LV_EVENT_CLICKED, NULL);

    /* 录音按钮（按住录音，松开结束） */
    s_record_btn = lv_button_create(page);
    lv_obj_set_size(s_record_btn, 200, 44);
    lv_obj_set_pos(s_record_btn, UI_SCREEN_W - UI_MARGIN - 200, DIRECT_AREA_BOTTOM + 20);
    ui_theme_apply_button(s_record_btn, true);
    s_record_label = lv_label_create(s_record_btn);
    lv_label_set_text(s_record_label, UI_STR_DIRECT_RECORD);
    lv_obj_center(s_record_label);
    lv_obj_add_event_cb(s_record_btn, record_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_record_btn, record_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_record_btn, record_release_cb, LV_EVENT_CLICKED, NULL);

    rebuild_history();
    return page;
}
