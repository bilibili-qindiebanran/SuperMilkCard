#include "stt_dashscope.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "network/net_config.h"

#define TAG "stt_dashscope"
#define STT_STARTED BIT0
#define STT_FINISHED BIT1
#define STT_FAILED BIT2
#define STT_CONNECTED BIT3
#define STT_CHUNK 1600

typedef struct {
    const uint8_t *pcm;
    size_t pcm_len;
    char *text;
    size_t text_cap;
    EventGroupHandle_t events;
    esp_websocket_client_handle_t client;
    bool sent_audio;
    char task_id[48];
} stt_context_t;

static const char *json_string_after_key(const char *object, const char *key)
{
    const char *p = strstr(object, key);
    if (!p) return NULL;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '"' ? p + 1 : NULL;
}

static void copy_json_text(const char *json, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';

    const char *sentence = strstr(json, "\"sentence\"");
    const char *p = sentence ? json_string_after_key(sentence, "\"text\"") : NULL;
    if (!p) p = json_string_after_key(json, "\"text\"");
    if (!p) return;

    size_t input = 0;
    size_t output = 0;
    while (p[input] && p[input] != '"' && output + 1 < cap)
    {
        if (p[input] == '\\' && p[input + 1])
        {
            char escaped = p[input + 1];
            if (escaped == 'n') out[output++] = '\n';
            else if (escaped == 'r') out[output++] = '\r';
            else if (escaped == 't') out[output++] = '\t';
            else out[output++] = escaped;
            input += 2;
        }
        else
        {
            out[output++] = p[input++];
        }
    }
    out[output] = '\0';
}
static int send_start_task(stt_context_t *ctx, const net_config_t *cfg)
{
    char start[512];
    snprintf(start, sizeof(start),
             "{\"header\":{\"action\":\"run-task\",\"streaming\":\"duplex\",\"task_id\":\"%s\"},"
             "\"payload\":{\"model\":\"%s\",\"parameters\":{\"format\":\"pcm\",\"sample_rate\":16000},"
             "\"input\":{},\"task\":\"asr\",\"task_group\":\"audio\",\"function\":\"recognition\"}}",
             ctx->task_id, cfg->stt_model);
    return esp_websocket_client_send_text(ctx->client, start, (int)strlen(start), pdMS_TO_TICKS(5000));
}

static int send_audio(stt_context_t *ctx)
{
    if (ctx->sent_audio) return 0;
    for (size_t off = 0; off < ctx->pcm_len; off += STT_CHUNK)
    {
        size_t len = ctx->pcm_len - off;
        if (len > STT_CHUNK) len = STT_CHUNK;
        if (esp_websocket_client_send_bin(ctx->client, (const char *)ctx->pcm + off,
                                          (int)len, pdMS_TO_TICKS(5000)) < 0)
        {
            return -1;
        }
    }
    char finish[160];
    snprintf(finish, sizeof(finish), "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\"},\"payload\":{\"input\":{}}}", ctx->task_id);
    if (esp_websocket_client_send_text(ctx->client, finish, (int)strlen(finish), pdMS_TO_TICKS(5000)) < 0)
    {
        return -1;
    }
    ctx->sent_audio = true;
    ESP_LOGI(TAG, "PCM sent: %u bytes", (unsigned)ctx->pcm_len);
    return 0;
}

