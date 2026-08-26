/**
 * @file board_keys.h
 * @brief 实体按键模块：GPIO38（返回/上一页）+ GPIO4（确认/下一项）
 *
 * 硬件：外部 10k 上拉，按下下拉=低电平。
 * 支持消抖 + 短按 + 长按（长按阈值 800ms）。
 *
 * GPIO38 → 返回（LV_KEY_ESC）/ 长按返回桌面
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

/* 长按阈值（ms）：按下持续超过该时长触发长按事件 */
#ifndef BOARD_KEY_LONG_PRESS_MS
#define BOARD_KEY_LONG_PRESS_MS 800
#endif

/* 按键事件 */
typedef enum {
    BOARD_KEY_NONE = 0,
    BOARD_KEY_BACK,      /* 返回键短按 */
    BOARD_KEY_OK,        /* 确认键短按 */
    BOARD_KEY_BACK_LONG, /* 返回键长按 */
    BOARD_KEY_OK_LONG,   /* 确认键长按 */
} board_key_event_t;

/**
 * @brief 初始化按键 GPIO（输入，外部上拉）
 */
esp_err_t board_keys_init(void);

/**
 * @brief 非阻塞轮询按键状态（消抖 + 短按 + 长按事件）
 *
 * 应周期调用（如 LVGL 输入轮询中，10ms 周期）。
 *
 * @return 检测到的按键事件；无事件返回 BOARD_KEY_NONE
 */
board_key_event_t board_keys_scan(void);

#ifdef __cplusplus
}
#endif
