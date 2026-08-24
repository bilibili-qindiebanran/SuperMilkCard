/**
 * @file net_app.c
 * @brief 网络应用入口实现
 */

#include "net_app.h"

#include "esp_log.h"

#include "net_config.h"
#include "net_tcp.h"
#include "net_wifi.h"

static const char *TAG = "net_app";

esp_err_t net_app_start(void)
{
    esp_err_t err = net_config_init();
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
