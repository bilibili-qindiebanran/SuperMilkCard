/**
 * @file net_app.c
 * @brief 网络应用入口实现
 */

#include "net_app.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "net_config.h"
#include "net_tcp.h"
#include "net_wifi.h"

static const char *TAG = "net_app";

esp_err_t net_app_start(void)
{
    /* 先初始化 lwIP 网络栈与事件循环：
     * esp_netif_init() 创建 tcpip 线程，TCP/UDP socket 才能安全创建；
     * 必须在 net_tcp_start()（会立即创建 socket）之前完成。 */
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return err;
    }

    err = net_config_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "net_config_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* TCP 服务端任务先启动：无论 STA/AP 都能监听（AP 网段也可直连调试） */
    err = net_tcp_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "net_tcp_start failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Wi-Fi：无保存网络自动进入 SoftAP 配网；有则 STA 自动连接 */
    err = net_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "net_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "network started (device id=%s)", net_config_device_id());
    return ESP_OK;
}
