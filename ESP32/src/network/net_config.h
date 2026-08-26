/**
 * @file net_config.h
 * @brief 网络配置持久化（NVS）：Wi-Fi 凭据、电脑地址/端口、设备名、STT、独立角色 LLM
 *
 * 安全约定：
 *   - Wi-Fi 密码、STT API Key、LLM API Key 只写入 NVS，绝不打印日志/网页回显；
 *   - 配网页查询状态接口只返回掩码/布尔状态，不返回任何 Key 明文；
 *   - LLM Key 单独提供设置/清除接口，不随 /api/llm 回传。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NET_CFG_SSID_MAX 33   /* 含 '\0' */
#define NET_CFG_PASS_MAX 65
#define NET_CFG_HOST_MAX 65
#define NET_CFG_NAME_MAX 33
#define NET_CFG_DEVICE_ID_MAX 24
#define NET_CFG_STT_URL_MAX 160
#define NET_CFG_STT_KEY_MAX 256
#define NET_CFG_STT_MODEL_MAX 80

/* 独立角色 LLM 配置上限（含 '\0'） */
#define NET_CFG_LLM_URL_MAX 192
#define NET_CFG_LLM_KEY_MAX 256
#define NET_CFG_LLM_MODEL_MAX 96
#define NET_CFG_ROLE_PROMPT_MAX 1536

#define NET_CFG_AP_SSID "SuperMilkCard" /* 配网热点名（无密码开放） */
#define NET_CFG_DEFAULT_TCP_PORT 9000
#define NET_CFG_DEFAULT_NAME "奶片助手"

/* LLM 默认值（与接口文档/配网页一致） */
#define NET_CFG_LLM_DEFAULT_TEMP 0.8f
#define NET_CFG_LLM_DEFAULT_MAX_TOKENS 1024
#define NET_CFG_LLM_DEFAULT_CONTEXT_TOKENS 8000

/* 预设中性角色提示词（可经配网页/API 覆盖） */
#define NET_CFG_ROLE_PROMPT_DEFAULT \
    "你是奶片助手，是用户随身设备上的 AI 角色。请用友好、简洁的方式回答，" \
    "优先使用中文。不要编造设备能力或承诺无法实现的功能。"

typedef struct {
    char base_url[NET_CFG_LLM_URL_MAX];     /* OpenAI 兼容服务根地址（仅 https） */
    char api_key[NET_CFG_LLM_KEY_MAX];      /* API Key，仅存 NVS，不回显 */
    char model[NET_CFG_LLM_MODEL_MAX];      /* 模型名 */
    float temperature;                      /* 0.0–2.0，默认 0.8 */
    uint16_t max_tokens;                    /* 64–2048，默认 1024 */
    uint16_t context_tokens;                /* 512–12000，默认 8000 */
    char role_prompt[NET_CFG_ROLE_PROMPT_MAX]; /* 角色人设（UTF-8） */
} net_llm_config_t;

typedef struct {
    char ssid[NET_CFG_SSID_MAX];
    char pass[NET_CFG_PASS_MAX];
    char host[NET_CFG_HOST_MAX];   /* Windows 电脑 IP/主机名 */
    uint16_t tcp_port;             /* TCP 业务端口 */
    char name[NET_CFG_NAME_MAX];   /* 设备显示名 */
    bool has_ssid;                 /* 是否已保存过网络 */
    char stt_url[NET_CFG_STT_URL_MAX];
    char stt_api_key[NET_CFG_STT_KEY_MAX];
    char stt_model[NET_CFG_STT_MODEL_MAX];
    net_llm_config_t llm;          /* 独立角色 LLM 配置 */
} net_config_t;

/**
 * @brief 初始化 NVS（首次调用时 flash 初始化 + 打开命名空间）
 * @note 幂等；网络模块启动前调用一次即可
 */
esp_err_t net_config_init(void);

/** @brief 读取配置到 cfg（未配置过的字段取默认值） */
esp_err_t net_config_load(net_config_t *cfg);

/** @brief 便捷判断：是否已保存过 Wi-Fi 网络（加载一次，不关心其他字段） */
bool net_config_load_and_has_ssid(void);

/** @brief 保存配置（含 Wi-Fi 凭据） */
esp_err_t net_config_save(const net_config_t *cfg);

/** @brief 忘记网络：清空 SSID/密码/主机（保留设备名与端口默认值） */
esp_err_t net_config_forget(void);

/** @brief 保存/读取 ESP32 直连 STT 配置 */
esp_err_t net_config_save_stt(const char *url, const char *api_key, const char *model);

/** @brief 便捷判断：是否已具备发起独立角色对话的条件（Base URL + Key + 模型） */
bool net_config_has_llm(const net_config_t *cfg);

/**
 * @brief 保存独立角色 LLM 配置（除 Key 外的全部字段）。
 * @note api_key 字段被忽略：Key 只经 net_config_set_llm_key() 单独写入，
 *       避免普通配置保存意外覆盖或清空 Key。
 */
esp_err_t net_config_save_llm(const net_llm_config_t *llm);

/** @brief 单独保存角色提示词（UTF-8，按字节截断不切断多字节字符） */
esp_err_t net_config_save_role_prompt(const char *prompt);

/** @brief 设置/更新 LLM API Key（单独写 NVS，不参与 /api/llm 保存） */
esp_err_t net_config_set_llm_key(const char *api_key);

/** @brief 清除 LLM API Key（清除后 net_config_has_llm() 返回 false） */
esp_err_t net_config_clear_llm_key(void);

/** @brief 设备识别码（基于 MAC，确定性生成；返回内部静态缓冲区） */
const char *net_config_device_id(void);

#ifdef __cplusplus
}
#endif
