/**
 * @file net_config.h
 * @brief 网络配置持久化（NVS）：Wi-Fi 凭据、电脑地址/端口、设备名
 *
 * 安全约定：
 *   - Wi-Fi 密码只写入 NVS 并仅用于 STA 自动重连，绝不打印日志/网页回显；
 *   - 配网页查询状态接口不返回密码字段。
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

#define NET_CFG_AP_SSID "SuperMilkCard" /* 配网热点名（无密码开放） */
#define NET_CFG_DEFAULT_TCP_PORT 9000
#define NET_CFG_DEFAULT_NAME "奶片助手"

typedef struct {
    char ssid[NET_CFG_SSID_MAX];
    char pass[NET_CFG_PASS_MAX];
    char host[NET_CFG_HOST_MAX];   /* Windows 电脑 IP/主机名 */
    uint16_t tcp_port;             /* TCP 业务端口 */
    char name[NET_CFG_NAME_MAX];   /* 设备显示名 */
    bool has_ssid;                 /* 是否已保存过网络 */
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

/** @brief 设备识别码（基于 MAC，确定性生成；返回内部静态缓冲区） */
const char *net_config_device_id(void);

#ifdef __cplusplus
}
#endif
