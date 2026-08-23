/**
 * @file ui_app.c
 * @brief LVGL 产品 UI 实现
 *
 * 阶段3：接入输入端口（ui_port_input：触摸 POINTER + 实体键 KEYPAD）
 * 后续阶段继续扩展：状态 → 页面
 */

#include "ui_app.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "lcd_ui.h"
#include "touch.h"
#include "ui_port_display.h"
#include "ui_port_input.h"

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

/* 测试按钮点击回调：验证触摸 → LVGL 事件链路 */
static void ui_test_btn_cb(lv_event_t *e)
{
    static int tap = 0;
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    char buf[32];
    snprintf(buf, sizeof(buf), "点击 %d 次", ++tap);
    lv_label_set_text(lbl, buf);
    ESP_LOGI(TAG, "button clicked %d times", tap);
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

    /* 阶段3：接入输入端口（触摸 POINTER + 实体键 KEYPAD） */
    esp_err_t in_err = ui_port_input_create();
    if (in_err != ESP_OK)
    {
        ESP_LOGE(TAG, "ui_port_input_create failed: %s", esp_err_to_name(in_err));
    }

    /* 同步触摸旋转与显示旋转：LVGL 显示 90° 横屏，触摸坐标也必须 90°，
     * 否则触摸位置与屏幕显示错位 */
    touch_set_rotation(UI_DISPLAY_ROTATION_DEG);

    /* 阶段3：测试按钮（点击计数，验证触摸 → LVGL 事件链路） */
    lv_obj_t *btn = lv_button_create(lv_scr_act());
    lv_obj_set_size(btn, 160, 60);
    lv_obj_center(btn);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "tap");
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(btn, ui_test_btn_cb, LV_EVENT_CLICKED, btn_label);

    /* 调试：打印旋转后分辨率确认方向 */
    ESP_LOGI(TAG, "disp after rot: hor=%d ver=%d rot=%d",
             lv_display_get_horizontal_resolution(disp),
             lv_display_get_vertical_resolution(disp),
             (int)lv_display_get_rotation(disp));

    /* 启动 LVGL 任务：心跳（CPU0）+ 主循环（CPU1，优先级 3 低于外设任务，
     * 避免长时间占用 CPU 饿死 IDLE/其他任务触发看门狗） */
    xTaskCreatePinnedToCore(ui_tick_task, "ui_tick", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(ui_loop_task, "ui_loop", 8192, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "ui_app started (stage 3: input port)");
    return ESP_OK;
}
