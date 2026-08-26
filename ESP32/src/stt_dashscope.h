#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 使用设备 NVS 中的 DashScope 配置，将 16kHz/16bit/单声道 PCM 送入 ASR。 */
esp_err_t stt_dashscope_transcribe(const uint8_t *pcm, size_t pcm_len, char *text, size_t text_cap);

#ifdef __cplusplus
}
#endif