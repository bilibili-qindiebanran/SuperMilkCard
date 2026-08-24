/**
 * @file net_portal.h
 * @brief SoftAP 配网页（ESP-IDF HTTP Server + 内嵌精简 HTML/CSS/JS）
 *
 * 由 net_wifi 在进入配网模式（WIFI_EVENT_AP_START）时调用。
 * 页面提供：Wi-Fi SSID/密码、电脑地址/端口、设备名、扫描、保存、忘记网络、状态查看。
 * 不提供共享令牌字段；任何密钥不出现在页面回显或日志。
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 启动配网 HTTP 服务（幂等） */
esp_err_t net_portal_start(void);

/** @brief 停止配网 HTTP 服务（幂等） */
void net_portal_stop(void);

/** @brief 配网服务是否正在运行 */
bool net_portal_is_running(void);

#ifdef __cplusplus
}
#endif
