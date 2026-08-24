/**
 * @file audio_voice.h
 * @brief 语音上行：I2S 麦克风 → 16kHz/16bit PCM → TCP voice_start/分片/voice_end
 *
 * 链路：Live2D 联动模式下按住录音 → audio_voice_start() 开录 →
 *       内部任务读 I2S(48k/32bit) → 降采样 16k/16bit → 分片发 AUDIO 帧 →
 *       audio_voice_stop() 发送 voice_end，Windows 侧 STT。
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 开始录音（非阻塞）。若已连接 Windows 客户端则发送 voice_start 并启动采集任务。
 * @return ESP_OK 已开始；ESP_ERR_NOT_FOUND 未连接；ESP_ERR_INVALID_STATE 已在录
 */
esp_err_t audio_voice_start(void);

/**
 * @brief 停止录音并发送 voice_end（若正在进行）。
 */
void audio_voice_stop(void);

/** @brief 是否正在录音 */
bool audio_voice_is_recording(void);

/** @brief 初始化录音任务（app_main 调用一次） */
esp_err_t audio_voice_init(void);

/** @brief 录音最长时长（秒），超时自动停止（默认 10s） */
#define AUDIO_VOICE_MAX_SEC 10

#ifdef __cplusplus
}
#endif
