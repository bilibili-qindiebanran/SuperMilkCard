/**
 * @file ui_app.c
 * @brief LVGL 产品 UI 实现
 *
 * 阶段2：接入显示端口（ui_port_display → lcd_ui_flush_area）
 * 后续阶段继续扩展：输入端口 → 状态 → 页面
 */

#include "ui_app.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "ui_port_display.h"

static const char *TAG = "ui_app";

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

    /* 阶段2：简单占位 label 验证旋转后渲染 */
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "LVGL 9.5 480x320");
    lv_obj_center(label);

    /* 启动 LVGL 任务：心跳（CPU0）+ 主循环（CPU1，优先级 3 低于外设任务，
     * 避免长时间占用 CPU 饿死 IDLE/其他任务触发看门狗） */
    xTaskCreatePinnedToCore(ui_tick_task, "ui_tick", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(ui_loop_task, "ui_loop", 8192, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "ui_app started (stage 2: display port)");
    return ESP_OK;
}