static void websocket_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    stt_context_t *ctx = arg;
    esp_websocket_event_data_t *event = event_data;
    if (event_id == WEBSOCKET_EVENT_ERROR || event_id == WEBSOCKET_EVENT_DISCONNECTED)
    {
        xEventGroupSetBits(ctx->events, STT_FAILED);
        return;
    }
    if (event_id == WEBSOCKET_EVENT_CONNECTED)
    {
        xEventGroupSetBits(ctx->events, STT_CONNECTED);
        return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA || event->op_code != 0x1) return;

    char msg[1024];
    size_t len = event->data_len < sizeof(msg) - 1 ? event->data_len : sizeof(msg) - 1;
    memcpy(msg, event->data_ptr, len);
    msg[len] = '\0';
    if (strstr(msg, "task-started"))
    {
        xEventGroupSetBits(ctx->events, STT_STARTED);
    }
    if (strstr(msg, "result-generated"))
    {
        char parsed_text[384];
        copy_json_text(msg, parsed_text, sizeof(parsed_text));
        if (parsed_text[0])
        {
            strncpy(ctx->text, parsed_text, ctx->text_cap - 1);
            ctx->text[ctx->text_cap - 1] = '\0';
            ESP_LOGI(TAG, "result-generated text: %s", ctx->text);
        }
    }
    if (strstr(msg, "task-finished"))
    {
        ESP_LOGI(TAG, "DashScope task-finished, text=%s", ctx->text[0] ? ctx->text : "<empty>");
        xEventGroupSetBits(ctx->events, STT_FINISHED);
    }
    if (strstr(msg, "task-failed"))
    {
        ESP_LOGE(TAG, "DashScope task failed: %s", msg);
        xEventGroupSetBits(ctx->events, STT_FAILED);
    }
}

esp_err_t stt_dashscope_transcribe(const uint8_t *pcm, size_t pcm_len, char *text, size_t text_cap)
{
    if (!pcm || pcm_len == 0 || !text || text_cap == 0) return ESP_ERR_INVALID_ARG;
    text[0] = '\0';
    net_config_t cfg;
    net_config_load(&cfg);
    if (!cfg.stt_api_key[0]) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "heap before TLS: 8bit=%u/%u internal=%u/%u psram=%u/%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    EventGroupHandle_t events = xEventGroupCreate();
    if (!events) return ESP_ERR_NO_MEM;
    stt_context_t ctx = {.pcm = pcm, .pcm_len = pcm_len, .text = text, .text_cap = text_cap,
                         .events = events, .client = NULL, .sent_audio = false};
    snprintf(ctx.task_id, sizeof(ctx.task_id), "esp32_%lu", (unsigned long)xTaskGetTickCount());
    char headers[320];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", cfg.stt_api_key);
    esp_websocket_client_config_t ws_cfg = {
        .uri = cfg.stt_url,
        .headers = headers,
        .buffer_size = 2048,
        .network_timeout_ms = 15000,
        .task_stack = 4096,
        .disable_auto_reconnect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    ctx.client = esp_websocket_client_init(&ws_cfg);
    if (!ctx.client) { vEventGroupDelete(events); return ESP_FAIL; }
    esp_websocket_register_events(ctx.client, WEBSOCKET_EVENT_ANY, websocket_event, &ctx);
    esp_err_t err = esp_websocket_client_start(ctx.client);
    if (err == ESP_OK)
    {
        EventBits_t bits = xEventGroupWaitBits(events, STT_CONNECTED | STT_FAILED,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(20000));
        if (bits & STT_FAILED) err = ESP_FAIL;
        else if (!(bits & STT_CONNECTED)) err = ESP_ERR_TIMEOUT;
        else
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (send_start_task(&ctx, &cfg) < 0)
            {
                err = ESP_FAIL;
            }
            else
            {
                bits = xEventGroupWaitBits(events, STT_STARTED | STT_FAILED,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(20000));
                if (bits & STT_FAILED) err = ESP_FAIL;
                else if (!(bits & STT_STARTED)) err = ESP_ERR_TIMEOUT;
                else if (send_audio(&ctx) < 0) err = ESP_FAIL;
                else
                {
                    bits = xEventGroupWaitBits(events, STT_FINISHED | STT_FAILED,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
                    if (bits & STT_FAILED) err = ESP_FAIL;
                    else if (!(bits & STT_FINISHED)) err = ESP_ERR_TIMEOUT;
                }
            }
        }
    }
    esp_websocket_client_stop(ctx.client);
    esp_websocket_client_destroy(ctx.client);
    if (err == ESP_OK && text[0] == '\0') err = ESP_ERR_INVALID_RESPONSE;
    vEventGroupDelete(events);
    return err;
}
