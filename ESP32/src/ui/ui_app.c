/**
 * @file ui_app.c
 * @brief LVGL 产品 UI 实现
 *
 * 阶段1：LVGL 最小初始化 + 空 display（验证 LVGL 能在当前 lcd_ui 环境运行）
 * 后续阶段在此基础扩展：显示端口 → 输入端口 → 状态 → 页面
 */

#include "ui_app.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

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
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t ui_app_start(void)
{
    ESP_LOGI(TAG, "LVGL %d.%d.%d starting...",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    /* 初始化 LVGL 核心 */
    lv_init();

    /* 阶段1：创建空 display（占位，阶段2 接入 ui_port_display 真实显示） */
    lv_display_t *disp = lv_display_create(480, 320);
    if (disp == NULL)
    {
        ESP_LOGE(TAG, "lv_display_create failed");
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    /* 阶段1：简单占位标签验证 LVGL 渲染管线 */
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "LVGL 9.5 ready");
    lv_obj_center(label);

    /* 启动 LVGL 任务（心跳 + 主循环） */
    xTaskCreatePinnedToCore(ui_tick_task, "ui_tick", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(ui_loop_task, "ui_loop", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "ui_app started (stage 1: minimal LVGL)");
    return ESP_OK;
}
