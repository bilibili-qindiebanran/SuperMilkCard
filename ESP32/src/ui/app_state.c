/**
 * @file app_state.c
 * @brief 应用状态模型实现
 *
 * 单写多读：驱动任务 publish（写），UI 任务读快照。
 * 用 portMUX 短临界区保护（更新频繁，开销低）。
 */

#include "app_state.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static app_state_t s_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* 读取更新后的时间戳（相对启动 ms） */
static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void app_state_publish_power(bool charging, bool full, bool light_load)
{
    portENTER_CRITICAL(&s_lock);
    s_state.power.charging = charging;
    s_state.power.charge_full = full;
    s_state.power.light_load = light_load;
    s_state.power.updated_ms = now_ms();
    portEXIT_CRITICAL(&s_lock);
}

void app_state_publish_audio(float rms_db, float peak_db)
{
    portENTER_CRITICAL(&s_lock);
    s_state.audio.rms_db = rms_db;
    s_state.audio.peak_db = peak_db;
    s_state.audio.updated_ms = now_ms();
    portEXIT_CRITICAL(&s_lock);
}

void app_state_publish_clock(bool synced, uint8_t hour, uint8_t min, const char *source)
{
    portENTER_CRITICAL(&s_lock);
    s_state.clock.synced = synced;
    s_state.clock.hour = hour;
    s_state.clock.min = min;
    s_state.clock.source = source;
    portEXIT_CRITICAL(&s_lock);
}

void app_state_publish_conn(app_conn_state_t wifi, app_conn_state_t uart, app_conn_state_t win)
{
    portENTER_CRITICAL(&s_lock);
    s_state.conn.wifi = wifi;
    s_state.conn.uart = uart;
    s_state.conn.win = win;
    portEXIT_CRITICAL(&s_lock);
}

void app_state_publish_chat(app_conn_state_t conn, const char *summary)
{
    portENTER_CRITICAL(&s_lock);
    s_state.chat.conn = conn;
    if (summary)
    {
        strncpy(s_state.chat.summary, summary, sizeof(s_state.chat.summary) - 1);
        s_state.chat.summary[sizeof(s_state.chat.summary) - 1] = '\0';
    }
    portEXIT_CRITICAL(&s_lock);
}

void app_state_publish_media(bool has_source, bool playing, const char *title, const char *artist)
{
    portENTER_CRITICAL(&s_lock);
    s_state.media.has_source = has_source;
    s_state.media.playing = playing;
    if (title)
    {
        strncpy(s_state.media.title, title, sizeof(s_state.media.title) - 1);
        s_state.media.title[sizeof(s_state.media.title) - 1] = '\0';
    }
    if (artist)
    {
        strncpy(s_state.media.artist, artist, sizeof(s_state.media.artist) - 1);
        s_state.media.artist[sizeof(s_state.media.artist) - 1] = '\0';
    }
    portEXIT_CRITICAL(&s_lock);
}

/* ------------------------------------------------------------------ */
/* Live2D 快照                                                         */
/* ------------------------------------------------------------------ */

/* 表情白名单（协议单一事实来源见 ESP32/接口文档.md §6.5） */
static const char *const s_expr_names[APP_EXPR_COUNT] = {
    "neutral", "happy", "sad", "angry", "surprised", "thinking",
};

/* 动作白名单 */
static const char *const s_motion_names[APP_MOTION_COUNT] = {
    "idle", "speaking", "listening", "thinking", "waving",
};

app_expr_t app_expr_from_str(const char *s)
{
    if (s)
    {
        for (int i = 0; i < APP_EXPR_COUNT; i++)
        {
            if (strcmp(s_expr_names[i], s) == 0) return (app_expr_t)i;
        }
    }
    return APP_EXPR_NEUTRAL;
}

app_motion_t app_motion_from_str(const char *s)
{
    if (s)
    {
        for (int i = 0; i < APP_MOTION_COUNT; i++)
        {
            if (strcmp(s_motion_names[i], s) == 0) return (app_motion_t)i;
        }
    }
    return APP_MOTION_IDLE;
}

const char *app_expr_to_str(app_expr_t e)
{
    return (e >= 0 && e < APP_EXPR_COUNT) ? s_expr_names[e] : s_expr_names[APP_EXPR_NEUTRAL];
}

const char *app_motion_to_str(app_motion_t m)
{
    return (m >= 0 && m < APP_MOTION_COUNT) ? s_motion_names[m] : s_motion_names[APP_MOTION_IDLE];
}

/* 截断到 max_bytes 字节（不拆断 UTF-8 字符，末尾补 '\0'）。
 * 仅用于 ASCII/UTF-8 多字节文本的显示安全截断。 */
static void truncate_utf8(char *dst, size_t cap, const char *src, size_t max_bytes)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }

    size_t len = strlen(src);
    if (len > max_bytes) len = max_bytes;
    /* 回退：若 len 指向多字节字符的中间字节，则回退到该字符首字节 */
    while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    size_t copy = len < cap - 1 ? len : cap - 1;
    memcpy(dst, src, copy);
    dst[copy] = '\0';
}

void app_state_publish_live2d(bool connected, app_expr_t expression, app_motion_t motion,
                              const char *message_preview)
{
    portENTER_CRITICAL(&s_lock);
    s_state.live2d.connected = connected;
    s_state.live2d.expression = expression;
    s_state.live2d.motion = motion;
    truncate_utf8(s_state.live2d.message_preview, sizeof(s_state.live2d.message_preview),
                  message_preview, sizeof(s_state.live2d.message_preview) - 1);
    s_state.live2d.updated_ms = now_ms();
    portEXIT_CRITICAL(&s_lock);
}

void app_state_publish_live2d_conn(bool connected)
{
    portENTER_CRITICAL(&s_lock);
    s_state.live2d.connected = connected;
    s_state.live2d.updated_ms = now_ms();
    portEXIT_CRITICAL(&s_lock);
}

void app_state_publish_live2d_message(const char *message_preview)
{
    portENTER_CRITICAL(&s_lock);
    truncate_utf8(s_state.live2d.message_preview, sizeof(s_state.live2d.message_preview),
                  message_preview, sizeof(s_state.live2d.message_preview) - 1);
    s_state.live2d.updated_ms = now_ms();
    portEXIT_CRITICAL(&s_lock);
}

const app_state_t *app_state_get(void)
{
    return &s_state;
}
