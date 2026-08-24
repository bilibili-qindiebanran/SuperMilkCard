/**
 * @file ui_page_settings.h
 * @brief Fluent 2 设置入口页
 */

#pragma once

#include "lvgl.h"

lv_obj_t *ui_page_settings_create(lv_obj_t *parent);

/** @brief 周期性刷新网络/TCP 状态显示（由 ui_app 定时器调用） */
void ui_page_settings_refresh(void);
