/**
 * @file ui_page_direct_chat.h
 * @brief 独立角色对话页（全屏）
 *
 * 交互：
 *   - 按住底部录音按钮 → ESP32 本地 STT → 独立角色 LLM（llm_role）
 *   - 对话历史（我/角色气泡）按时间显示，自动换行并滚动到底
 *   - 顶部返回桌面；LLM 请求期间禁用录音，避免并发会话
 *   - 不跳转 Windows Live2D 页面
 *
 * 线程模型：STT 文本与 LLM 结果由其它任务经回调进入本模块的
 * 加锁暂存区，LVGL 刷新（ui_page_direct_chat_refresh）周期消费；
 * 回调内绝不操作 LVGL 对象。
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建独立对话页（覆盖整个屏幕，默认隐藏；由 ui_pages_create 调用）
 */
lv_obj_t *ui_page_direct_chat_create(lv_obj_t *parent);

/** @brief 进入独立对话页时调用（发布 DIRECT 会话模式、重置瞬态状态） */
void ui_page_direct_chat_show(void);

/** @brief 离开独立对话页时调用（置回 NONE 模式、取消未完成任务、解除 STT sink） */
void ui_page_direct_chat_hide(void);

/** @brief 周期刷新（消费 STT/LLM 暂存结果，更新状态行） */
void ui_page_direct_chat_refresh(void);

#ifdef __cplusplus
}
#endif
