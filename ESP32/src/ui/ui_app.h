/**
 * @file ui_app.h
 * @brief LVGL 产品 UI 入口
 *
 * 正式产品 UI（横屏 480×320 卡片式）。分阶段接入：
 *   阶段1：LVGL 最小初始化（lv_init + 空 display）
 *   阶段2：显示端口（ui_port_display → lcd_ui_flush_area）
 *   阶段3：输入端口（ui_port_input：触摸 + 实体键）
 *   阶段4：状态模型 + 中文字体 + 主题
 *   阶段5：页面与底部导航
 *   阶段6：性能与回归
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动产品 UI（创建 LVGL 任务并初始化）
 * @return ESP_OK 成功
 */
esp_err_t ui_app_start(void);

#ifdef __cplusplus
}
#endif
