/**
 * @file ui_port_display.c
 * @brief LVGL 显示端口实现
 */

#include "ui_port_display.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_ui.h"

static const char *TAG = "ui_port_disp";

/* LVGL 渲染缓冲：两块内部 DMA 可访问缓冲（PARTIAL 模式，条带高度可调） */
#define UI_DISP_HOR_RES 480 /* 逻辑横屏分辨率 */
#define UI_DISP_VER_RES 320
#define UI_DISP_BUF_ROWS 32            /* 条带高度（像素） */
#define UI_DISP_BUF_BYTES (UI_DISP_HOR_RES * UI_DISP_BUF_ROWS * 2)

static lv_display_t *s_disp = NULL;
static uint16_t *s_buf1 = NULL;
static uint16_t *s_buf2 = NULL;

/* ------------------------------------------------------------------ */
/* flush_cb：LVGL 渲染完成回调（ISR 安全，可异步）                     */
/* ------------------------------------------------------------------ */
static void ui_port_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;

    /* 1. RGB565 字节序交换：LVGL 渲染是小端 native，ST77926 QSPI 要求大端 */
    lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));

    /* 2. 交给 lcd_ui 局部刷新（4 像素对齐 + 分块 DMA + 同步屏障）
     *    area 是物理坐标（LVGL 旋转 90° 后），px_map 是物理方向数据 */
    esp_err_t err = lcd_ui_flush_area(area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "lcd_ui_flush_area failed: %s", esp_err_to_name(err));
    }

    /* 3. 通知 LVGL 刷新完成（lcd_ui_flush_area 内部已同步 DMA） */
    lv_display_flush_ready(disp);
}

/* ------------------------------------------------------------------ */
/* 创建显示                                                            */
/* ------------------------------------------------------------------ */
lv_display_t *ui_port_display_create(void)
{
    if (s_disp != NULL)
    {
        return s_disp;
    }

    /* 分配内部 RAM 双缓冲（DMA 可访问） */
    s_buf1 = heap_caps_malloc(UI_DISP_BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_buf1 == NULL)
    {
        ESP_LOGE(TAG, "buf1 alloc failed (%d bytes)", UI_DISP_BUF_BYTES);
        return NULL;
    }
    s_buf2 = heap_caps_malloc(UI_DISP_BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_buf2 == NULL)
    {
        ESP_LOGE(TAG, "buf2 alloc failed (%d bytes)", UI_DISP_BUF_BYTES);
        free(s_buf1);
        return NULL;
    }

    /* 创建 display：产品逻辑分辨率 480x320（横屏） */
    s_disp = lv_display_create(UI_DISP_HOR_RES, UI_DISP_VER_RES); /* 480x320 横屏 */
    if (s_disp == NULL)
    {
        ESP_LOGE(TAG, "lv_display_create failed");
        free(s_buf1);
        free(s_buf2);
        return NULL;
    }

    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, UI_DISP_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, ui_port_flush_cb);

    /* 软件旋转：逻辑 480x320 → 物理 320x480（90°） */
    lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_90);

    ESP_LOGI(TAG, "display created: 480x320 -> phys 320x480 @90°, dual %d-byte buffers",
             UI_DISP_BUF_BYTES);
    return s_disp;
}

void ui_port_display_get_resolution(int32_t *hor, int32_t *ver)
{
    if (hor) *hor = UI_DISP_HOR_RES;
    if (ver) *ver = UI_DISP_VER_RES;
}
