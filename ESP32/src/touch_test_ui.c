/**
 * @file touch_test_ui.c
 * @brief 触摸测试界面实现
 */

#include "touch_test_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_ui.h"
#include "touch.h"

static const char *TAG = "touch_ui";

/* 触点多点颜色（按 id 分配） */
static const uint16_t s_point_colors[TOUCH_MAX_POINTS] = {
    LCD_UI_WHITE, LCD_UI_RED, LCD_UI_GREEN, LCD_UI_BLUE, LCD_UI_YELLOW,
    LCD_UI_CYAN,  LCD_UI_RGB565(255, 128, 0), LCD_UI_RGB565(128, 0, 255),
    LCD_UI_RGB565(0, 255, 128), LCD_UI_RGB565(255, 0, 128),
};

/* 目标点（四角 + 中心） */
static const struct
{
    int x, y;
    const char *name;
} s_targets[] = {
    {20,  20,  "TL"}, {300, 20,  "TR"}, {20,  460, "BL"},
    {300, 460, "BR"}, {160, 240, "C"},
};

#define TARGET_RADIUS 12      /* 目标点命中半径 */
#define TARGET_HIT_TOL 30     /* 命中容差 */

/* 轨迹点记录（滑动/长按测试用） */
#define TRACK_LEN 64
typedef struct
{
    uint16_t x, y;
    uint32_t ts_ms;
} track_pt_t;

typedef struct
{
    bool active;
    uint16_t x, y;
    uint32_t down_ms;
    uint32_t last_ms;
    uint32_t move_count;
    int start_x, start_y;
    track_pt_t trail[TRACK_LEN];
    int trail_len;
} touch_state_t;

static touch_state_t s_touch[TOUCH_MAX_POINTS];
static uint8_t s_target_hit[5] = {0}; /* 各目标点命中次数 */

/* ------------------------------------------------------------------ */
/* 画目标点                                                            */
/* ------------------------------------------------------------------ */
static void draw_targets(void)
{
    for (int i = 0; i < 5; i++)
    {
        uint16_t color = s_target_hit[i] ? LCD_UI_GREEN : LCD_UI_YELLOW;
        lcd_ui_draw_circle(s_targets[i].x, s_targets[i].y, TARGET_RADIUS, color);
        char buf[8];
        snprintf(buf, sizeof(buf), "%s", s_targets[i].name);
        lcd_ui_draw_string(s_targets[i].x - 6, s_targets[i].y - TARGET_RADIUS - 10,
                           buf, color, LCD_UI_BLACK);
    }
}

/* ------------------------------------------------------------------ */
/* 画单个触点（光标 + 轨迹）                                          */
/* ------------------------------------------------------------------ */
static void draw_touch_point(int id, const touch_state_t *t)
{
    uint16_t color = s_point_colors[id % TOUCH_MAX_POINTS];
    /* 十字光标 */
    lcd_ui_draw_crosshair(t->x, t->y, 10, color);
    /* 轨迹 */
    for (int i = 1; i < t->trail_len; i++)
    {
        lcd_ui_draw_line(t->trail[i - 1].x, t->trail[i - 1].y,
                         t->trail[i].x, t->trail[i].y, color);
    }
}

