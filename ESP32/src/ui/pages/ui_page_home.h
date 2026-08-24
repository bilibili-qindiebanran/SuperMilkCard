/**
 * @file ui_page_home.h
 * @brief Fluent 2 首页
 */

#pragma once

#include "lvgl.h"

lv_obj_t *ui_page_home_create(lv_obj_t *parent);
void ui_page_home_refresh(void);
void ui_page_home_refresh_fps(void);
void ui_page_home_settings_event(lv_event_t *event);
