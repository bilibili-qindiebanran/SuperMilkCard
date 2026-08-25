/**
 * @file llm_role.h
 * @brief 独立角色对话：ESP32 直连 OpenAI 兼容 LLM（HTTPS，非流式）
 *
 * 职责：
 *   - 维护单请求会话（同一时间只允许一个 LLM 请求）；
 *   - 在独立 FreeRTOS 任务中执行 HTTP/TLS 请求与 JSON 解析，禁止阻塞 LVGL 任务；
 *   - 上下文裁剪：system 首条 + user/assistant 交替，按 UTF-8 字节估算 Token；
 *   - 完成后经回调通知调用方（回调内禁止操作 LVGL；UI 用 app_state + 轮询）。
 *
 * 安全：
 *   - API Key 只在本模块内用于 Authorization 头，绝不打印/回传；
 *   - 错误 detail 只包含服务端返回的有限错误摘要，不含请求头/Key。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LLM_ROLE_OK = 0,
    LLM_ROLE_ERR_CONFIG,   /* Base URL/Key/模型未配置 */
    LLM_ROLE_ERR_WIFI,     /* 未连接 Wi-Fi */
    LLM_ROLE_ERR_BUSY,     /* 已有请求进行中 */
    LLM_ROLE_ERR_NETWORK,  /* 网络/DNS/TLS 失败 */
    LLM_ROLE_ERR_HTTP,     /* 服务端非 2xx */
    LLM_ROLE_ERR_PARSE,    /* 响应非 JSON/缺 choices[0].message.content */
    LLM_ROLE_ERR_NO_MEMORY,/* PSRAM/堆内存不足 */
} llm_role_result_t;

/**
 * @brief 请求完成回调（在 llm_role 工作任务中调用；禁止直接操作 LVGL）。
 * @param result   结果码；LLM_ROLE_OK 时 reply 有效
 * @param reply    回复文本（仅 OK 时有效，UTF-8，指向 llm_role 内部缓冲）
 * @param detail   附加信息：非 OK 时为可读错误摘要（HTTP 状态码/有限正文）；可为空串
 * @param user_ctx start_chat 传入的 user_ctx
 */
typedef void (*llm_role_done_cb)(llm_role_result_t result, const char *reply,
                                 const char *detail, void *user_ctx);

/** @brief 是否已具备发起请求条件（Base URL + Key + 模型均配置） */
bool llm_role_is_ready(void);

/** @brief 是否有请求进行中 */
bool llm_role_is_busy(void);

/**
 * @brief 发起一次角色对话（异步，立即返回）。
 * @param user_text 用户文本（UTF-8，非空）
 * @return ESP_OK 已开始；LLM_ROLE_ERR_* 对应错误（busy/config/wifi 等）
 */
esp_err_t llm_role_start_chat(const char *user_text, llm_role_done_cb cb, void *user_ctx);

/** @brief 取消当前请求（回调会以 LLM_ROLE_ERR_NETWORK 结束；没有进行中则无操作） */
void llm_role_cancel(void);

/** @brief 清空上下文历史（对话页「清空记忆」用） */
void llm_role_clear_history(void);

#ifdef __cplusplus
}
#endif
