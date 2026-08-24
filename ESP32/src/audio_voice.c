/**
 * @file audio_voice.c
 * @brief 语音上行实现
 *
 * 降采样：I2S 48kHz 32-bit slot（24-bit 有效高位）→ 16kHz 16-bit 小端 PCM。
 * 采集任务：阻塞读 I2S 一小块 → 每取 3 个采样取 1 个（48k→16k，简单抽取）→
 *           缩放到 16-bit → 累积到分片缓冲 → 满 8KB 发一帧 AUDIO。
 * 只保留 L 声道（麦克风接左声道）。
 */

#include "audio_voice.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2s_audio.h"
#include "network/net_tcp.h"

#define AUDIO_VOICE_TAG "audio_voice"
#define AUDIO_VOICE_DECIMATION 3      /* 48k → 16k */
#define AUDIO_VOICE_CHUNK_FRAMES 120  /* 每次读 120 帧 I2S（=2.5ms 48k，≤DMA 缓冲 128 帧） */
#define AUDIO_VOICE_CHUNK_OUT 40      /* 抽取后 40 采样（=2.5ms 16k） */
#define AUDIO_VOICE_16K 16000

static const char *TAG = AUDIO_VOICE_TAG;

static TaskHandle_t s_task;
static volatile bool s_recording;

/* 大缓冲放静态区，避免任务栈溢出（栈仅保留控制量） */
static uint8_t s_frag[AUDIO_VOICE_FRAG_BYTES];

/* 录音任务 */
static void voice_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "voice task started");

    int32_t in[AUDIO_VOICE_CHUNK_FRAMES * 2];        /* 48k 32-bit 立体声（960B） */
    int16_t out[AUDIO_VOICE_CHUNK_OUT];              /* 抽取后 16-bit 单声道（80B） */
    uint32_t frag_len = 0;
    uint32_t total_ms = 0;

    while (1)
    {
        /* 等待开始录音 */
        while (!s_recording)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        /* 发送 voice_start + 独占麦克风 */
        net_tcp_send_json(
            "{\"type\":\"voice_start\",\"format\":\"pcm\",\"sampleRate\":16000,"
            "\"channels\":1,\"bits\":16}");
        i2s_audio_set_rx_exclusive(true);
        ESP_LOGI(TAG, "recording start (16k/1ch/16bit)");

        frag_len = 0;
        total_ms = 0;

        while (s_recording)
        {
            static int diag_fail;
            static int diag_ok;
            int rd_err = i2s_audio_read_voice(in, AUDIO_VOICE_CHUNK_FRAMES);
            if (rd_err != ESP_OK)
            {
                /* 诊断：读失败计数 */
                if (++diag_fail <= 5)
                {
                    ESP_LOGW(TAG, "i2s read fail: %s", esp_err_to_name(rd_err));
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            diag_ok++;
            if (diag_ok == 1)
            {
                ESP_LOGI(TAG, "i2s read OK (mic data flowing)");
            }

            /* 抽取降采样：每 3 个采样取 1 个（L 声道 32-bit → 16-bit） */
            for (int i = 0; i < AUDIO_VOICE_CHUNK_OUT; i++)
            {
                int32_t l = in[i * AUDIO_VOICE_DECIMATION * 2] >> 16; /* 24-bit 高位 → 16-bit */
                if (l > 32767) l = 32767;
                if (l < -32768) l = -32768;
                out[i] = (int16_t)l;
            }

            /* 累积到分片，满 4KB 发一帧 */
            uint32_t out_bytes = sizeof(out);
            if (frag_len + out_bytes > sizeof(s_frag))
            {
                if (frag_len > 0)
                {
                    net_tcp_send_audio(s_frag, frag_len);
                    frag_len = 0;
                }
            }
            memcpy(s_frag + frag_len, out, out_bytes);
            frag_len += out_bytes;
            if (frag_len >= AUDIO_VOICE_FRAG_BYTES)
            {
                net_tcp_send_audio(s_frag, frag_len);
                frag_len = 0;
            }

            total_ms += 2; /* 每块 2.5ms，约 2ms 精度即可 */
            /* 超时自动停止 */
            if (total_ms >= AUDIO_VOICE_MAX_SEC * 1000)
            {
                ESP_LOGI(TAG, "recording max %ds reached, stop", AUDIO_VOICE_MAX_SEC);
                s_recording = false;
            }
        }

        /* 发送剩余 + voice_end + 释放麦克风独占 */
        if (frag_len > 0)
        {
            net_tcp_send_audio(s_frag, frag_len);
        }
        net_tcp_send_json("{\"type\":\"voice_end\"}");
        i2s_audio_set_rx_exclusive(false);
        ESP_LOGI(TAG, "recording stop, sent %u ms", (unsigned)total_ms);
    }
}

esp_err_t audio_voice_start(void)
{
    if (s_recording) return ESP_ERR_INVALID_STATE;
    if (!net_tcp_is_client_connected()) return ESP_ERR_NOT_FOUND;
    s_recording = true;
    return ESP_OK;
}

void audio_voice_stop(void)
{
    s_recording = false;
}

bool audio_voice_is_recording(void)
{
    return s_recording;
}

/* 初始化：创建录音任务 */
esp_err_t audio_voice_init(void)
{
    if (s_task) return ESP_OK;
    /* 栈 8KB：in(3.8KB)+out(0.3KB)+调用开销；frag 已移静态区 */
    if (xTaskCreate(voice_task, "audio_voice", 8192, NULL, 4, &s_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
