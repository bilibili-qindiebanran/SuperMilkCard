#include "stt_dashscope.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "network/net_config.h"

#define TAG "stt_dashscope"
#define STT_STARTED BIT0
#define STT_FINISHED BIT1
#define STT_FAILED BIT2
#define STT_CHUNK 3200

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

static void copy_json_text(const char *json, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    const char *p = strstr(json, "\"text\":\"");
    if (!p) return;
    p += strlen("\"text\":\"");
    size_t n = 0;
    while (p[n] && p[n] != '"' && n + 1 < cap)
    {
        if (p[n] == '\\' && p[n + 1]) n++;
        out[n] = p[n];
        n++;
    }
    out[n] = '\0';
}

static void send_audio(stt_context_t *ctx)
{
    if (ctx->sent_audio) return;
    for (size_t off = 0; off < ctx->pcm_len; off += STT_CHUNK)
    {
        size_t len = ctx->pcm_len - off;
        if (len > STT_CHUNK) len = STT_CHUNK;
        if (esp_websocket_client_send_bin(ctx->client, (const char *)ctx->pcm + off,
                                          (int)len, 5000) < 0)
        {
            xEventGroupSetBits(ctx->events, STT_FAILED);
            return;
        }
    }
    char finish[160];
    snprintf(finish, sizeof(finish), "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"%s\"},\"payload\":{\"input\":{}}}", ctx->task_id);
    esp_websocket_client_send_text(ctx->client, finish, (int)strlen(finish), 5000);
    ctx->sent_audio = true;
    ESP_LOGI(TAG, "PCM sent: %u bytes", (unsigned)ctx->pcm_len);
}

static void websocket_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    stt_context_t *ctx = arg;
    esp_websocket_event_data_t *event = event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED)
    {
        net_config_t cfg;
        net_config_load(&cfg);
        char start[512];
        snprintf(start, sizeof(start),
                 "{\"header\":{\"action\":\"run-task\",\"streaming\":\"duplex\",\"task_id\":\"esp32_%lu\"},"
                 "\"payload\":{\"model\":\"%s\",\"parameters\":{\"format\":\"pcm\",\"sample_rate\":16000},"
                 "\"input\":{},\"task\":\"asr\",\"task_group\":\"audio\",\"function\":\"recognition\"}}",
                 (unsigned long)xTaskGetTickCount(), cfg.stt_model);
        esp_websocket_client_send_text(ctx->client, start, (int)strlen(start), 5000);
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
        send_audio(ctx);
    }
    else if (strstr(msg, "result-generated"))
    {
        copy_json_text(msg, ctx->text, ctx->text_cap);
    }
    else if (strstr(msg, "task-finished"))
    {
        xEventGroupSetBits(ctx->events, STT_FINISHED);
    }
    else if (strstr(msg, "task-failed"))
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
        .buffer_size = 4096,
        .network_timeout_ms = 15000,
        .task_stack = 8192,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    ctx.client = esp_websocket_client_init(&ws_cfg);
    if (!ctx.client) { vEventGroupDelete(events); return ESP_FAIL; }
    esp_websocket_register_events(ctx.client, WEBSOCKET_EVENT_ANY, websocket_event, &ctx);
    esp_err_t err = esp_websocket_client_start(ctx.client);
    if (err == ESP_OK)
    {
        EventBits_t bits = xEventGroupWaitBits(events, STT_FINISHED | STT_FAILED,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(60000));
        if (bits & STT_FAILED) err = ESP_FAIL;
        else if (!(bits & STT_FINISHED)) err = ESP_ERR_TIMEOUT;
    }
    esp_websocket_client_stop(ctx.client);
    esp_websocket_client_destroy(ctx.client);
    vEventGroupDelete(events);
    return err;
}
