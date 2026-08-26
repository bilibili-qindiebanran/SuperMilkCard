/**
 * @file net_config.c
 * @brief 网络配置 NVS 持久化实现
 */

#include "net_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define NET_CFG_NS "netcfg"
#define KEY_SSID "ssid"
#define KEY_PASS "pass"
#define KEY_HOST "host"
#define KEY_PORT "port"
#define KEY_NAME "name"
#define KEY_STT_URL "stt_url"
#define KEY_STT_KEY "stt_key"
#define KEY_STT_MODEL "stt_model"
#define KEY_LLM_URL "llm_url"
#define KEY_LLM_KEY "llm_key"
#define KEY_LLM_MODEL "llm_model"
#define KEY_LLM_TEMP "llm_temp"   /* u16：温度×1000 */
#define KEY_LLM_MAX "llm_max"
#define KEY_LLM_CTX "llm_ctx"
#define KEY_ROLE_PROMPT "role_prompt"

/* 按 UTF-8 字节截断，不切断多字节字符（末尾补 '\0'） */
static void truncate_utf8(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len > cap - 1) len = cap - 1;
    while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* 角色提示词预设默认值 */
static void llm_defaults(net_llm_config_t *llm)
{
    memset(llm, 0, sizeof(*llm));
    llm->temperature = NET_CFG_LLM_DEFAULT_TEMP;
    llm->max_tokens = NET_CFG_LLM_DEFAULT_MAX_TOKENS;
    llm->context_tokens = NET_CFG_LLM_DEFAULT_CONTEXT_TOKENS;
    truncate_utf8(llm->role_prompt, sizeof(llm->role_prompt), NET_CFG_ROLE_PROMPT_DEFAULT);
}

static bool s_nvs_ready;

esp_err_t net_config_init(void)
{
    if (s_nvs_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    s_nvs_ready = true;
    return ESP_OK;
}

static esp_err_t get_str(nvs_handle_t h, const char *key, char *out, size_t size)
{
    size_t len = size;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) { out[0] = '\0'; return ESP_OK; }
    return err;
}

esp_err_t net_config_load(net_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->tcp_port = NET_CFG_DEFAULT_TCP_PORT;
    snprintf(cfg->name, sizeof(cfg->name), "%s", NET_CFG_DEFAULT_NAME);
    snprintf(cfg->stt_url, sizeof(cfg->stt_url), "%s", "wss://dashscope.aliyuncs.com/api-ws/v1/inference");
    snprintf(cfg->stt_model, sizeof(cfg->stt_model), "%s", "qwen-audio-3.0-asr-flash-streaming");
    llm_defaults(&cfg->llm);

    if (!s_nvs_ready) return ESP_OK;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    if (nvs_get_u16(h, KEY_PORT, &cfg->tcp_port) != ESP_OK)
        cfg->tcp_port = NET_CFG_DEFAULT_TCP_PORT;

    get_str(h, KEY_SSID, cfg->ssid, sizeof(cfg->ssid));
    get_str(h, KEY_PASS, cfg->pass, sizeof(cfg->pass));
    get_str(h, KEY_HOST, cfg->host, sizeof(cfg->host));
    get_str(h, KEY_NAME, cfg->name, sizeof(cfg->name));
    get_str(h, KEY_STT_URL, cfg->stt_url, sizeof(cfg->stt_url));
    get_str(h, KEY_STT_KEY, cfg->stt_api_key, sizeof(cfg->stt_api_key));
    get_str(h, KEY_STT_MODEL, cfg->stt_model, sizeof(cfg->stt_model));
    if (!cfg->stt_url[0]) snprintf(cfg->stt_url, sizeof(cfg->stt_url), "%s", "wss://dashscope.aliyuncs.com/api-ws/v1/inference");
    if (!cfg->stt_model[0]) snprintf(cfg->stt_model, sizeof(cfg->stt_model), "%s", "qwen-audio-3.0-asr-flash-streaming");

    /* 独立角色 LLM 配置 */
    get_str(h, KEY_LLM_URL, cfg->llm.base_url, sizeof(cfg->llm.base_url));
    get_str(h, KEY_LLM_KEY, cfg->llm.api_key, sizeof(cfg->llm.api_key));
    get_str(h, KEY_LLM_MODEL, cfg->llm.model, sizeof(cfg->llm.model));
    uint16_t temp_x1000 = 0;
    if (nvs_get_u16(h, KEY_LLM_TEMP, &temp_x1000) == ESP_OK)
        cfg->llm.temperature = (float)temp_x1000 / 1000.0f;
    uint16_t v = 0;
    if (nvs_get_u16(h, KEY_LLM_MAX, &v) == ESP_OK) cfg->llm.max_tokens = v;
    v = 0;
    if (nvs_get_u16(h, KEY_LLM_CTX, &v) == ESP_OK) cfg->llm.context_tokens = v;
    get_str(h, KEY_ROLE_PROMPT, cfg->llm.role_prompt, sizeof(cfg->llm.role_prompt));
    if (!cfg->llm.role_prompt[0])
        truncate_utf8(cfg->llm.role_prompt, sizeof(cfg->llm.role_prompt), NET_CFG_ROLE_PROMPT_DEFAULT);

    cfg->has_ssid = cfg->ssid[0] != '\0';

    nvs_close(h);
    return ESP_OK;
}

