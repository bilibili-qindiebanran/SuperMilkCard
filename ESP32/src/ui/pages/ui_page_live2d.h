/**
 * @file ui_page_live2d.h
 * @brief 全屏 Live2D 互动页（Emoji 风格表情 + 连接状态 + 对话摘要）
 *
 * 首期用 LVGL 基础图元（圆/弧/线/文本）绘制 6 种表情：
 * neutral / happy / sad / angry / surprised / thinking。
 * 不依赖彩色 Emoji 字形（中文子集字体不保证包含），不使用预渲染位图。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建全屏互动页（覆盖整个屏幕，默认隐藏；由 ui_pages_create 调用）
 */
lv_obj_t *ui_page_live2d_create(lv_obj_t *parent);

/** @brief 进入互动页时调用：通知 Windows（live2d_command: enter） */
void ui_page_live2d_show(void);

/** @brief 交互回调：左上角返回桌面（由 ui_pages 的 return_home 处理） */
void ui_page_live2d_return_cb(lv_event_t *event);

/** @brief 周期刷新表情/状态/摘要（由 ui_app 定时器调用） */
void ui_page_live2d_refresh(void);

#ifdef __cplusplus
}
#endif
