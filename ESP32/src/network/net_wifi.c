/**
 * @file net_wifi.c
 * @brief Wi-Fi 管理实现（ESP-IDF 6.0.1：AP 切换用 stop/set_mode/start 方式）
 */

#include "net_wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../ui/app_state.h"
#include "net_config.h"
#include "net_portal.h"

#define NET_WIFI_MAX_RETRY 3 /* 连续失败次数，超过则进入配网 */

static const char *TAG = "net_wifi";

static bool s_started;
static bool s_provisioning;      /* SoftAP 配网模式 */
static int s_fail_count;
static esp_netif_t *s_netif_sta;
static esp_netif_t *s_netif_ap;
static bool s_sta_ip;            /* 是否已获得 IP */

/* 广播 IP（已连接时用于 Wi-Fi 状态发布；避免依赖全局） */
static bool s_sta_connected;

void net_wifi_handle_sta_connected(void);
void net_wifi_handle_sta_disconnected(void);

/* ------------------------------------------------------------------ */
/* 事件处理                                                           */
/* ------------------------------------------------------------------ */

static void publish_conn_state(void)
{
    app_conn_state_t wifi = APP_CONN_OFF;
    if (s_started) wifi = s_sta_connected ? APP_CONN_OK : APP_CONN_DISCONNECTED;
    /* uart 未接入首期；win 状态由 TCP 服务端维护，这里不动 */
    app_state_publish_conn(wifi, APP_CONN_OFF, app_state_get()->conn.win);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch (id)
    {
    case WIFI_EVENT_STA_START:
        if (!s_provisioning && net_config_load_and_has_ssid())
        {
            esp_wifi_connect();
        }
        break;

    case WIFI_EVENT_STA_CONNECTED:
        s_fail_count = 0;
        s_sta_connected = true;
        ESP_LOGI(TAG, "STA connected to AP");
        publish_conn_state();
        break;

    case WIFI_EVENT_STA_DISCONNECTED:
        s_sta_connected = false;
        s_sta_ip = false;
        publish_conn_state();
        if (s_provisioning) break; /* 配网模式下不自动重连 */

        {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "STA disconnected: %d (fail=%d)", d->reason, s_fail_count + 1);
        }
        s_fail_count++;
        if (s_fail_count >= NET_WIFI_MAX_RETRY)
        {
            ESP_LOGW(TAG, "connect failed %d times, entering provisioning", s_fail_count);
            net_wifi_start_provisioning();
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
        }
        break;

    case WIFI_EVENT_AP_START:
        s_provisioning = true;
        s_sta_connected = false;
        ESP_LOGI(TAG, "SoftAP started: %s (open)", NET_CFG_AP_SSID);
        publish_conn_state();
        net_portal_start(); /* 配网模式打开配网页 */
        break;

    case WIFI_EVENT_AP_STOP:
        net_portal_stop();
        break;

    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_sta_ip = true;
        s_sta_connected = true;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        publish_conn_state();
    }
    else if (id == IP_EVENT_STA_LOST_IP)
    {
        s_sta_ip = false;
        s_sta_connected = false;
        publish_conn_state();
    }
}

/* ------------------------------------------------------------------ */
/* 模式切换                                                           */
/* ------------------------------------------------------------------ */

static esp_err_t apply_mode(wifi_mode_t mode, bool with_ap_config)
{
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) return err;

    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) return err;

    if (with_ap_config)
    {
        wifi_config_t ap_cfg = {0};
        strncpy((char *)ap_cfg.ap.ssid, NET_CFG_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
        ap_cfg.ap.ssid_len = (uint8_t)strlen(NET_CFG_AP_SSID);
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN; /* 无密码开放热点（产品决策） */
        ap_cfg.ap.max_connection = 4;
        ap_cfg.ap.channel = 1;
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        if (err != ESP_OK) return err;
    }
    else
    {
        /* 恢复 STA 配置（从 NVS 读最新） */
        net_config_t cfg;
        net_config_load(&cfg);
        wifi_config_t sta_cfg = {0};
        if (cfg.has_ssid)
        {
            strncpy((char *)sta_cfg.sta.ssid, cfg.ssid, sizeof(sta_cfg.sta.ssid) - 1);
            strncpy((char *)sta_cfg.sta.password, cfg.pass, sizeof(sta_cfg.sta.password) - 1);
        }
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        if (err != ESP_OK) return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    if (mode == WIFI_MODE_STA)
    {
        s_provisioning = false;
        s_fail_count = 0;
        if (net_config_load_and_has_ssid()) esp_wifi_connect();
        publish_conn_state();
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                           */
/* ------------------------------------------------------------------ */

esp_err_t net_wifi_start(void)
{
    if (s_started) return ESP_OK;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    /* 两个 netif 都创建：模式切换不需要重建 */
    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap = esp_netif_create_default_wifi_ap();
    if (s_netif_sta == NULL || s_netif_ap == NULL) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL);
    if (err != ESP_OK) return err;

    s_started = true;

    net_config_t nc;
    net_config_load(&nc);
    if (nc.has_ssid)
    {
        ESP_LOGI(TAG, "saved network found, connecting STA (ssid=%s)", nc.ssid);
        return apply_mode(WIFI_MODE_STA, false);
    }
    ESP_LOGI(TAG, "no saved network, entering provisioning");
    return apply_mode(WIFI_MODE_APSTA, true);
}

bool net_wifi_is_provisioning(void)
{
    return s_provisioning;
}

esp_err_t net_wifi_start_provisioning(void)
{
    if (s_provisioning) return ESP_OK;
    ESP_LOGI(TAG, "manual provisioning start");
    return apply_mode(WIFI_MODE_APSTA, true);
}

esp_err_t net_wifi_request_connect(const char *ssid, const char *pass,
                                   const char *host, uint16_t tcp_port,
                                   const char *name)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;

    net_config_t cfg;
    net_config_load(&cfg);
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
    if (pass) strncpy(cfg.pass, pass, sizeof(cfg.pass) - 1);
    else cfg.pass[0] = '\0';
    cfg.pass[sizeof(cfg.pass) - 1] = '\0';
    if (host) strncpy(cfg.host, host, sizeof(cfg.host) - 1);
    cfg.host[sizeof(cfg.host) - 1] = '\0';
    cfg.tcp_port = tcp_port ? tcp_port : NET_CFG_DEFAULT_TCP_PORT;
    if (name && name[0]) strncpy(cfg.name, name, sizeof(cfg.name) - 1);
    cfg.name[sizeof(cfg.name) - 1] = '\0';
    cfg.has_ssid = true;

    esp_err_t err = net_config_save(&cfg);
    if (err != ESP_OK) return err;

    /* 从配网模式切回 STA 连接 */
    if (s_provisioning) return apply_mode(WIFI_MODE_STA, false);
    return esp_wifi_connect();
}

esp_err_t net_wifi_forget(void)
{
    esp_err_t err = net_config_forget();
    if (err != ESP_OK) return err;
    if (s_provisioning) return ESP_OK;
    return apply_mode(WIFI_MODE_APSTA, true);
}

bool net_wifi_is_connected(void)
{
    return s_sta_connected && s_sta_ip;
}
