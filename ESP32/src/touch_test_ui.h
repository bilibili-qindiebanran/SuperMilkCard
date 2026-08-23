/**
 * @file touch_test_ui.h
 * @brief 触摸测试界面：显示识别信息、坐标、轨迹、手势测试
 *
 * 替换原有 lcd_demo 的屏幕显示内容，用于触摸驱动验证。
 * 测试项：
 *   - 控制器识别信息 / TP_INT 电平 / 触点数
 *   - 单点：十字光标 + 实时轨迹
 *   - 多点：按 touch_id 不同颜色光标 + 轨迹
 *   - 点击：四角 + 中心目标点，记录命中
 *   - 滑动：起点/终点/位移/方向
 *   - 长按：时长 + 触发
 *   - 松手：触点 Up / 超时释放
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动触摸测试 UI 任务（阻塞创建任务，返回后任务独立运行）
 */
esp_err_t touch_test_ui_start(void);

#ifdef __cplusplus
}
#endif
