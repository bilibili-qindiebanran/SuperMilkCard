/**
 * @file ui_pages.c
 * @brief 页面管理与 Fluent 2 底部导航
 */

#include "ui_pages.h"

#include "pages/ui_page_chat.h"
#include "pages/ui_page_home.h"
#include "pages/ui_page_live2d.h"
#include "pages/ui_page_music.h"
#include "pages/ui_page_settings.h"
#include "ui_strings.h"
#include "ui_theme.h"

static lv_obj_t *s_pages[UI_PAGE_COUNT];
static lv_obj_t *s_nav_btns[UI_PAGE_COUNT];
static lv_obj_t *s_content_area;
static lv_obj_t *s_nav;
static lv_obj_t *s_live2d_page;
static ui_page_id_t s_current = UI_PAGE_HOME;

static lv_obj_t *(*const s_page_creators[UI_PAGE_COUNT])(lv_obj_t *) = {
    ui_page_home_create,
    ui_page_chat_create,
    ui_page_music_create,
    ui_page_settings_create,
};

static const char *const s_nav_labels[UI_PAGE_COUNT] = {
    UI_STR_HOME, UI_STR_CHAT, UI_STR_MUSIC, UI_STR_SETTINGS,
};

static void nav_click_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_current_target(event);
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        if (s_nav_btns[i] == button)
        {
            ui_pages_show((ui_page_id_t)i);
            return;
        }
    }
}

static void update_nav_style(void)
{
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        bool selected = i == (int)s_current;
        lv_obj_set_style_bg_color(s_nav_btns[i], selected ? UI_COLOR_PRIMARY_SOFT : UI_COLOR_SURFACE,
                                  LV_PART_MAIN);
        lv_obj_set_style_text_color(s_nav_btns[i], selected ? UI_COLOR_PRIMARY_DARK : UI_COLOR_TEXT_DIM,
                                    LV_PART_MAIN);
        lv_obj_set_style_border_width(s_nav_btns[i], selected ? 1 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_nav_btns[i], UI_COLOR_PRIMARY, LV_PART_MAIN);
    }
}

void ui_pages_create(lv_obj_t *parent)
{
    lv_obj_t *content_area = lv_obj_create(parent);
    lv_obj_set_size(content_area, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_pos(content_area, 0, 0);
    lv_obj_set_style_bg_opa(content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content_area, 0, 0);
    lv_obj_clear_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
    s_content_area = content_area;

    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        s_pages[i] = s_page_creators[i](content_area);
        if (i != UI_PAGE_HOME) lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *nav = lv_obj_create(parent);
    lv_obj_set_size(nav, UI_SCREEN_W, UI_NAV_HEIGHT);
    lv_obj_set_pos(nav, 0, UI_CONTENT_H);
    lv_obj_set_style_bg_color(nav, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_90, 0);
    lv_obj_set_style_border_width(nav, 1, 0);
    lv_obj_set_style_border_color(nav, UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(nav, 6, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    s_nav = nav;

    lv_coord_t button_width = UI_SCREEN_W / UI_PAGE_COUNT;
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        lv_obj_t *button = lv_button_create(nav);
        lv_obj_set_size(button, button_width - 8, UI_NAV_HEIGHT - 12);
        lv_obj_set_pos(button, i * button_width + 4, 6);
        lv_obj_set_style_radius(button, UI_RADIUS_SMALL, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, nav_click_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, s_nav_labels[i]);
        lv_obj_center(label);
        s_nav_btns[i] = button;
    }
    update_nav_style();

    /* 全屏 Live2D 互动页（覆盖整个屏幕，默认隐藏） */
    s_live2d_page = ui_page_live2d_create(parent);
    lv_obj_add_flag(s_live2d_page, LV_OBJ_FLAG_HIDDEN);
}

void ui_pages_show(ui_page_id_t id)
{
    if (id >= UI_PAGE_COUNT) return;
    for (int i = 0; i < UI_PAGE_COUNT; i++)
    {
        if (i == (int)id) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_current = id;
    update_nav_style();
}

ui_page_id_t ui_pages_current(void)
{
    return s_current;
}

void ui_pages_show_live2d(void)
{
    if (s_live2d_page == NULL) return;
    /* 隐藏常规内容区与底部导航 */
    if (s_content_area) lv_obj_add_flag(s_content_area, LV_OBJ_FLAG_HIDDEN);
    if (s_nav) lv_obj_add_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < UI_PAGE_COUNT; i++) lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_live2d_page, LV_OBJ_FLAG_HIDDEN);
    ui_page_live2d_show();
}

void ui_pages_return_home(void)
{
    if (s_live2d_page) lv_obj_add_flag(s_live2d_page, LV_OBJ_FLAG_HIDDEN);
    if (s_content_area) lv_obj_clear_flag(s_content_area, LV_OBJ_FLAG_HIDDEN);
    if (s_nav) lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
    ui_pages_show(UI_PAGE_HOME);
}

bool ui_pages_live2d_active(void)
{
    return s_live2d_page && !lv_obj_has_flag(s_live2d_page, LV_OBJ_FLAG_HIDDEN);
}
