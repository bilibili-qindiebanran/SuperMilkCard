/**
 * @file ui_app.c
 * @brief LVGL 产品 UI 实现
 *
 * 阶段5：产品页面 + 底部导航
 *   - 主题初始化（深色 + 官方 CJK 字体）
 *   - 创建 4 页面 + 底部导航
 *   - 启动心跳/主循环任务
 */

#include "ui_app.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "lcd_ui.h"
#include "touch.h"
#include "ui_pages.h"
#include "pages/ui_page_home.h"
#include "ui_port_display.h"
#include "ui_port_input.h"
#include "ui_theme.h"

static const char *TAG = "ui_app";

static void ui_state_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_page_home_refresh();
}

/* LVGL 心跳（周期调用 lv_tick_inc） */
static void ui_tick_task(void *arg)
{
    (void)arg;
    while (1)
    {
        lv_tick_inc(10); /* 10ms 心跳 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* LVGL 主循环任务 */
static void ui_loop_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "ui_loop_task started");

    while (1)
    {
        lv_timer_handler(); /* 处理 LVGL 定时器/刷新 */
        /* 释放 CPU：16ms 周期（约 60fps 上限），避免饿死 IDLE0 触发看门狗 */
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

esp_err_t ui_app_start(void)
{
    ESP_LOGI(TAG, "LVGL %d.%d.%d starting...",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    /* 初始化 LVGL 核心 */
    lv_init();

    /* 接入显示端口（lcd_ui 局部刷新 + 旋转） */
    lv_display_t *disp = ui_port_display_create();
    if (disp == NULL)
    {
        ESP_LOGE(TAG, "ui_port_display_create failed");
        return ESP_ERR_NO_MEM;
    }

    /* 接入输入端口（触摸 POINTER + 实体键 KEYPAD） */
    esp_err_t in_err = ui_port_input_create();
    if (in_err != ESP_OK)
    {
        ESP_LOGE(TAG, "ui_port_input_create failed: %s", esp_err_to_name(in_err));
    }

    /* 同步触摸旋转与显示旋转 */
    touch_set_rotation(UI_DISPLAY_ROTATION_DEG);

    /* 阶段5：初始化主题（深色背景 + 官方 CJK 字体） */
    ui_theme_init();

    /* 阶段5：创建页面 + 底部导航 */
    ui_pages_create(lv_scr_act());
    lv_timer_create(ui_state_refresh_cb, 500, NULL);

    /* 调试：打印旋转后分辨率确认方向 */
    ESP_LOGI(TAG, "disp: hor=%d ver=%d rot=%d",
             lv_display_get_horizontal_resolution(disp),
             lv_display_get_vertical_resolution(disp),
             (int)lv_display_get_rotation(disp));

    /* 启动 LVGL 任务：心跳（CPU0）+ 主循环（CPU1，优先级 3） */
    xTaskCreatePinnedToCore(ui_tick_task, "ui_tick", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(ui_loop_task, "ui_loop", 8192, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "ui_app started (stage 5: pages + nav)");
    return ESP_OK;
}
