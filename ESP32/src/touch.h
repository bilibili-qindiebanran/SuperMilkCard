/**
 * @file touch.h
 * @brief 触摸屏驱动：探测 → 采集 → 坐标转换 → 事件队列
 *
 * 架构：
 *   - 探测：优先官方 ST77926 触摸组件（I2C 0x55，CHIP_ID 0x83/0x84），
 *     失败则扫描总线做通用探测（输出地址 + 安全寄存器读取）
 *   - 采集：专用 touch_task，默认轮询；TP_INT 极性确认后启用 GPIO ISR
 *     （ISR 只投递通知，I2C 读取始终在任务侧）
 *   - 坐标转换：raw 坐标与逻辑坐标分离，支持 0/90/180/270 旋转 + mirror
 *   - 事件队列：触点事件（Down/Move/Up）经 FreeRTOS queue 交给 UI
 *
 * 引脚：TP_RST=GPIO42（输出）、TP_INT=GPIO39（输入）、I2C0 共用总线
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 引脚定义（platformio.ini 已定义，这里提供默认值兜底）              */
/* ------------------------------------------------------------------ */
#ifndef TOUCH_PIN_TP_INT
#define TOUCH_PIN_TP_INT 39
#endif
#ifndef TOUCH_PIN_TP_RST
#define TOUCH_PIN_TP_RST 42
#endif

/* 触摸 I2C 地址（ST77926 触摸控制器） */
#define TOUCH_I2C_ADDR_ST77926 0x55

/* 最大触点 */
#define TOUCH_MAX_POINTS 10

/* ------------------------------------------------------------------ */
/* 触摸事件                                                            */
/* ------------------------------------------------------------------ */
typedef enum {
    TOUCH_EVT_NONE = 0,
    TOUCH_EVT_DOWN,   /* 按下 */
    TOUCH_EVT_MOVE,   /* 移动 */
    TOUCH_EVT_UP,     /* 抬起 */
} touch_evt_t;

typedef struct {
    uint8_t id;              /* 触点 id（track id） */
    touch_evt_t event;       /* 事件类型 */
    uint16_t raw_x;          /* 控制器原始 X */
    uint16_t raw_y;          /* 控制器原始 Y */
    uint16_t x;              /* 逻辑坐标 X（旋转/镜像后） */
    uint16_t y;              /* 逻辑坐标 Y */
    uint16_t pressure;       /* 压力/面积（控制器支持时） */
    uint32_t timestamp_ms;   /* 事件时间戳 */
} touch_event_t;

/* ------------------------------------------------------------------ */
/* 控制器识别信息                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    bool detected;           /* 是否识别到控制器 */
    const char *name;        /* 控制器型号名（"ST77926 Touch" 等） */
    uint8_t i2c_addr;        /* I2C 地址 */
    uint8_t chip_id;         /* 芯片 ID */
    uint16_t raw_x_max;      /* 原始 X 范围 */
    uint16_t raw_y_max;      /* 原始 Y 范围 */
    uint8_t max_touch;       /* 最大触点数 */
    bool interrupt_active_high; /* TP_INT 有效电平（探测结果） */
} touch_info_t;

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化触摸子系统：总线探测 → 控制器识别 → 启动采集任务
 * @return ESP_OK 成功（含"识别失败但已输出诊断"的情况）
 */
esp_err_t touch_init(void);

/**
 * @brief 获取触摸事件（阻塞等待，UI 任务用）
 * @param evt 输出事件
 * @param timeout_ms 超时
 * @return ESP_OK 收到事件；ESP_ERR_TIMEOUT 超时
 */
esp_err_t touch_get_event(touch_event_t *evt, uint32_t timeout_ms);

/**
 * @brief 获取控制器识别信息
 */
const touch_info_t *touch_get_info(void);

/**
 * @brief 设置屏幕旋转角度（0/90/180/270）
 * @return ESP_OK 成功
 */
esp_err_t touch_set_rotation(int angle_deg);

/**
 * @brief 获取当前旋转角度
 */
int touch_get_rotation(void);

/**
 * @brief 获取 TP_INT 当前电平
 */
int touch_get_int_level(void);

/**
 * @brief 是否已启用中断模式（false=轮询模式）
 */
bool touch_is_interrupt_mode(void);

#ifdef __cplusplus
}
#endif
