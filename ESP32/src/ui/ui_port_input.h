/**
 * @file ui_port_input.h
 * @brief LVGL 输入端口：触摸 + 实体键适配
 *
 * 设计：
 *   - 专用输入轮询任务：非阻塞批量消费 touch_get_event()，维护"最新指针状态"
 *   - LVGL read_cb 只读最新状态（Down/Move/Up），不碰 I2C、不等队列
 *   - 实体键 GPIO38/GPIO4 → LVGL KEYPAD（ESC/ENTER）
 */

#pragma once

#include "esp_err.h"

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 LVGL 输入设备（触摸 POINTER + 实体键 KEYPAD）并启动轮询任务
 * @return ESP_OK 成功
 */
esp_err_t ui_port_input_create(void);

#ifdef __cplusplus
}
#endif
