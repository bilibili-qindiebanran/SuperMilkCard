/**
 * @file ui_page_live2d.c
 * @brief 全屏 Live2D 互动页实现
 *
 * 表情绘制：LVGL 基础图元（圆/弧/线/文本）在中央区域绘制。
 * 动效：2–8 FPS 的眨眼/呼吸/嘴型微动，不做全屏刷新。
 * 数据源：只读 app_state 快照（TCP 任务发布），不直接操作网络。
 */

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "../../audio_voice.h"
#include "../../network/net_tcp.h"
#include "../app_state.h"
#include "../ui_pages.h"
#include "../ui_strings.h"
#include "../ui_theme.h"
#include "ui_page_live2d.h"

#define LIVE2D_FACE_CX 240
#define LIVE2D_FACE_CY 118
#define LIVE2D_FACE_R 56
#define LIVE2D_FACE_COLOR lv_color_hex(0xFFE9C9) /* 肤色 */

/* 表情描述 */
typedef struct {
    app_expr_t expr;
    const char *name;   /* 中文标签 */
    float eye_open;     /* 0=闭眼 1=全开 */
    float mouth;        /* 0=闭合 1=微笑 2=开口 3=惊讶O 4=嘟嘴 */
    int brow;           /* 0=平 1=上挑 2=下垂 */
    int extra;          /* 0=无 1=腮红 2=问号 3=泪点 4=怒气 5=汗滴 */
} face_style_t;

static const face_style_t s_styles[] = {
    {APP_EXPR_NEUTRAL,   "平静",  1.0f, 1, 0, 0},
    {APP_EXPR_HAPPY,     "开心",  0.75f, 2, 1, 1},
    {APP_EXPR_SAD,       "难过",  0.5f, 4, 2, 3},
    {APP_EXPR_ANGRY,     "生气",  0.85f, 4, 2, 4},
    {APP_EXPR_SURPRISED, "惊讶",  1.5f, 3, 1, 5},
    {APP_EXPR_THINKING,  "思考",  0.5f, 4, 0, 2},
};

static lv_obj_t *s_page;
static lv_obj_t *s_face;         /* 圆脸 */
static lv_obj_t *s_eye_l;
static lv_obj_t *s_eye_r;
static lv_obj_t *s_brow_l;
static lv_obj_t *s_brow_r;
static lv_obj_t *s_mouth;        /* 嘴（圆弧/弧） */
static lv_obj_t *s_expr_label;   /* 表情名 */
static lv_obj_t *s_conn_value;
static lv_obj_t *s_msg_value;
static lv_obj_t *s_back_btn;

static uint32_t s_anim_tick;     /* 动效计数器 */

/* ------------------------------------------------------------------ */
/* 图元辅助                                                           */
/* ------------------------------------------------------------------ */

static lv_obj_t *dot_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t r,
                            lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_pos(o, x - r, y - r);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
}

/* 绘制一条线（线段从 (x0,y0) 到 (x1,y1)，宽度 w） */
static lv_obj_t *line_create(lv_obj_t *parent, lv_coord_t x0, lv_coord_t y0,
                             lv_coord_t x1, lv_coord_t y1, lv_coord_t w, lv_color_t color)
{
    lv_obj_t *o = lv_line_create(parent);
    static lv_point_precise_t points[2];
    points[0].x = x0;
    points[0].y = y0;
    points[1].x = x1;
    points[1].y = y1;
    lv_line_set_points(o, points, 2);
    lv_obj_set_style_line_width(o, w, 0);
    lv_obj_set_style_line_color(o, color, 0);
    lv_obj_set_style_line_rounded(o, true, 0);
    return o;
}

