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
bool net_config_load_and_has_ssid(void)
{
    net_config_t cfg;
    return net_config_load(&cfg) == ESP_OK && cfg.has_ssid;
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