/* ------------------------------------------------------------------ */
/* UI 任务                                                             */
/* ------------------------------------------------------------------ */
static void touch_test_ui_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "touch test UI started");

    touch_event_t evt;
    int frame = 0;

    while (1)
    {
        bool has_event = false;

        /* 背景 */
        lcd_ui_fill_screen(LCD_UI_BLACK);

        /* 顶部信息：型号 / 地址 / TP_INT / 触点数 / 旋转 */
        const touch_info_t *info = touch_get_info();
        char line[48];

        snprintf(line, sizeof(line), "%s @0x%02X %s",
                 info->detected ? info->name : "NO TOUCH",
                 info->i2c_addr, touch_is_interrupt_mode() ? "INT" : "POLL");
        lcd_ui_draw_string(4, 0, line, LCD_UI_WHITE, LCD_UI_BLACK);

        snprintf(line, sizeof(line), "TP_INT=%d rot=%ddeg pts=%d",
                 touch_get_int_level(), touch_get_rotation(),
                 (int)s_touch[0].active + (int)s_touch[1].active);
        lcd_ui_draw_string(4, 10, line, LCD_UI_CYAN, LCD_UI_BLACK);

        /* 目标点 */
        draw_targets();

        /* 只吸收当前已排队的事件，随后立即渲染最新触点位置。 */
        while (touch_get_event(&evt, 0) == ESP_OK)
        {
            int id = evt.id % TOUCH_MAX_POINTS;
            touch_state_t *t = &s_touch[id];
            has_event = true; /* 有事件 → 本轮立即渲染 */

            switch (evt.event)
            {
            case TOUCH_EVT_DOWN:
                t->active = true;
                t->x = evt.x;
                t->y = evt.y;
                t->down_ms = evt.timestamp_ms;
                t->last_ms = evt.timestamp_ms;
                t->move_count = 0;
                t->start_x = evt.x;
                t->start_y = evt.y;
                t->trail_len = 0;
                t->trail[t->trail_len].x = evt.x;
                t->trail[t->trail_len].y = evt.y;
                t->trail[t->trail_len].ts_ms = evt.timestamp_ms;
                t->trail_len = 1;
                ESP_LOGI(TAG, "DOWN id=%d (%d,%d)", id, evt.x, evt.y);
                break;

            case TOUCH_EVT_MOVE:
                if (t->active)
                {
                    t->x = evt.x;
                    t->y = evt.y;
                    t->last_ms = evt.timestamp_ms;
                    t->move_count++;
                    if (t->trail_len < TRACK_LEN)
                    {
                        t->trail[t->trail_len].x = evt.x;
                        t->trail[t->trail_len].y = evt.y;
                        t->trail[t->trail_len].ts_ms = evt.timestamp_ms;
                        t->trail_len++;
                    }
                }
                break;

            case TOUCH_EVT_UP:
                if (t->active)
                {
                    uint32_t dur = evt.timestamp_ms - t->down_ms;
                    int dx = (int)evt.x - t->start_x;
                    int dy = (int)evt.y - t->start_y;
                    ESP_LOGI(TAG, "UP id=%d dur=%lums dist=%d,%d moves=%lu",
                             id, (unsigned long)dur, dx, dy,
                             (unsigned long)t->move_count);

                    /* 长按判定：>800ms */
                    if (dur > 800)
                    {
                        ESP_LOGI(TAG, "  LONG PRESS id=%d dur=%lums", id, (unsigned long)dur);
                    }

                    /* 点击命中目标点判定 */
                    for (int ti = 0; ti < 5; ti++)
                    {
                        int td = (int)evt.x - s_targets[ti].x;
                        int tyd = (int)evt.y - s_targets[ti].y;
                        if (td * td + tyd * tyd <= TARGET_HIT_TOL * TARGET_HIT_TOL)
                        {
                            s_target_hit[ti]++;
                            ESP_LOGI(TAG, "  HIT target %s (%d,%d) x%d",
                                     s_targets[ti].name, evt.x, evt.y, s_target_hit[ti]);
                        }
                    }
                    t->active = false;
                }
                break;

            default:
                break;
            }
        }

        /* 画活跃触点 */
        bool any_active = false;
        for (int id = 0; id < TOUCH_MAX_POINTS; id++)
        {
            if (s_touch[id].active)
            {
                any_active = true;
                draw_touch_point(id, &s_touch[id]);
            }
        }

        /* 帧计数 + 命中统计 */
        snprintf(line, sizeof(line), "hit:%d %d %d %d %d frame=%d",
                 s_target_hit[0], s_target_hit[1], s_target_hit[2],
                 s_target_hit[3], s_target_hit[4], frame++);
        lcd_ui_draw_string(4, 470, line, LCD_UI_YELLOW, LCD_UI_BLACK);

        lcd_ui_flush();

        /* 事件驱动：有事件时立即进入下一轮等待（无延时，事件到达即渲染）；
         * 无事件时短暂延时（兜底刷新静态内容） */
        if (!has_event && !any_active)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

esp_err_t touch_test_ui_start(void)
{
    memset(s_touch, 0, sizeof(s_touch));
    xTaskCreatePinnedToCore(touch_test_ui_task, "touch_ui", 4096, NULL, 4, NULL, 0);
    return ESP_OK;
}