esp_err_t net_config_save(const net_config_t *cfg)
{
    if (!s_nvs_ready) return ESP_ERR_INVALID_STATE;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_str(h, KEY_SSID, cfg->ssid);
    nvs_set_str(h, KEY_PASS, cfg->pass);
    nvs_set_str(h, KEY_HOST, cfg->host);
    nvs_set_str(h, KEY_NAME, cfg->name[0] ? cfg->name : NET_CFG_DEFAULT_NAME);
    nvs_set_u16(h, KEY_PORT, cfg->tcp_port);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t net_config_save_stt(const char *url, const char *api_key, const char *model)
{
    if (!s_nvs_ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    if (url) nvs_set_str(h, KEY_STT_URL, url);
    if (api_key) nvs_set_str(h, KEY_STT_KEY, api_key);
    if (model) nvs_set_str(h, KEY_STT_MODEL, model);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool net_config_has_llm(const net_config_t *cfg)
{
    if (!cfg) return false;
    return cfg->llm.base_url[0] != '\0' &&
           cfg->llm.api_key[0] != '\0' &&
           cfg->llm.model[0] != '\0';
}

esp_err_t net_config_save_llm(const net_llm_config_t *llm)
{
    if (!s_nvs_ready || !llm) return ESP_ERR_INVALID_STATE;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_str(h, KEY_LLM_URL, llm->base_url);
    nvs_set_str(h, KEY_LLM_MODEL, llm->model);
    uint16_t temp_x1000 = (uint16_t)(llm->temperature * 1000.0f + 0.5f);
    nvs_set_u16(h, KEY_LLM_TEMP, temp_x1000);
    nvs_set_u16(h, KEY_LLM_MAX, llm->max_tokens);
    nvs_set_u16(h, KEY_LLM_CTX, llm->context_tokens);
    if (llm->role_prompt[0])
        nvs_set_str(h, KEY_ROLE_PROMPT, llm->role_prompt);
    /* 注意：不写 KEY_LLM_KEY——Key 只经 net_config_set_llm_key() 单独保存 */

    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t net_config_save_role_prompt(const char *prompt)
{
    if (!s_nvs_ready) return ESP_ERR_INVALID_STATE;

    char buf[NET_CFG_ROLE_PROMPT_MAX];
    truncate_utf8(buf, sizeof(buf), prompt);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, KEY_ROLE_PROMPT, buf);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t net_config_set_llm_key(const char *api_key)
{
    if (!s_nvs_ready || !api_key) return ESP_ERR_INVALID_STATE;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, KEY_LLM_KEY, api_key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t net_config_clear_llm_key(void)
{
    if (!s_nvs_ready) return ESP_ERR_INVALID_STATE;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, KEY_LLM_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
bool net_config_load_and_has_ssid(void)
{
    if (!s_nvs_ready) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t ssid_len = 0;
    err = nvs_get_str(handle, KEY_SSID, NULL, &ssid_len);
    nvs_close(handle);
    return err == ESP_OK && ssid_len > 1;
}

esp_err_t net_config_forget(void)
{
    if (!s_nvs_ready) return ESP_ERR_INVALID_STATE;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NET_CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_erase_key(h, KEY_SSID);
    nvs_erase_key(h, KEY_PASS);
    nvs_erase_key(h, KEY_HOST);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

const char *net_config_device_id(void)
{
    static char s_id[NET_CFG_DEVICE_ID_MAX];
    static bool s_done;

    if (!s_done)
    {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_id, sizeof(s_id), "esp32_%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        s_done = true;
    }
    return s_id;
}
