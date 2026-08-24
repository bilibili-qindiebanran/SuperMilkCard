/**
 * @file net_app.h
 * @brief 网络应用入口：NVS + Wi-Fi + 配网 + TCP 服务端 的装配
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化并启动全部网络功能（app_main 中调用一次） */
esp_err_t net_app_start(void);

#ifdef __cplusplus
}
#endif
