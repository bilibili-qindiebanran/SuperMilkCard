/**
 * @file net_wifi.h
 * @brief Wi-Fi 管理：STA 连接（NVS 持久化）+ SoftAP 配网模式切换
 *
 * 状态机：
 *   - 首次启动（无已保存网络）→ 自动进入 SoftAP 配网（APSTA，SSID=SuperMilkCard 无密码）
 *   - 已保存网络 → STA 自动连接；连续失败（≥3 次）→ 自动切 SoftAP 配网
 *   - 设置页可随时 net_wifi_start_provisioning() 手动开启配网
 *   - 配网页保存后 net_wifi_request_connect() 切回 STA 连接
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化并启动 Wi-Fi（net_config_init 需已调用）；返回 ESP_OK 且后台自动连接 */
esp_err_t net_wifi_start(void);

/** @brief 是否处于 SoftAP 配网模式（UI 显示热点名/提示用） */
bool net_wifi_is_provisioning(void);

/** @brief 手动开启 SoftAP 配网（设置页入口；幂等，已开启则无操作） */
esp_err_t net_wifi_start_provisioning(void);

/**
 * @brief 配网页保存后调用：持久化配置并切回 STA 连接
 * @note ssid/pass 仅写入 NVS，不打印；host/port/name 为电脑连接参数
 */
esp_err_t net_wifi_request_connect(const char *ssid, const char *pass,
                                   const char *host, uint16_t tcp_port,
                                   const char *name);

/** @brief 忘记网络：清空凭据并回到配网模式 */
esp_err_t net_wifi_forget(void);

/** @brief 当前 STA 是否已连上并获得 IP（供 TCP 服务端判断是否可启动） */
bool net_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
