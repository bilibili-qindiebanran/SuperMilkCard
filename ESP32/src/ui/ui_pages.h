/**
 * @file ui_pages.h
 * @brief 产品 UI 页面接口（首页/聊天/音乐/设置 + 全屏互动页）
 *
 * 页面统一接口：create 返回页面对象，show/hide 切换。
 * 底部导航在 ui_app 中管理，切换只隐藏/显示，不重初始化底层。
 * 全屏互动页（Live2D 联动 / 模式选择）隐藏底部导航，覆盖整个屏幕。
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

#include "../board_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 页面索引（与底部导航顺序一致） */
typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_CHAT,
    UI_PAGE_MUSIC,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT,
} ui_page_id_t;

/**
 * @brief 创建所有页面（一次性创建，后续 show/hide 切换）
 */
void ui_pages_create(lv_obj_t *parent);

/**
 * @brief 显示指定页面，隐藏其他
 */
void ui_pages_show(ui_page_id_t id);

/**
 * @brief 获取当前页面 id
 */
ui_page_id_t ui_pages_current(void);

/**
 * @brief 进入全屏 Live2D 互动页（隐藏底部导航，通知 Windows）
 */
void ui_pages_show_live2d(void);

/**
 * @brief 进入全屏语音互动模式选择页
 */
void ui_pages_show_voice_mode(void);

/**
 * @brief 返回桌面：回到主页并恢复常规底部导航，通知 Windows
 */
void ui_pages_return_home(void);

/**
 * @brief 当前是否处于全屏 Live2D 互动页
 */
bool ui_pages_live2d_active(void);

/**
 * @brief 当前是否处于全屏语音互动模式选择页
 */
bool ui_pages_voice_mode_active(void);

/**
 * @brief 实体按键分发（ui_port_input 调用）：按当前页面路由
 */
void ui_pages_handle_key(board_key_event_t key);

/**
 * @brief 使当前可见页面失效（强制重绘）
 */
void ui_pages_invalidate_active(void);

#ifdef __cplusplus
}
#endif
