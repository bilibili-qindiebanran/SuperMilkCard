/**
 * @file ui_port_input.c
 * @brief LVGL 输入端口实现
 */

#include "ui_port_input.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_keys.h"
#include "touch.h"

static const char *TAG = "ui_port_in";

/* ------------------------------------------------------------------ */
/* 最新指针状态（read_cb 只读，轮询任务写）                           */
/* ------------------------------------------------------------------ */
static lv_point_t s_pointer_pt = {0, 0};
static lv_indev_state_t s_pointer_state = LV_INDEV_STATE_RELEASED;

/* 实体键事件（keypad 用） */
static volatile uint32_t s_key_pending = 0; /* 待上报的按键码（0=无） */

/* LVGL 输入句柄 */
static lv_indev_t *s_indev_ptr = NULL;
static lv_indev_t *s_indev_key = NULL;

/* ------------------------------------------------------------------ */
/* 触摸 read_cb：只读最新状态（不碰 I2C）                             */
/* ------------------------------------------------------------------ */
static void ui_port_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_pointer_pt.x;
    data->point.y = s_pointer_pt.y;
    data->state = s_pointer_state;
}

/* ------------------------------------------------------------------ */
/* 实体键 read_cb                                                      */
/* ------------------------------------------------------------------ */
static void ui_port_key_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;

    /* 消费一个待上报按键 */
    uint32_t key = s_key_pending;
    if (key != 0)
    {
        /* 原子取走 */
        s_key_pending = 0;
        data->key = key;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

/* ------------------------------------------------------------------ */
/* 输入轮询任务：消费触摸事件 + 扫描实体键                            */
/* ------------------------------------------------------------------ */
static void ui_port_input_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "input task started");

    touch_event_t evt;

    while (1)
    {
        /* 有界消费触摸事件，避免队列持续有数据时长期占满 CPU。 */
        for (int event_count = 0; event_count < 8; event_count++)
        {
            if (touch_get_event(&evt, 0) != ESP_OK) break;
            switch (evt.event)
            {
            case TOUCH_EVT_DOWN:
                s_pointer_pt.x = evt.x;
                s_pointer_pt.y = evt.y;
                s_pointer_state = LV_INDEV_STATE_PRESSED;
                break;
            case TOUCH_EVT_MOVE:
                /* 只保留最新坐标 */
                s_pointer_pt.x = evt.x;
                s_pointer_pt.y = evt.y;
                break;
            case TOUCH_EVT_UP:
                s_pointer_pt.x = evt.x;
                s_pointer_pt.y = evt.y;
                s_pointer_state = LV_INDEV_STATE_RELEASED;
                break;
            default:
                break;
            }
        }

        /* 实体键扫描 → LVGL KEYPAD */
        board_key_event_t key = board_keys_scan();
        if (key == BOARD_KEY_BACK)
        {
            s_key_pending = LV_KEY_ESC;
        }
        else if (key == BOARD_KEY_OK)
        {
            s_key_pending = LV_KEY_ENTER;
        }

        vTaskDelay(1);
    }
}

/* ------------------------------------------------------------------ */
/* 创建输入                                                            */
/* ------------------------------------------------------------------ */
esp_err_t ui_port_input_create(void)
{
    if (s_indev_ptr != NULL)
    {
        return ESP_OK;
    }

    /* 实体键初始化 */
    board_keys_init();

    /* 触摸 POINTER 输入 */
    s_indev_ptr = lv_indev_create();
    lv_indev_set_type(s_indev_ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev_ptr, ui_port_touch_read_cb);

    /* 实体键 KEYPAD 输入 */
    s_indev_key = lv_indev_create();
    lv_indev_set_type(s_indev_key, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev_key, ui_port_key_read_cb);

    /* 启动输入轮询任务 */
    xTaskCreatePinnedToCore(ui_port_input_task, "ui_input", 4096, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "input created: touch POINTER + keys KEYPAD");
    return ESP_OK;
}
