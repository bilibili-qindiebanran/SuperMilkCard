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

const app_state_t *app_state_get(void)
{
    return &s_state;
}
