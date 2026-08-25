/**
 * @file llm_role.c
 * @brief 独立角色对话实现：OpenAI 兼容 /v1/chat/completions（HTTPS，非流式）
 *
 * 任务模型：
 *   - 独立 worker 任务（懒创建），等待请求；HTTP/TLS/JSON 解析全部在该任务执行；
 *   - UI 回调/数据搬移经互斥锁保护（s_lock），回调里只存数据，绝不动 LVGL；
 *   - UI 线程通过 app_state + 周期轮询消费结果。
 *
 * 内存：
 *   - 请求体/响应体/历史消息全部放 PSRAM（MALLOC_CAP_SPIRAM），
 *     HTTP 任务栈保持小尺寸；仅锁与状态标志在内部 RAM。
 *   - 请求体用「带剩余容量检查的追加器」构建，杜绝越界写。
 */

#include "llm_role.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "network/net_config.h"
#include "network/net_wifi.h"

#define TAG "llm_role"

#define LLM_ROLE_TASK_STACK 8192
#define LLM_ROLE_TASK_PRIO 2

#define LLM_ROLE_MSG_MAX 4096            /* 单条消息文本上限（字节） */
#define LLM_ROLE_HIST_MAX 24             /* 历史消息条数上限 */
#define LLM_ROLE_REPLY_MAX 4096          /* 回复上限（字节） */
#define LLM_ROLE_DETAIL_MAX 128          /* 错误摘要上限 */
#define LLM_ROLE_HTTP_TIMEOUT_MS 60000   /* 整请求超时 */
#define LLM_ROLE_HTTP_BUFFER 16384       /* 响应缓冲 */

typedef struct {
    char *text;   /* 消息文本（PSRAM） */
    bool is_user;
} llm_role_msg_t;

/* 全局互斥：保护回调数据与忙碌状态（HTTP 任务与 UI 线程共享） */
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static volatile bool s_busy;
static volatile bool s_cancel;
static volatile bool s_clear_pending; /* 忙碌期间的清空请求，完成后执行 */
static llm_role_done_cb s_cb;
static void *s_ctx;

/* 待发送的用户文本（start_chat 写入，worker 读取；锁保护） */
static char s_pending_user[LLM_ROLE_MSG_MAX];

/* 结果缓冲（回调期间由 UI 通过锁读取） */
static char s_reply[LLM_ROLE_REPLY_MAX];
static char s_detail[LLM_ROLE_DETAIL_MAX];
static llm_role_result_t s_result;

/* 历史消息（PSRAM） */
static llm_role_msg_t *s_history;
static int s_hist_count;

/* ------------------------------------------------------------------ */
/* 内部工具                                                           */
/* ------------------------------------------------------------------ */

static bool lock_take(uint32_t ms)
{
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static void lock_give(void)
{
    xSemaphoreGive(s_lock);
}

/* 估算 Token：UTF-8 字节数保守估算 ceil(字节/3)（非精确值） */
static size_t est_tokens(const char *s)
{
    size_t bytes = strlen(s);
    return (bytes + 2) / 3;
}

/* 按 UTF-8 边界把 src 截断到 max_bytes 并写入 dst（cap 含 '\0'） */
static void truncate_utf8(char *dst, size_t cap, const char *src, size_t max_bytes)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len > max_bytes) len = max_bytes;
    while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    size_t copy = len < cap - 1 ? len : cap - 1;
    memcpy(dst, src, copy);
    dst[copy] = '\0';
}

static void set_result(llm_role_result_t r, const char *reply, const char *detail)
{
    if (!lock_take(100)) return;
    s_result = r;
    if (reply) truncate_utf8(s_reply, sizeof(s_reply), reply, sizeof(s_reply) - 1);
    else s_reply[0] = '\0';
    if (detail) truncate_utf8(s_detail, sizeof(s_detail), detail, sizeof(s_detail) - 1);
    else s_detail[0] = '\0';
    lock_give();
}

static void call_done_cb(void)
{
    if (!s_cb) return;
    s_cb(s_result, s_reply, s_detail, s_ctx);
}

/* ------------------------------------------------------------------ */
/* 历史消息管理（PSRAM）                                              */
/* ------------------------------------------------------------------ */

