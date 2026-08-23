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

/* LVGL 使用产品横屏逻辑分辨率，lcd_ui 在刷新时旋转到物理面板。 */
#define UI_DISP_HOR_RES LCD_UI_W
#define UI_DISP_VER_RES LCD_UI_H
#define UI_DISP_BUF_ROWS 32
#define UI_DISP_BUF_BYTES (UI_DISP_HOR_RES * UI_DISP_BUF_ROWS * 2)

static lv_display_t *s_disp = NULL;
static uint16_t *s_buf1 = NULL;
static uint16_t *s_buf2 = NULL;

/* ------------------------------------------------------------------ */
/* flush_cb：LVGL 渲染完成回调（ISR 安全，可异步）                     */
/* ------------------------------------------------------------------ */
static void ui_port_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* px_map 是与 area 对应的紧凑区域；字节序转换在 lcd_ui 的 DMA 行缓冲中进行。 */
    esp_err_t err = lcd_ui_flush_area(area->x1, area->y1,
                                      area->x2 + 1, area->y2 + 1, px_map);
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

    /* 双 32 行条带缓冲：区域数据天然紧凑，避免整帧步长歧义。 */
    const uint32_t buf_bytes = UI_DISP_BUF_BYTES;
    s_buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_buf1 == NULL)
    {
        ESP_LOGE(TAG, "buf1 alloc failed");
        return NULL;
    }

    s_buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_buf2 == NULL)
    {
        ESP_LOGE(TAG, "buf2 alloc failed");
        free(s_buf1);
        s_buf1 = NULL;
        return NULL;
    }

    /* LVGL 原生创建为产品横屏 480×320。 */
    s_disp = lv_display_create(UI_DISP_HOR_RES, UI_DISP_VER_RES);
    if (s_disp == NULL)
    {
        ESP_LOGE(TAG, "lv_display_create failed");
        free(s_buf1);
        free(s_buf2);
        s_buf1 = NULL;
        s_buf2 = NULL;
        return NULL;
    }

    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_disp, ui_port_flush_cb);

    ESP_LOGI(TAG, "display created: 480x320, dual %lu-byte partial buffers",
             (unsigned long)buf_bytes);
    return s_disp;
}

void ui_port_display_get_resolution(int32_t *hor, int32_t *ver)
{
    if (hor) *hor = s_disp ? lv_display_get_horizontal_resolution(s_disp) : UI_DISP_HOR_RES;
    if (ver) *ver = s_disp ? lv_display_get_vertical_resolution(s_disp) : UI_DISP_VER_RES;
}
