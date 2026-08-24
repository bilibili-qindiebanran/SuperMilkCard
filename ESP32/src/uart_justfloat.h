/**
 * @file uart_justfloat.h
 * @brief UART0 (GPIO43=TX / GPIO44=RX) JustFloat 协议输出
 *
 * 设计：JustFloat 二进制数据走 UART0（接 USB 转串口 → VOFA+），
 *       日志继续走 USB-Serial/JTAG console，互不干扰。
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef JUSTFLOAT_UART_NUM
#define JUSTFLOAT_UART_NUM 0 /* UART0 */
#endif
#ifndef JUSTFLOAT_UART_TX
#define JUSTFLOAT_UART_TX 43 /* GPIO43 = UART0 TX */
#endif
#ifndef JUSTFLOAT_UART_RX
#define JUSTFLOAT_UART_RX 44 /* GPIO44 = UART0 RX */
#endif
#ifndef JUSTFLOAT_UART_BAUD
#define JUSTFLOAT_UART_BAUD 115200
#endif

/**
 * @brief 初始化 UART0 用于 JustFloat 输出
 * @return ESP_OK 成功
 */
esp_err_t uart_justfloat_init(void);

/**
 * @brief 启用/禁用 JustFloat 输出（默认禁用，便于把 UART0 让给调试日志）
 * @param enabled true=发送 JustFloat 帧；false=静默（不占用总线）
 */
void uart_justfloat_set_enabled(bool enabled);

/**
 * @brief 发送一帧 JustFloat 数据（N 个 float + 帧尾）
 * @param data  float 数组
 * @param count float 个数
 */
void uart_justfloat_send(const float *data, size_t count);

#ifdef __cplusplus
}
#endif
