#include "audio_voice.h"

#include <string.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "network/net_wifi.h"
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
static portMUX_TYPE s_sink_mux = portMUX_INITIALIZER_UNLOCKED;
static audio_voice_text_cb_t s_text_sink;
static void *s_text_sink_ctx;

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
    esp_err_t err = net_tcp_send_json(json);
    ESP_LOGI(TAG, "voice_text sent: bytes=%u result=%s", (unsigned)strlen(json),
             esp_err_to_name(err));
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
        uint32_t read_ok = 0;
        uint32_t read_fail = 0;
        uint32_t nonzero = 0;
        int32_t peak = 0;
        uint64_t abs_sum = 0;
        TickType_t last_diag = xTaskGetTickCount();
        TickType_t last_read_warn = 0;
        i2s_audio_set_rx_exclusive(true);
        ESP_LOGI(TAG, "recording locally for ESP32 STT (I2S 48k stereo -> 16k mono)");

        while (s_recording && pcm_len + sizeof(out) <= MAX_PCM_BYTES)
        {
            size_t frames_read = 0;
            esp_err_t read_err = i2s_audio_read_voice(in, CHUNK_FRAMES, &frames_read);
            if (read_err != ESP_OK)
            {
                read_fail++;
                if ((xTaskGetTickCount() - last_read_warn) >= pdMS_TO_TICKS(1000))
                {
                    ESP_LOGW(TAG, "I2S capture read failed: %s (ok=%lu fail=%lu)",
                             esp_err_to_name(read_err), (unsigned long)read_ok, (unsigned long)read_fail);
                    last_read_warn = xTaskGetTickCount();
                }
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            read_ok++;
            size_t output_samples = frames_read / DECIMATION;
            if (output_samples > CHUNK_OUT) output_samples = CHUNK_OUT;
            for (size_t i = 0; i < output_samples; i++)
            {
                /* Current ICS-43434 driver data is scaled from the upper 16 bits. */
                int32_t sample = in[i * DECIMATION * 2] >> 16;
                int32_t abs_sample = sample < 0 ? -sample : sample;
                if (abs_sample > peak) peak = abs_sample;
                if (abs_sample > 0) nonzero++;
                abs_sum += (uint32_t)abs_sample;
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                out[i] = (int16_t)sample;
            }
            size_t output_bytes = output_samples * sizeof(int16_t);
            memcpy(pcm + pcm_len, out, output_bytes);
            pcm_len += output_bytes;
            if ((xTaskGetTickCount() - last_diag) >= pdMS_TO_TICKS(500))
            {
                uint32_t samples = (uint32_t)(pcm_len / sizeof(int16_t));
                ESP_LOGI(TAG, "capture status: ok=%lu fail=%lu pcm=%uB samples=%lu nonzero=%lu peak=%ld avg_abs=%lu",
                         (unsigned long)read_ok, (unsigned long)read_fail, (unsigned)pcm_len,
                         (unsigned long)samples, (unsigned long)nonzero, (long)peak,
                         (unsigned long)(samples ? abs_sum / samples : 0));
                last_diag = xTaskGetTickCount();
            }
        }
        if (pcm_len + sizeof(out) > MAX_PCM_BYTES) s_recording = false;
        i2s_audio_set_rx_exclusive(false);

        ESP_LOGI(TAG, "recording stopped: pcm=%uB duration=%ums ok=%lu fail=%lu nonzero=%lu peak=%ld avg_abs=%lu",
                 (unsigned)pcm_len, (unsigned)(pcm_len / 32), (unsigned long)read_ok,
                 (unsigned long)read_fail, (unsigned long)nonzero, (long)peak,
                 (unsigned long)(pcm_len ? abs_sum / (pcm_len / sizeof(int16_t)) : 0));
        if (pcm_len == 0) continue;
        text[0] = '\0';
        esp_err_t err = stt_dashscope_transcribe(pcm, pcm_len, text, sizeof(text));
        if (err == ESP_OK && text[0])
        {
            ESP_LOGI(TAG, "STT result: %s", text);
            audio_voice_text_cb_t sink;
            void *sink_ctx;
            portENTER_CRITICAL(&s_sink_mux);
            sink = s_text_sink;
            sink_ctx = s_text_sink_ctx;
            portEXIT_CRITICAL(&s_sink_mux);
            if (sink)
                sink(text, sink_ctx);
            else if (net_tcp_is_client_connected())
                send_voice_text(text);
        }
        else
        {
            ESP_LOGE(TAG, "STT failed: %s", esp_err_to_name(err));
            if (net_tcp_is_client_connected())
                net_tcp_send_json("{\"type\":\"voice_error\",\"message\":\"ESP32 STT failed\"}");
        }
    }
}

esp_err_t audio_voice_start(void)
{
    if (s_recording) return ESP_ERR_INVALID_STATE;
    if (!net_tcp_is_client_connected() && !net_wifi_is_connected()) return ESP_ERR_NOT_FOUND;
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
void audio_voice_set_text_sink(audio_voice_text_cb_t cb, void *user_ctx)
{
    portENTER_CRITICAL(&s_sink_mux);
    s_text_sink = cb;
    s_text_sink_ctx = user_ctx;
    portEXIT_CRITICAL(&s_sink_mux);
}