static void hist_free_all(void)
{
    if (!s_history) return;
    for (int i = 0; i < s_hist_count; i++)
    {
        if (s_history[i].text) free(s_history[i].text);
    }
    free(s_history);
    s_history = NULL;
    s_hist_count = 0;
}

static bool hist_ensure(int need)
{
    if (s_history && s_hist_count + need <= LLM_ROLE_HIST_MAX) return true;
    int new_cap = (s_hist_count + need) * 2;
    if (new_cap > LLM_ROLE_HIST_MAX) new_cap = LLM_ROLE_HIST_MAX;
    if (new_cap < 8) new_cap = 8;
    llm_role_msg_t *nh = heap_caps_realloc(s_history,
                                           (size_t)new_cap * sizeof(llm_role_msg_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nh) return false;
    s_history = nh;
    return true;
}

/* 追加一条消息（深拷贝文本到 PSRAM）；超上限时挤掉最早一条 */
static bool hist_add(const char *text, bool is_user)
{
    if (!text || !text[0]) return true;
    if (!hist_ensure(1)) return false;

    char *copy = heap_caps_malloc(LLM_ROLE_MSG_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return false;
    truncate_utf8(copy, LLM_ROLE_MSG_MAX, text, LLM_ROLE_MSG_MAX - 1);

    if (s_hist_count >= LLM_ROLE_HIST_MAX)
    {
        free(s_history[0].text);
        memmove(&s_history[0], &s_history[1],
                (size_t)(s_hist_count - 1) * sizeof(llm_role_msg_t));
        s_hist_count--;
    }
    s_history[s_hist_count].text = copy;
    s_history[s_hist_count].is_user = is_user;
    s_hist_count++;
    return true;
}

/* ------------------------------------------------------------------ */
/* 端点归一化：根地址 → /v1/chat/completions                           */
/* ------------------------------------------------------------------ */

static void build_endpoint(char *out, size_t cap, const char *base_url)
{
    static const char suffix[] = "/chat/completions";
    size_t len = strlen(base_url);
    if (len >= sizeof(suffix) - 1 && strcmp(base_url + len - (sizeof(suffix) - 1), suffix) == 0)
    {
        snprintf(out, cap, "%s", base_url);
        return;
    }
    if (len >= 3 && strcmp(base_url + len - 3, "/v1") == 0)
    {
        snprintf(out, cap, "%s%s", base_url, suffix);
        return;
    }
    snprintf(out, cap, "%s/v1%s", base_url, suffix);
}

/* ------------------------------------------------------------------ */
/* JSON 响应解析（仅用于服务端响应）                                   */
/* ------------------------------------------------------------------ */

/* 定位某字段的字符串值（不含嵌套/转义处理，用于扁平响应字段） */
static bool find_string_field(const char *json, const char *field, char *out, size_t cap)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", field);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    size_t o = 0;
    while (p[o] && p[o] != '"' && o + 1 < cap)
    {
        if (p[o] == '\\' && p[o + 1]) { o++; } /* 跳过转义字符本身 */
        out[o] = p[o];
        o++;
    }
    out[o] = '\0';
    return o > 0;
}

/* 提取 choices[0].message.content 文本 */
static bool parse_content(const char *json, char *out, size_t cap)
{
    out[0] = '\0';
    const char *p = strstr(json, "\"message\"");
    if (!p) return false;
    return find_string_field(p, "content", out, cap);
}

/* 非 2xx 错误摘要：优先取 error.message 的有限文本 */
static void extract_error_summary(const char *json, char *out, size_t cap)
{
    out[0] = '\0';
    if (!json) return;
    const char *p = strstr(json, "\"error\"");
    if (p)
    {
        char tmp[96] = "";
        if (find_string_field(p, "message", tmp, sizeof(tmp)) && tmp[0])
        {
            truncate_utf8(out, cap, tmp, cap - 1);
            return;
        }
    }
    /* 无 message 时取原始正文前若干字符（不含请求头/Key） */
    truncate_utf8(out, cap, json, cap - 1);
}

/* ------------------------------------------------------------------ */
/* 请求体构建（带容量检查的追加器）                                    */
/* ------------------------------------------------------------------ */

/* 追加格式化字符串；剩余容量不足返回 false（调用方立即中止） */
static bool body_appendf(char *buf, size_t cap, size_t *pos, const char *fmt, ...)
{
    if (*pos >= cap) return false;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    if ((size_t)n >= cap - *pos) return false; /* 截断即失败，避免半截 JSON */
    *pos += (size_t)n;
    return true;
}

/* 追加 JSON 字符串字段值（对 '"' 与 '\' 转义），容量不足返回 false */
static bool body_append_escaped(char *buf, size_t cap, size_t *pos, const char *s)
{
    for (const char *p = s; *p; p++)
    {
        char c = *p;
        if (c == '"' || c == '\\')
        {
            if (*pos + 2 >= cap) return false;
            buf[*pos] = '\\';
            buf[*pos + 1] = c;
            *pos += 2;
        }
        else
        {
            if (*pos + 1 >= cap) return false;
            buf[*pos] = c;
            *pos += 1;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* HTTP 事件回调（运行在 esp_http_client 上下文）                      */
/* ------------------------------------------------------------------ */

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} http_collect_t;

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    http_collect_t *c = evt->user_data;
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (c && evt->data && evt->data_len > 0 && c->len + (size_t)evt->data_len < c->cap)
        {
            memcpy(c->buf + c->len, evt->data, (size_t)evt->data_len);
            c->len += (size_t)evt->data_len;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
    case HTTP_EVENT_DISCONNECTED:
        if (c) c->buf[c->len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 核心请求流程（worker 任务内执行）                                   */
/* ------------------------------------------------------------------ */

/* 返回结果码；成功时 reply_out 写入回复文本 */
static llm_role_result_t do_request(const char *user_text, char *reply_out, size_t reply_cap)
{
    if (!net_wifi_is_connected()) return LLM_ROLE_ERR_WIFI;

    net_config_t cfg;
    net_config_load(&cfg);
    if (!net_config_has_llm(&cfg)) return LLM_ROLE_ERR_CONFIG;

    char endpoint[NET_CFG_LLM_URL_MAX + 32];
    build_endpoint(endpoint, sizeof(endpoint), cfg.llm.base_url);

    /* 请求体容量：上下文预算内的文本（token≈3 字节×2 转义）+ 结构开销 */
    size_t body_cap = (size_t)cfg.llm.context_tokens * 6 + 8192;
    if (body_cap > 128 * 1024) body_cap = 128 * 1024;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) return LLM_ROLE_ERR_NO_MEMORY;

    /* 预算：系统提示词 + 历史 + 本次文本 + 回复上限 <= context_tokens */
    size_t budget = (size_t)cfg.llm.context_tokens - (size_t)cfg.llm.max_tokens;
    size_t used = est_tokens(cfg.llm.role_prompt) + est_tokens(user_text);
    int start = 0;
    if (s_hist_count > 0)
    {
        while (start < s_hist_count)
        {
            used += est_tokens(s_history[start].text);
            if (used > budget)
            {
                /* 超预算：从最早一条开始删，保证剩余 <= budget */
                size_t u = est_tokens(cfg.llm.role_prompt) + est_tokens(user_text);
                start = 0;
                for (int i = 0; i < s_hist_count; i++)
                {
                    size_t add = est_tokens(s_history[i].text);
                    if (u + add > budget) break;
                    u += add;
                    start++;
                }
                break;
            }
            start++;
        }
    }

    size_t pos = 0;
    if (!body_appendf(body, body_cap, &pos, "{\"model\":\"%s\",\"temperature\":%.1f,\"max_tokens\":%u,"
                                        "\"stream\":false,\"messages\":[",
                      cfg.llm.model, cfg.llm.temperature, (unsigned)cfg.llm.max_tokens))
        goto fail;

    /* system 永远第一条 */
    if (!body_appendf(body, body_cap, &pos, "{\"role\":\"system\",\"content\":\""))
        goto fail;
    if (!body_append_escaped(body, body_cap, &pos, cfg.llm.role_prompt))
        goto fail;
    if (!body_appendf(body, body_cap, &pos, "\"}"))
        goto fail;

    /* 历史（跳过被裁剪的 start 之前的条目） */
    for (int i = start; i < s_hist_count; i++)
    {
        if (!body_appendf(body, body_cap, &pos, ",{\"role\":\"%s\",\"content\":\"",
                          s_history[i].is_user ? "user" : "assistant"))
            goto fail;
        if (!body_append_escaped(body, body_cap, &pos, s_history[i].text))
            goto fail;
        if (!body_appendf(body, body_cap, &pos, "\"}"))
            goto fail;
    }

    /* 本次用户文本 */
    if (!body_appendf(body, body_cap, &pos, ",{\"role\":\"user\",\"content\":\""))
        goto fail;
    if (!body_append_escaped(body, body_cap, &pos, user_text))
        goto fail;
    if (!body_appendf(body, body_cap, &pos, "\"}]}"))
        goto fail;

    /* ---- HTTP 请求（TLS + 证书验证） ---- */
    http_collect_t collect = {0};
    char *resp_buf = heap_caps_malloc(LLM_ROLE_HTTP_BUFFER, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!resp_buf)
    {
        free(body);
        return LLM_ROLE_ERR_NO_MEMORY;
    }
    collect.buf = resp_buf;
    collect.cap = LLM_ROLE_HTTP_BUFFER;

    esp_http_client_config_t http_cfg = {
        .url = endpoint,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event,
        .user_data = &collect,
        .timeout_ms = LLM_ROLE_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client)
    {
        free(body);
        free(resp_buf);
        return LLM_ROLE_ERR_NO_MEMORY;
    }

    /* Authorization 头：Key 只在本函数内使用，不打印 */
    char auth[NET_CFG_LLM_KEY_MAX + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg.llm.api_key);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_post_field(client, body, (int)pos);
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "http perform failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(body);
        free(resp_buf);
        return LLM_ROLE_ERR_NETWORK;
    }
    collect.buf[collect.len] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    if (status >= 200 && status < 300)
    {
        char reply[LLM_ROLE_REPLY_MAX];
        if (!parse_content(collect.buf, reply, sizeof(reply)))
        {
            ESP_LOGW(TAG, "no choices[0].message.content in response");
            free(resp_buf);
            return LLM_ROLE_ERR_PARSE;
        }
        free(resp_buf);

        /* 成功：写入 assistant 历史，返回回复文本 */
        hist_add(reply, false);
        truncate_utf8(reply_out, reply_cap, reply, reply_cap - 1);
        return LLM_ROLE_OK;
    }

    /* 非 2xx：提取有限错误摘要（不包含请求头/Key） */
    char detail[LLM_ROLE_DETAIL_MAX];
    extract_error_summary(collect.buf, detail, sizeof(detail));
    ESP_LOGW(TAG, "http status=%d detail=%s", status, detail);
    free(resp_buf);
    truncate_utf8(reply_out, reply_cap, detail, reply_cap - 1); /* 供 detail 使用 */
    return LLM_ROLE_ERR_HTTP;

fail:
    free(body);
    return LLM_ROLE_ERR_NO_MEMORY;
}

/* 把错误结果映射成 detail 文案（供 UI 展示） */
static void fill_error_detail(llm_role_result_t r, const char *extra)
{
    switch (r)
    {
    case LLM_ROLE_ERR_WIFI:     set_result(r, NULL, "未连接 Wi-Fi"); break;
    case LLM_ROLE_ERR_CONFIG:   set_result(r, NULL, "未配置 API Key 或模型"); break;
    case LLM_ROLE_ERR_NETWORK:  set_result(r, NULL, "网络错误，可重试"); break;
    case LLM_ROLE_ERR_PARSE:    set_result(r, NULL, "服务响应无法解析"); break;
    case LLM_ROLE_ERR_NO_MEMORY: set_result(r, NULL, "内存不足"); break;
    case LLM_ROLE_ERR_HTTP:
        /* extra 为服务端错误摘要（若为空则用通用文案） */
        set_result(r, NULL, extra && extra[0] ? extra : "服务返回错误，可重试");
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* worker 任务                                                        */
/* ------------------------------------------------------------------ */

static void llm_role_task(void *arg)
{
    (void)arg;
    while (1)
    {
        if (!lock_take(pdMS_TO_TICKS(50))) continue;
        bool has_job = s_busy;
        if (has_job)
        {
            char user_text[LLM_ROLE_MSG_MAX];
            snprintf(user_text, sizeof(user_text), "%s", s_pending_user);
            lock_give();

            s_cancel = false;
            char reply[LLM_ROLE_REPLY_MAX];
            char detail[LLM_ROLE_DETAIL_MAX];
            detail[0] = '\0';

            llm_role_result_t r = do_request(user_text, reply, sizeof(reply));
            if (s_cancel) r = LLM_ROLE_ERR_NETWORK;

            lock_take(pdMS_TO_TICKS(100));
            s_busy = false;
            lock_give();

            if (r == LLM_ROLE_OK)
            {
                set_result(LLM_ROLE_OK, reply, "");
            }
            else
            {
                if (r == LLM_ROLE_ERR_HTTP)
                    truncate_utf8(detail, sizeof(detail), reply, sizeof(detail) - 1);
                fill_error_detail(r, detail);
            }
            /* 请求期间若有清空请求（s_clear_pending），现在执行（无并发访问） */
            if (lock_take(pdMS_TO_TICKS(100)))
            {
                if (s_clear_pending)
                {
                    hist_free_all();
                    s_clear_pending = false;
                }
                lock_give();
            }
            call_done_cb();
        }
        else
        {
            lock_give();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                           */
/* ------------------------------------------------------------------ */

bool llm_role_is_ready(void)
{
    net_config_t cfg;
    net_config_load(&cfg);
    return net_config_has_llm(&cfg);
}

bool llm_role_is_busy(void)
{
    return s_busy;
}

esp_err_t llm_role_start_chat(const char *user_text, llm_role_done_cb cb, void *user_ctx)
{
    if (!user_text || !user_text[0]) return ESP_ERR_INVALID_ARG;
    if (!net_wifi_is_connected()) return (esp_err_t)LLM_ROLE_ERR_WIFI;
    net_config_t cfg;
    net_config_load(&cfg);
    if (!net_config_has_llm(&cfg)) return (esp_err_t)LLM_ROLE_ERR_CONFIG;

    if (s_lock == NULL)
    {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return (esp_err_t)LLM_ROLE_ERR_NO_MEMORY;
    }

    if (!lock_take(100)) return (esp_err_t)LLM_ROLE_ERR_BUSY;
    if (s_busy)
    {
        lock_give();
        return (esp_err_t)LLM_ROLE_ERR_BUSY;
    }

    /* 追加用户消息到历史（用于上下文），并暂存待发送文本 */
    if (!hist_add(user_text, true))
    {
        lock_give();
        return (esp_err_t)LLM_ROLE_ERR_NO_MEMORY;
    }
    truncate_utf8(s_pending_user, sizeof(s_pending_user), user_text, sizeof(s_pending_user) - 1);

    s_busy = true;
    s_cb = cb;
    s_ctx = user_ctx;
    s_result = LLM_ROLE_OK;
    s_reply[0] = '\0';
    s_detail[0] = '\0';
    lock_give();

    if (s_task == NULL)
    {
        if (xTaskCreate(llm_role_task, "llm_role", LLM_ROLE_TASK_STACK, NULL,
                        LLM_ROLE_TASK_PRIO, &s_task) != pdPASS)
        {
            /* 任务创建失败：回滚 */
            if (lock_take(100))
            {
                if (s_hist_count > 0) { free(s_history[s_hist_count - 1].text); s_hist_count--; }
                s_busy = false;
                lock_give();
            }
            return (esp_err_t)LLM_ROLE_ERR_NO_MEMORY;
        }
    }
    return ESP_OK;
}

void llm_role_cancel(void)
{
    s_cancel = true;
}

void llm_role_clear_history(void)
{
    if (!lock_take(100)) return;
    if (s_busy)
    {
        /* 请求进行中：延迟到当前请求结束后清空，避免与 worker 并发访问历史 */
        s_clear_pending = true;
        lock_give();
        return;
    }
    hist_free_all();
    lock_give();
}
