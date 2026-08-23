/**
 * @file ui_page_settings.c
 * @brief 设置页（展示已有能力：显示/连接/设备状态）
 */

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "../app_state.h"
#include "../touch.h"
#include "../ui_strings.h"
#include "../ui_theme.h"

lv_obj_t *ui_page_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, UI_STR_SETTINGS);
    lv_obj_set_pos(title, UI_MARGIN, UI_MARGIN);
    lv_obj_set_style_text_font(title, UI_FONT_DEFAULT, 0);

    /* 显示设置行（固定文案拼接，避免长格式化） */
    lv_obj_t *disp = lv_label_create(page);
    lv_label_set_text(disp, UI_STR_SETTINGS_DISP ": " UI_STR_SETTINGS_ROT " 90 / "
                           UI_STR_SETTINGS_TOUCH " " UI_STR_STATE_OK " / "
                           UI_STR_SETTINGS_BL " " UI_STR_STATE_ON);
    lv_obj_set_pos(disp, UI_MARGIN, 60);
    lv_obj_set_width(disp, lv_pct(100) - 2 * UI_MARGIN);
    lv_label_set_long_mode(disp, LV_LABEL_LONG_WRAP);

    /* 设备信息 */
    const touch_info_t *info = touch_get_info();
    const char *name = (info && info->detected) ? info->name : "unknown";
    uint8_t addr = info ? info->i2c_addr : 0;

    lv_obj_t *dev_title = lv_label_create(page);
    lv_label_set_text(dev_title, UI_STR_SETTINGS_DEV ":");
    lv_obj_set_pos(dev_title, UI_MARGIN, 110);

    char dev_buf[40];
    dev_buf[0] = '\0';
    strncat(dev_buf, name, sizeof(dev_buf) - 8 - 1); /* 预留 " @0x.." */
    snprintf(dev_buf + strlen(dev_buf), 8, " @0x%02X", addr);
    lv_obj_t *dev = lv_label_create(page);
    lv_label_set_text(dev, dev_buf);
    lv_obj_set_pos(dev, UI_MARGIN, 140);

    /* 固件版本 */
    char fw_buf[40];
    snprintf(fw_buf, sizeof(fw_buf), "%d.%d.%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_obj_t *fw_title = lv_label_create(page);
    lv_label_set_text(fw_title, UI_STR_SETTINGS_FW ":");
    lv_obj_set_pos(fw_title, UI_MARGIN, 180);
    lv_obj_t *fw = lv_label_create(page);
    lv_label_set_text(fw, fw_buf);
    lv_obj_set_pos(fw, UI_MARGIN, 210);

    return page;
}