/* 创建一条弧线（角度制，angle_start 到 angle_end，逆时针） */
static lv_obj_t *arc_create(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy, lv_coord_t r,
                            lv_coord_t w, lv_coord_t start, lv_coord_t end, lv_color_t color)
{
    lv_obj_t *o = lv_arc_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_style_arc_width(o, w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(o, color, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(o, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(o, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(o, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(o, w, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(o, start, end);
    lv_arc_set_rotation(o, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* 绘制小文字（表情名等） */
static lv_obj_t *text_create(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                             lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_font(l, UI_FONT_DEFAULT, 0);
    return l;
}

/* ------------------------------------------------------------------ */
/* 表情图元布局                                                       */
/* ------------------------------------------------------------------ */

/* 设置嘴巴（按 face_style.mouth） */
static void mouth_apply(int kind)
{
    if (!s_mouth) return;
    lv_obj_del(s_mouth);
    s_mouth = NULL;

    lv_coord_t cx = LIVE2D_FACE_CX;
    lv_coord_t cy = LIVE2D_FACE_CY + 24;

    switch (kind)
    {
    case 1: /* 微笑：下凹弧 */
        s_mouth = arc_create(s_page, cx, cy + 2, 14, 4, 20, 160, UI_COLOR_TEXT);
        break;
    case 2: /* 开口笑：小圆 */
        s_mouth = dot_create(s_page, cx, cy + 3, 8, UI_COLOR_TEXT);
        break;
    case 3: /* 惊讶 O：稍大圆 */
        s_mouth = dot_create(s_page, cx, cy + 3, 10, UI_COLOR_TEXT);
        break;
    case 4: /* 嘟嘴/难过：上凹弧 */
        s_mouth = arc_create(s_page, cx, cy - 2, 12, 4, 160, 380, UI_COLOR_TEXT);
        break;
    default: /* 0=闭合 */
        s_mouth = line_create(s_page, cx - 12, cy, cx + 12, cy, 4, UI_COLOR_TEXT);
        break;
    }
}

/* 设置眉毛（按 face_style.brow） */
static void brow_apply(int kind)
{
    if (!s_brow_l || !s_brow_r) return;
    lv_coord_t y = LIVE2D_FACE_CY - 30;
    lv_point_precise_t pts_l[2], pts_r[2];
    switch (kind)
    {
    case 1: /* 上挑（外高内低）：左眉/右眉都外高 */
        pts_l[0].x = LIVE2D_FACE_CX - 38; pts_l[0].y = y - 6;
        pts_l[1].x = LIVE2D_FACE_CX - 12; pts_l[1].y = y;
        pts_r[0].x = LIVE2D_FACE_CX + 12; pts_r[0].y = y;
        pts_r[1].x = LIVE2D_FACE_CX + 38; pts_r[1].y = y - 6;
        break;
    case 2: /* 下垂（内高外低）：眉尾向内下压 */
        pts_l[0].x = LIVE2D_FACE_CX - 38; pts_l[0].y = y;
        pts_l[1].x = LIVE2D_FACE_CX - 12; pts_l[1].y = y + 6;
        pts_r[0].x = LIVE2D_FACE_CX + 12; pts_r[0].y = y + 6;
        pts_r[1].x = LIVE2D_FACE_CX + 38; pts_r[1].y = y;
        break;
    default: /* 平 */
        pts_l[0].x = LIVE2D_FACE_CX - 38; pts_l[0].y = y;
        pts_l[1].x = LIVE2D_FACE_CX - 12; pts_l[1].y = y;
        pts_r[0].x = LIVE2D_FACE_CX + 12; pts_r[0].y = y;
        pts_r[1].x = LIVE2D_FACE_CX + 38; pts_r[1].y = y;
        break;
    }
    lv_line_set_points(s_brow_l, pts_l, 2);
    lv_line_set_points(s_brow_r, pts_r, 2);
}

/* 额外元素（腮红/问号/泪点/怒气/汗滴），由表情切换时重建 */
static void extras_apply(int kind)
{
    static lv_obj_t *s_extra = NULL;
    if (s_extra)
    {
        lv_obj_del(s_extra);
        s_extra = NULL;
    }
    switch (kind)
    {
    case 1: /* 腮红 */
        s_extra = dot_create(s_page, LIVE2D_FACE_CX - 40, LIVE2D_FACE_CY + 14, 7,
                             lv_color_hex(0xFF9BB3));
        s_extra = dot_create(s_page, LIVE2D_FACE_CX + 40, LIVE2D_FACE_CY + 14, 7,
                             lv_color_hex(0xFF9BB3));
        break;
    case 2: /* 问号（思考） */
        s_extra = text_create(s_page, "?", LIVE2D_FACE_CX + 42, LIVE2D_FACE_CY - 44,
                              UI_COLOR_PRIMARY);
        lv_obj_set_style_text_font(s_extra, UI_FONT_DEFAULT, 0);
        break;
    case 3: /* 泪点 */
        s_extra = dot_create(s_page, LIVE2D_FACE_CX - 48, LIVE2D_FACE_CY + 8, 3,
                             lv_color_hex(0x4AA8FF));
        s_extra = dot_create(s_page, LIVE2D_FACE_CX + 48, LIVE2D_FACE_CY + 8, 3,
                             lv_color_hex(0x4AA8FF));
        break;
    case 4: /* 怒气 */
        s_extra = line_create(s_page, LIVE2D_FACE_CX - 46, LIVE2D_FACE_CY - 46,
                              LIVE2D_FACE_CX - 34, LIVE2D_FACE_CY - 58, 3,
                              lv_color_hex(0xD13438));
        break;
    case 5: /* 汗滴 */
        s_extra = dot_create(s_page, LIVE2D_FACE_CX + 42, LIVE2D_FACE_CY - 40, 4,
                             lv_color_hex(0x4AA8FF));
        break;
    default:
        break;
    }
}

/* 应用表情：眼睛开度 + 嘴 + 眉 + 额外元素 */
static void face_apply(app_expr_t expr)
{
    const face_style_t *st = &s_styles[0];
    for (size_t i = 0; i < sizeof(s_styles) / sizeof(s_styles[0]); i++)
    {
        if (s_styles[i].expr == expr)
        {
            st = &s_styles[i];
            break;
        }
    }

    /* 眼睛开度（x 缩放） */
    float open = st->eye_open;
    lv_coord_t eye_r = 9;
    lv_obj_set_size(s_eye_l, (lv_coord_t)(eye_r * 2 * open), eye_r * 2);
    lv_obj_set_size(s_eye_r, (lv_coord_t)(eye_r * 2 * open), eye_r * 2);

    mouth_apply(st->mouth);
    brow_apply(st->brow);
    extras_apply(st->extra);
}

/* ------------------------------------------------------------------ */
/* 刷新                                                               */
/* ------------------------------------------------------------------ */

void ui_page_live2d_refresh(void)
{
    if (!s_page) return;
    if (!ui_pages_live2d_active()) return; /* 未在互动页时跳过（省渲染） */

    const app_state_t *state = app_state_get();

    /* 连接状态 */
    if (state->live2d.connected)
    {
        lv_label_set_text(s_conn_value, UI_STR_LIVE2D_CONN);
        ui_theme_apply_status_text(s_conn_value, 1);
    }
    else
    {
        lv_label_set_text(s_conn_value, UI_STR_LIVE2D_DISC);
        ui_theme_apply_status_text(s_conn_value, 0);
    }

    /* 表情名 */
    lv_label_set_text(s_expr_label, s_styles[(int)state->live2d.expression % 6].name);

    /* 消息摘要（滚动省略号） */
    if (state->live2d.message_preview[0])
    {
        lv_label_set_text(s_msg_value, state->live2d.message_preview);
    }
    else
    {
        lv_label_set_text(s_msg_value, UI_STR_LIVE2D_WAIT);
    }

    /* 表情只在变化时重建（expression 变化触发） */
    static app_expr_t s_last_expr = APP_EXPR_NEUTRAL;
    if (state->live2d.expression != s_last_expr)
    {
        s_last_expr = state->live2d.expression;
        face_apply(state->live2d.expression);
    }
    else
    {
        /* 低频动效：眨眼（每 40 帧半闭一次）/呼吸 */
        s_anim_tick++;
        if ((s_anim_tick % 40) == 0)
        {
            face_apply(state->live2d.expression);
        }
    }
}

/* ------------------------------------------------------------------ */
/* 交互：返回桌面                                                     */
/* ------------------------------------------------------------------ */

void ui_page_live2d_return_cb(lv_event_t *event)
{
    (void)event;
    ui_pages_return_home();
}

/* ------------------------------------------------------------------ */
/* 交互：录音（点击开始/停止 → Windows STT）                            */
/* ------------------------------------------------------------------ */

static lv_obj_t *s_record_btn;
static lv_obj_t *s_record_label;

static void record_click_cb(lv_event_t *event)
{
    (void)event;
    if (audio_voice_is_recording())
    {
        audio_voice_stop();
        lv_label_set_text(s_record_label, UI_STR_LIVE2D_RECORD);
        app_state_publish_session(APP_MODE_LIVE2D_LINK, APP_SESSION_LINKED_IDLE, NULL);
    }
    else
    {
        esp_err_t err = audio_voice_start();
        if (err == ESP_OK)
        {
            lv_label_set_text(s_record_label, UI_STR_LIVE2D_RECORDING);
            app_state_publish_session(APP_MODE_LIVE2D_LINK, APP_SESSION_LINKED_LISTENING, NULL);
        }
    }
}

void ui_page_live2d_show(void)
{
    /* 通知 Windows：用户进入互动页 */
    net_tcp_send_live2d_command("enter");
    s_anim_tick = 0;
}

/* ------------------------------------------------------------------ */
/* 创建                                                               */
/* ------------------------------------------------------------------ */

lv_obj_t *ui_page_live2d_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    s_page = page;

    /* 顶部标题栏：返回桌面 + 标题 */
    lv_obj_t *bar = lv_obj_create(page);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, UI_SCREEN_W, 48);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_back_btn = lv_button_create(bar);
    lv_obj_set_size(s_back_btn, 110, 36);
    lv_obj_set_pos(s_back_btn, UI_MARGIN, 6);
    ui_theme_apply_button(s_back_btn, false);
    lv_obj_t *back_label = lv_label_create(s_back_btn);
    lv_label_set_text(back_label, UI_STR_LIVE2D_BACK);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(s_back_btn, ui_page_live2d_return_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, UI_STR_LIVE2D_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    ui_theme_apply_title(title);

    /* 表情区（脸 + 眼睛 + 眉毛），坐标以 LIVE2D_FACE_CX/CY 为中心 */
    s_face = dot_create(page, LIVE2D_FACE_CX, LIVE2D_FACE_CY, LIVE2D_FACE_R, LIVE2D_FACE_COLOR);

    s_eye_l = dot_create(page, LIVE2D_FACE_CX - 20, LIVE2D_FACE_CY - 6, 9, UI_COLOR_TEXT);
    s_eye_r = dot_create(page, LIVE2D_FACE_CX + 20, LIVE2D_FACE_CY - 6, 9, UI_COLOR_TEXT);

    s_brow_l = line_create(page, LIVE2D_FACE_CX - 38, LIVE2D_FACE_CY - 30,
                           LIVE2D_FACE_CX - 12, LIVE2D_FACE_CY - 30, 3, UI_COLOR_TEXT);
    s_brow_r = line_create(page, LIVE2D_FACE_CX + 12, LIVE2D_FACE_CY - 30,
                           LIVE2D_FACE_CX + 38, LIVE2D_FACE_CY - 30, 3, UI_COLOR_TEXT);

    mouth_apply(0); /* 默认闭合嘴 */

    /* 表情名标签（脸下方） */
    s_expr_label = text_create(page, "平静", LIVE2D_FACE_CX - 30, LIVE2D_FACE_CY + 58,
                               UI_COLOR_TEXT_DIM);
    lv_obj_set_width(s_expr_label, 60);
    lv_label_set_long_mode(s_expr_label, LV_LABEL_LONG_DOT);

    /* 底部状态卡：连接状态 + 摘要 */
    lv_obj_t *info_card = lv_obj_create(page);
    lv_obj_set_pos(info_card, UI_MARGIN, UI_SCREEN_H - 62);
    lv_obj_set_size(info_card, UI_SCREEN_W - UI_MARGIN * 2, 50);
    ui_theme_apply_card(info_card);

    s_conn_value = lv_label_create(info_card);
    lv_label_set_text(s_conn_value, UI_STR_LIVE2D_DISC);
    lv_obj_set_pos(s_conn_value, UI_GAP, 6);
    ui_theme_apply_status_text(s_conn_value, 0);

    s_msg_value = lv_label_create(info_card);
    lv_label_set_text(s_msg_value, UI_STR_LIVE2D_WAIT);
    lv_obj_set_pos(s_msg_value, UI_GAP, 28);
    lv_obj_set_width(s_msg_value, UI_SCREEN_W - UI_MARGIN * 2 - UI_GAP * 2 - 120);
    lv_label_set_long_mode(s_msg_value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_msg_value, UI_COLOR_TEXT_DIM, 0);

    /* 录音按钮（底部状态卡右下角，点击开始/停止） */
    s_record_btn = lv_button_create(page);
    lv_obj_set_size(s_record_btn, 110, 38);
    lv_obj_set_pos(s_record_btn, UI_SCREEN_W - UI_MARGIN - 110, UI_SCREEN_H - 56);
    ui_theme_apply_button(s_record_btn, true);
    s_record_label = lv_label_create(s_record_btn);
    lv_label_set_text(s_record_label, UI_STR_LIVE2D_RECORD);
    lv_obj_center(s_record_label);
    lv_obj_add_event_cb(s_record_btn, record_click_cb, LV_EVENT_CLICKED, NULL);

    face_apply(APP_EXPR_NEUTRAL);
    return page;
}
