/**
 * @file ui_pages.h
 * @brief 产品 UI 页面接口（首页/聊天/音乐/设置）
 *
 * 页面统一接口：create 返回页面对象，show/hide 切换。
 * 底部导航在 ui_app 中管理，切换只隐藏/显示，不重初始化底层。
 */

#pragma once

#include "lvgl.h"

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

#ifdef __cplusplus
}
#endif
