#include "audio_voice.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2s_audio.h"
#include "network/net_tcp.h"
#include "stt_dashscope.h"

#define TAG "audio_voice"
#define DECIMATION 3
#define CHUNK_FRAMES 120
#define CHUNK_OUT 40
#define MAX_PCM_BYTES (16000 * 2 * AUDIO_VOICE_MAX_SEC)
#define TEXT_MAX 384

static TaskHandle_t s_task;
static volatile bool s_recording;

static size_t json_escape(char *out, size_t cap, const char *text)
{
    size_t pos = 0;
    for (size_t i = 0; text && text[i] && pos + 2 < cap; i++)
    {
        char c = text[i];
        if (c == '"' || c == '\\') out[pos++] = '\\';
        out[pos++] = c;
    }
    out[pos] = '\0';
    return pos;
}

static void send_voice_text(const char *text)
{
    char escaped[TEXT_MAX * 2];
    char json[TEXT_MAX * 2 + 64];
    json_escape(escaped, sizeof(escaped), text);
    snprintf(json, sizeof(json), "{\"type\":\"voice_text\",\"text\":\"%s\"}", escaped);
    net_tcp_send_json(json);
}

static void voice_task(void *arg)
{
    (void)arg;
    uint8_t *pcm = heap_caps_malloc(MAX_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) pcm = heap_caps_malloc(MAX_PCM_BYTES, MALLOC_CAP_8BIT);
    if (!pcm)
    {
        ESP_LOGE(TAG, "PCM buffer allocation failed: %u bytes", MAX_PCM_BYTES);
        vTaskDelete(NULL);
        return;
    }

    int32_t in[CHUNK_FRAMES * 2];
    int16_t out[CHUNK_OUT];
    char text[TEXT_MAX];

    while (1)
    {
        while (!s_recording) vTaskDelay(pdMS_TO_TICKS(20));
        size_t pcm_len = 0;
        i2s_audio_set_rx_exclusive(true);
        ESP_LOGI(TAG, "recording locally for ESP32 STT");

        while (s_recording && pcm_len + sizeof(out) <= MAX_PCM_BYTES)
        {
            if (i2s_audio_read_voice(in, CHUNK_FRAMES) != ESP_OK)
            {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            for (int i = 0; i < CHUNK_OUT; i++)
            {
                int32_t sample = in[i * DECIMATION * 2] >> 16;
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                out[i] = (int16_t)sample;
            }
            memcpy(pcm + pcm_len, out, sizeof(out));
            pcm_len += sizeof(out);
        }
        if (pcm_len + sizeof(out) > MAX_PCM_BYTES) s_recording = false;
        i2s_audio_set_rx_exclusive(false);

        if (pcm_len == 0) continue;
        text[0] = '\0';
        esp_err_t err = stt_dashscope_transcribe(pcm, pcm_len, text, sizeof(text));
        if (err == ESP_OK && text[0])
        {
            ESP_LOGI(TAG, "STT result: %s", text);
            send_voice_text(text);
        }
        else
        {
            ESP_LOGE(TAG, "STT failed: %s", esp_err_to_name(err));
            net_tcp_send_json("{\"type\":\"voice_error\",\"message\":\"ESP32 STT failed\"}");
        }
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

esp_err_t audio_voice_init(void)
{
    if (s_task) return ESP_OK;
    if (xTaskCreate(voice_task, "audio_voice", 10240, NULL, 4, &s_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}