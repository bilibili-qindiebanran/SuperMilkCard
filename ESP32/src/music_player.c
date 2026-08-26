#include "music_player.h"

#include <stdbool.h>
#include <string.h>

#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "i2s_audio.h"

#define TAG "music_player"
#define MUSIC_RING_BYTES (256 * 1024)
#define MUSIC_INPUT_BYTES 4096
#define MUSIC_OUTPUT_BYTES 16384

typedef union {
    esp_m4a_dec_cfg_t m4a_cfg;
    esp_ts_dec_cfg_t ts_cfg;
    esp_aac_dec_cfg_t aac_cfg;
    esp_sbc_dec_cfg_t sbc_cfg;
    esp_lc3_dec_cfg_t lc3_cfg;
    esp_opus_dec_cfg_t opus_cfg;
} music_dec_cfg_t;

static uint8_t *s_ring;
static size_t s_read_pos;
static size_t s_write_pos;
static size_t s_ring_used;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_data_ready;
static TaskHandle_t s_task;
static bool s_started;
static bool s_finished;
static bool s_stop_requested;

static void ring_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_read_pos = 0;
    s_write_pos = 0;
    s_ring_used = 0;
    s_finished = false;
    s_stop_requested = false;
    xSemaphoreGive(s_lock);
}

static size_t ring_available(void)
{
    size_t available;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    available = s_ring_used;
    xSemaphoreGive(s_lock);
    return available;
}

static size_t ring_write(const uint8_t *data, size_t len)
{
    size_t first = MUSIC_RING_BYTES - s_write_pos;
    if (first > len) first = len;
    memcpy(s_ring + s_write_pos, data, first);
    if (len > first) memcpy(s_ring, data + first, len - first);
    s_write_pos = (s_write_pos + len) % MUSIC_RING_BYTES;
    s_ring_used += len;
    return len;
}

static size_t ring_read(uint8_t *data, size_t len)
{
    if (len > s_ring_used) len = s_ring_used;
    size_t first = MUSIC_RING_BYTES - s_read_pos;
    if (first > len) first = len;
    memcpy(data, s_ring + s_read_pos, first);
    if (len > first) memcpy(data + first, s_ring, len - first);
    s_read_pos = (s_read_pos + len) % MUSIC_RING_BYTES;
    s_ring_used -= len;
    return len;
}

static void music_decode_task(void *arg)
{
    (void)arg;
    uint8_t *input = heap_caps_malloc(MUSIC_INPUT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *output = heap_caps_malloc(MUSIC_OUTPUT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!input || !output)
    {
        ESP_LOGE(TAG, "decoder buffers allocation failed");
        vTaskDelete(NULL);
        return;
    }

    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    for (;;)
    {
        xSemaphoreTake(s_data_ready, portMAX_DELAY);
        if (!s_started) continue;

        music_dec_cfg_t all_cfg = {};
        esp_audio_simple_dec_cfg_t cfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg = &all_cfg,
            .use_frame_dec = false,
        };
        esp_audio_simple_dec_handle_t decoder = NULL;
        esp_audio_err_t err = esp_audio_simple_dec_open(&cfg, &decoder);
        if (err != ESP_AUDIO_ERR_OK)
        {
            ESP_LOGE(TAG, "MP3 decoder open failed: %d", err);
            s_started = false;
            continue;
        }

        size_t raw_len = 0;
        bool got_info = false;
        while (s_started)
        {
            if (raw_len == 0)
            {
                while (ring_available() == 0)
                {
                    if (s_finished || s_stop_requested) break;
                    xSemaphoreTake(s_data_ready, pdMS_TO_TICKS(100));
                }
                if (ring_available() > 0)
                {
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    raw_len = ring_read(input, MUSIC_INPUT_BYTES);
                    xSemaphoreGive(s_lock);
                }
            }

            if (raw_len > 0)
            {
                esp_audio_simple_dec_raw_t raw = {
                    .buffer = input,
                    .len = raw_len,
                    .eos = false,
                };
                esp_audio_simple_dec_out_t out = {
                    .buffer = output,
                    .len = MUSIC_OUTPUT_BYTES,
                };
                err = esp_audio_simple_dec_process(decoder, &raw, &out);
                if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && out.needed_size > MUSIC_OUTPUT_BYTES)
                {
                    uint8_t *next = heap_caps_realloc(output, out.needed_size,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (!next)
                    {
                        ESP_LOGE(TAG, "decoder output buffer resize failed");
                        break;
                    }
                    output = next;
                    out.buffer = output;
                    out.len = out.needed_size;
                    err = esp_audio_simple_dec_process(decoder, &raw, &out);
                }
                if (err != ESP_AUDIO_ERR_OK)
                {
                    ESP_LOGW(TAG, "MP3 decode failed: %d", err);
                    raw_len = 0;
                    continue;
                }
                if (out.decoded_size > 0)
                {
                    esp_audio_simple_dec_info_t info = {};
                    esp_audio_simple_dec_get_info(decoder, &info);
                    if (!got_info)
                    {
                        i2s_audio_set_tx_sample_rate(info.sample_rate);
                        ESP_LOGI(TAG, "MP3 info: rate=%d bits=%d channels=%d",
                                 info.sample_rate, info.bits_per_sample, info.channel);
                        got_info = true;
                    }
                    i2s_audio_write_pcm16((const int16_t *)out.buffer,
                                          out.decoded_size / sizeof(int16_t),
                                          (uint8_t)info.channel);
                }
                if (raw.consumed > 0)
                {
                    raw_len -= raw.consumed;
                    if (raw_len > 0) memmove(input, input + raw.consumed, raw_len);
                }
            }

            if (raw_len == 0 && (s_finished || s_stop_requested) && ring_available() == 0)
                break;
        }

        esp_audio_simple_dec_close(decoder);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_started = false;
        s_finished = false;
        s_stop_requested = false;
        xSemaphoreGive(s_lock);
    }
}

esp_err_t music_player_init(void)
{
    if (s_task) return ESP_OK;
    s_ring = heap_caps_malloc(MUSIC_RING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_lock = xSemaphoreCreateMutex();
    s_data_ready = xSemaphoreCreateBinary();
    if (!s_ring || !s_lock || !s_data_ready) return ESP_ERR_NO_MEM;
    if (xTaskCreate(music_decode_task, "music_decode", 16384, NULL, 4, &s_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t music_player_start(void)
{
    if (!s_task) return ESP_ERR_INVALID_STATE;
    ring_reset();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_started = true;
    xSemaphoreGive(s_lock);
    xSemaphoreGive(s_data_ready);
    ESP_LOGI(TAG, "MP3 stream started");
    return ESP_OK;
}

esp_err_t music_player_write(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    while (len > 0)
    {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!s_started || s_stop_requested)
        {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_STATE;
        }
        size_t free_bytes = MUSIC_RING_BYTES - s_ring_used;
        size_t take = len < free_bytes ? len : free_bytes;
        if (take > 0) ring_write(data, take);
        xSemaphoreGive(s_lock);
        if (take == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        data += take;
        len -= take;
        xSemaphoreGive(s_data_ready);
    }
    return ESP_OK;
}

esp_err_t music_player_finish(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_finished = true;
    xSemaphoreGive(s_lock);
    xSemaphoreGive(s_data_ready);
    return ESP_OK;
}

void music_player_stop(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stop_requested = true;
    s_finished = true;
    s_started = false;
    xSemaphoreGive(s_lock);
    xSemaphoreGive(s_data_ready);
    ESP_LOGI(TAG, "MP3 stream stopped");
}
