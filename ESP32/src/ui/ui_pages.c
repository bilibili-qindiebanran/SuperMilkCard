/**
 * @file ui_pages.c
 * @brief 页面管理 + 底部导航实现
 */

#include "ui_pages.h"

#include "pages/ui_page_home.h"
#include "pages/ui_page_chat.h"
#include "pages/ui_page_music.h"
#include "pages/ui_page_settings.h"
#include "ui_strings.h"
#include "ui_theme.h"

/* 页面对象（按 UI_PAGE_* 索引） */
static lv_obj_t *s_pages[UI_PAGE_COUNT];
static lv_obj_t *s_nav_btns[UI_PAGE_COUNT];
static lv_obj_t *s_content;
static ui_page_id_t s_current = UI_PAGE_HOME;

/* 页面创建函数表 */
static lv_obj_t *(*const s_page_creators[UI_PAGE_COUNT])(lv_obj_t *) = {
    ui_page_home_create,
    ui_page_chat_create,
    ui_page_music_create,
    ui_page_settings_create,
};

/* 底部导航文案 */
static const char *const s_nav_labels[UI_PAGE_COUNT] = {
    UI_STR_HOME, UI_STR_CHAT, UI_STR_MUSIC, UI_STR_SETTINGS,
};

/* 导航点击回调 */
static void nav_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        if (s_nav_btns[i] == btn)
        {
            ui_pages_show((ui_page_id_t)i);
            break;
        }
    }
}

void ui_pages_create(lv_obj_t *parent)
{
    s_content = parent;

    /* 内容区（底部导航上方） */
    static lv_obj_t *content_area;
    content_area = lv_obj_create(parent);
    lv_obj_set_size(content_area, lv_pct(100), lv_pct(100) - 48);
    lv_obj_set_pos(content_area, 0, 0);
    lv_obj_set_style_bg_opa(content_area, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);

    /* 创建所有页面（隐藏除首页外） */
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        s_pages[i] = s_page_creators[i](content_area);
        if (i != UI_PAGE_HOME)
        {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 底部导航条 */
    lv_obj_t *nav = lv_obj_create(parent);
    lv_obj_set_size(nav, lv_pct(100), 48);
    lv_obj_set_pos(nav, 0, lv_pct(100) - 48);
    lv_obj_set_style_bg_color(nav, UI_COLOR_SURFACE_2, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t btn_w = 480 / UI_PAGE_COUNT;
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        lv_obj_t *btn = lv_button_create(nav);
        lv_obj_set_size(btn, btn_w - 4, 40);
        lv_obj_set_pos(btn, i * btn_w + 2, 4);
        lv_obj_set_style_bg_color(btn, UI_COLOR_SURFACE_2, 0);
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, s_nav_labels[i]);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, nav_click_cb, LV_EVENT_CLICKED, NULL);
        s_nav_btns[i] = btn;
    }
}

void ui_pages_show(ui_page_id_t id)
{
    if (id >= UI_PAGE_COUNT)
    {
        return;
    }
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        if (i == (int)id)
        {
            lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    s_current = id;
}

ui_page_id_t ui_pages_current(void)
{
    return s_current;
}
