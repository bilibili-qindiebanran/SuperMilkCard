/**
 * @file app_state.h
 * @brief 应用状态模型：UI 不直接读驱动私有变量，统一经此模块取一致快照
 *
 * 分类：power / audio / clock / connectivity / chat / media
 * 驱动/协议侧通过 app_state_publish_*() 更新；UI 每帧读取快照。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 连接状态 */
typedef enum {
    APP_CONN_OFF = 0,    /* 未接入 */
    APP_CONN_OK,         /* 可用/已连接 */
    APP_CONN_DISCONNECTED, /* 未连接 */
} app_conn_state_t;

/* 电源状态快照 */
typedef struct {
    bool charging;
    bool charge_full;
    bool light_load;
    uint32_t updated_ms; /* 更新时间戳 */
} app_state_power_t;

/* 音频状态快照 */
typedef struct {
    float rms_db;
    float peak_db;
    uint32_t updated_ms;
} app_state_audio_t;

/* 时钟状态快照（首版未接入真实时钟） */
typedef struct {
    bool synced;         /* 是否已同步 */
    uint8_t hour, min;   /* 有效时的时间 */
    const char *source;  /* 来源 */
} app_state_clock_t;

/* 连接状态快照 */
typedef struct {
    app_conn_state_t wifi;
    app_conn_state_t uart;
    app_conn_state_t win;
} app_state_conn_t;

/* 聊天状态快照 */
typedef struct {
    app_conn_state_t conn;  /* 数据源连接状态 */
    char summary[32];       /* 简短摘要 */
} app_state_chat_t;

/* 媒体状态快照 */
typedef struct {
    bool has_source;    /* 是否有媒体源 */
    bool playing;
    char title[32];
    char artist[32];
} app_state_media_t;

/* 完整状态快照（UI 每帧读取） */
typedef struct {
    app_state_power_t power;
    app_state_audio_t audio;
    app_state_clock_t clock;
    app_state_conn_t conn;
    app_state_chat_t chat;
    app_state_media_t media;
} app_state_t;

/* ------------------------------------------------------------------ */
/* 发布（驱动/任务侧调用）                                            */
/* ------------------------------------------------------------------ */
void app_state_publish_power(bool charging, bool full, bool light_load);
void app_state_publish_audio(float rms_db, float peak_db);
void app_state_publish_clock(bool synced, uint8_t hour, uint8_t min, const char *source);
void app_state_publish_conn(app_conn_state_t wifi, app_conn_state_t uart, app_conn_state_t win);
void app_state_publish_chat(app_conn_state_t conn, const char *summary);
void app_state_publish_media(bool has_source, bool playing, const char *title, const char *artist);

/* ------------------------------------------------------------------ */
/* 读取快照（UI 调用）                                                */
/* ------------------------------------------------------------------ */
const app_state_t *app_state_get(void);

#ifdef __cplusplus
}
#endif
