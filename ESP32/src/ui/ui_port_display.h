/**
 * @file ui_port_display.h
 * @brief LVGL 显示端口：把 LVGL 渲染输出接入 lcd_ui（QSPI + 分块 DMA）
 *
 * 设计：
 *   - LVGL 逻辑分辨率 480x320（横屏），lcd_ui 物理面板 320x480（竖屏）
 *   - LVGL 软件旋转（lv_display_set_rotation 90）处理方向，flush_cb 收到物理坐标
 *   - flush_cb 内做 RGB565 字节序交换（面板要求大端），交给 lcd_ui_flush_area
 *   - DMA 发送完成后 lv_display_flush_ready()
 */

#pragma once

#include "esp_err.h"

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 LVGL 显示（接入 lcd_ui）
 * @return 创建的 display 句柄，失败返回 NULL
 */
lv_display_t *ui_port_display_create(void);

/**
 * @brief 获取当前逻辑分辨率
 */
void ui_port_display_get_resolution(int32_t *hor, int32_t *ver);

#ifdef __cplusplus
}
#endif
