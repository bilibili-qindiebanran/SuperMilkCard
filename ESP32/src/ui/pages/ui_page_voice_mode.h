/**
 * @file ui_page_voice_mode.h
 * @brief 语音互动模式选择页（全屏）
 *
 * 展示两个模式卡片：
 *   0 = 连接电脑 Live2D（Live2D 局域网联动）
 *   1 = 独立角色对话（独立角色 API 模式）
 *
 * 交互（实体按键 + 触摸同一流程）：
 *   - 短按 BACK / 触摸卡片：切换焦点（主色高亮）
 *   - 短按 OK / 触摸「进入」：执行当前模式预检并进入
 *   - 长按 BACK / 左上角返回桌面：回到主页
 * 见 docs/双模式联网语音角色与智能家居实施计划.md §2。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建模式选择页（覆盖整个屏幕，默认隐藏；由 ui_pages_create 调用）
 */
lv_obj_t *ui_page_voice_mode_create(lv_obj_t *parent);

/** @brief 进入模式选择页时调用（重置焦点/状态） */
void ui_page_voice_mode_show(void);

/** @brief 返回桌面按钮回调 */
void ui_page_voice_mode_back_cb(lv_event_t *event);

/** @brief 模式选择页按键分发（由 ui_pages 的按键路由调用） */
void ui_page_voice_mode_handle_key(int key_event);

/** @brief 周期刷新（模式在线状态/预检结果） */
void ui_page_voice_mode_refresh(void);

/** @brief 当前聚焦的模式：0=Live2D 联动，1=独立角色 */
int ui_page_voice_mode_focus(void);

#ifdef __cplusplus
}
#endif
