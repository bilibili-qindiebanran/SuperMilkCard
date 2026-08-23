/**
 * @file ui_port_display.h
 * @brief LVGL 显示端口：把 LVGL 渲染输出接入 lcd_ui（QSPI + 分块 DMA）
 *
 * 设计：
 *   - LVGL 使用 480x320 横屏逻辑坐标与双条带 PARTIAL 缓冲
 *   - lcd_ui 在 DMA 行缓冲中将每个逻辑区域旋转为物理 320x480 面板坐标
 *   - RGB565 字节序仅在 lcd_ui 的内部 DMA 行缓冲中转换，不能改写 LVGL 整帧缓冲
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
