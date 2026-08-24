/**
 * @file net_tcp.h
 * @brief TCP 业务服务端（AA55 二进制帧协议）+ UDP 设备发现广播
 *
 * 方向：ESP32 为 TCP 服务端，Windows 为客户端（沿用 ESP32/接口文档.md）。
 *   - 连接建立后立即发送 HELLO 帧（携带设备识别码）
 *   - 只允许一个已握手客户端；有新连接且已有客户端时直接关闭新连接
 *   - 解析 TEXT 帧内的 live2d_state / chat，经 app_state 发布给 UI
 *   - 提供 send_live2d_command 供 UI 页面发送 enter / return_home
 *   - 周期性 UDP 广播设备发现信息（端口 4210）
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 启动 TCP 服务端 + 发现广播（创建独立任务；幂等） */
esp_err_t net_tcp_start(void);

/** @brief 是否已有已连接客户端 */
bool net_tcp_is_client_connected(void);

/**
 * @brief 发送一帧 TEXT（UTF-8 JSON），任意任务可调用（内部加锁、非阻塞）
 * @return ESP_OK 已发送；否则未连接或发送失败
 */
esp_err_t net_tcp_send_json(const char *json);

/** @brief 发送 live2d_command 命令（enter / return_home / reconnect） */
esp_err_t net_tcp_send_live2d_command(const char *command);

/** @brief 发送一帧二进制 AUDIO（0x03）到客户端；未连接返回 ESP_ERR_NOT_FOUND */
esp_err_t net_tcp_send_audio(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif
