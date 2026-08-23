/**
 * @file ui_page_home.c
 * @brief 首页：横屏卡片式布局
 *
 * 布局（480x320）：
 *   - 顶部状态栏：设备名、时间
 *   - 左侧大卡：时间
 *   - 右侧卡列：电源 / 音频 / 连接 / 媒体
 */

#include "lvgl.h"

#include "../app_state.h"
#include "../ui_strings.h"
#include "../ui_theme.h"

/* 卡片工具：创建圆角卡片 */
static lv_obj_t *card_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    ui_theme_apply_card(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *card_title(lv_obj_t *card, const char *text)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static lv_obj_t *card_value(lv_obj_t *card, const char *text)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_obj_set_style_text_font(lbl, UI_FONT_DEFAULT, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

/* 刷新首页（每帧调用，读 app_state 快照） */
void ui_page_home_refresh(void)
{
    const app_state_t *st = app_state_get();
    /* 首页对象由 ui_pages 持有；此处通过全局刷新标记由定时器驱动。
     * 简化：页面内容在 create 时静态创建，刷新逻辑在 ui_app 定时器回调。 */
    (void)st;
}

/* 创建首页内容（父对象为 home page 容器） */
lv_obj_t *ui_page_home_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部状态栏 */
    lv_obj_t *status = lv_label_create(page);
    lv_label_set_text(status, UI_STR_STATUS_BAR " | " UI_STR_TIME_PLACE);
    lv_obj_set_pos(status, UI_MARGIN, UI_MARGIN - 6);

    /* 左侧大卡：时间 */
    lv_obj_t *time_card = card_create(page, UI_MARGIN, 36, 220, 200);
    card_title(time_card, UI_STR_TIME_TITLE);
    lv_obj_t *time_val = card_value(time_card, UI_STR_TIME_PLACE);
    lv_obj_set_pos(time_val, UI_GAP, 40);
    lv_obj_set_style_text_font(time_val, UI_FONT_DEFAULT, 0);
    lv_obj_t *time_sync = card_value(time_card, UI_STR_TIME_SYNC);
    lv_obj_set_pos(time_sync, UI_GAP, 80);
    ui_theme_apply_status_text(time_sync, 2); /* 警告色：待同步 */

    /* 右侧卡片列 */
    lv_coord_t rx = 244;
    lv_coord_t cw = 224;
    lv_coord_t ch = 62;

    /* 电源卡 */
    lv_obj_t *pw = card_create(page, rx, 36, cw, ch);
    card_title(pw, UI_STR_POWER_TITLE);
    lv_obj_t *pw_val = card_value(pw, UI_STR_POWER_IDLE);
    lv_obj_set_pos(pw_val, UI_GAP, 32);

    /* 音频卡 */
    lv_obj_t *au = card_create(page, rx, 36 + ch + UI_GAP, cw, ch);
    card_title(au, UI_STR_AUDIO_TITLE);
    lv_obj_t *au_val = card_value(au, "-- dB");
    lv_obj_set_pos(au_val, UI_GAP, 32);

    /* 连接卡 */
    lv_obj_t *co = card_create(page, rx, 36 + 2 * (ch + UI_GAP), cw, ch);
    card_title(co, UI_STR_CONN_TITLE);
    lv_obj_t *co_val = card_value(co, UI_STR_CONN_WIFI ":" UI_STR_STATE_OFF
                                     "  " UI_STR_CONN_UART ":" UI_STR_STATE_OK
                                     "  " UI_STR_CONN_WIN ":" UI_STR_STATE_DISC);
    lv_obj_set_pos(co_val, UI_GAP, 32);
    lv_obj_set_width(co_val, cw - 2 * UI_GAP);
    lv_label_set_long_mode(co_val, LV_LABEL_LONG_WRAP);

    return page;
}
