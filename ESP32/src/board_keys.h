/**
 * @file board_keys.h
 * @brief 实体按键模块：GPIO38（返回/上一页）+ GPIO4（确认/下一项）
 *
 * 硬件：外部 10k 上拉，按下下拉=低电平。
 * 首版实现消抖 + 短按事件；长按行为预留。
 *
 * GPIO38 → 返回（LV_KEY_ESC）
 * GPIO4   → 确认/下一项（LV_KEY_ENTER）
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 按键定义（platformio.ini 已定义，可覆盖） */
#ifndef BOARD_KEY_BACK_GPIO
#define BOARD_KEY_BACK_GPIO 38 /* GPIO38 = 返回 */
#endif
#ifndef BOARD_KEY_OK_GPIO
#define BOARD_KEY_OK_GPIO 4    /* GPIO4 = 确认/下一项 */
#endif

/* 按键事件 */
typedef enum {
    BOARD_KEY_NONE = 0,
    BOARD_KEY_BACK,  /* 返回键短按 */
    BOARD_KEY_OK,    /* 确认键短按 */
} board_key_event_t;

/**
 * @brief 初始化按键 GPIO（输入，外部上拉）
 */
esp_err_t board_keys_init(void);

/**
 * @brief 非阻塞轮询按键状态（带消抖，短按触发事件）
 *
 * 应周期调用（如 LVGL 输入轮询中，10ms 周期）。
 *
 * @return 检测到的按键事件；无事件返回 BOARD_KEY_NONE
 */
board_key_event_t board_keys_scan(void);

#ifdef __cplusplus
}
#endif
