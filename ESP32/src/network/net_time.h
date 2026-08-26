/**
 * @file net_time.h
 * @brief 系统时间同步（SNTP）接口
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动系统时间同步
 *
 * 创建两个任务：
 *  - local_clock：晶振近似走时，保证首页时钟立即有值
 *  - sntp_sync：等待 Wi-Fi 就绪后发起 SNTP 同步，成功后校准系统时间
 *
 * @return ESP_OK 成功；ESP_ERR_NO_MEM 任务创建失败
 */
esp_err_t net_time_init(void);

/**
 * @brief 网络获得 IP 时通知时间模块（由 net_wifi 事件回调调用）
 *
 * 用于 SNTP 首次等待超时或网络重连后拉起同步重试。
 */
void net_time_on_ip_got(void);

#ifdef __cplusplus
}
#endif
