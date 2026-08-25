/**
 * @file i2s_audio.h
 * @brief I2S 音频驱动：ICS-43434 数字麦克风（RX）+ MAX98357A D 类功放（TX）
 *
 * 硬件连接（ESP32-S3）：
 *   I2S_BCLK = GPIO17（共享）
 *   I2S_WS   = GPIO8 （共享）
 *   I2S_DIN  = GPIO18（ICS-43434 SD，麦克风数据输入）
 *   I2S_DOUT = GPIO7 （MAX98357A DIN，功放数据输出）
 *   SPKMODE  = GPIO6 （MAX98357A 增益/通道选择）
 *   麦克风 LR 接地 → 左声道
 *
 * 协议要点：
 *   - ICS-43434：I2S 从机，24-bit 二补码，必须 64 BCLK/帧（BCLK = WS*64）
 *   - MAX98357A：I2S 从机，16/24/32-bit，8-96kHz，免 MCLK
 *   - 两者共享 WS/BCLK → 全双工环回（麦克风拾音 → 功放外放）
 *
 * 实现采用 ESP-IDF esp_driver_i2s 标准模式（I2S_STD），32-bit slot（=64 BCLK/帧）。
 * 麦克风 24-bit 有效数据在 32-bit slot 高位，读取后右移 8 位得到 24-bit。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 引脚配置（可通过 platformio.ini build_flags 覆盖）                    */
/* ------------------------------------------------------------------ */

#ifndef I2S_AUDIO_BCLK
#define I2S_AUDIO_BCLK 17
#endif
#ifndef I2S_AUDIO_WS
#define I2S_AUDIO_WS 8
#endif
#ifndef I2S_AUDIO_DIN
#define I2S_AUDIO_DIN 18
#endif
#ifndef I2S_AUDIO_DOUT
#define I2S_AUDIO_DOUT 7
#endif
#ifndef I2S_AUDIO_SPKMODE
#define I2S_AUDIO_SPKMODE 6
#endif

/* 采样率（MAX98357A 音频模式 FIR 滤波最佳区，ICS-43434 高精度模式） */
#define I2S_AUDIO_SAMPLE_RATE 48000
/* 每帧采样数（环回缓冲） */
#define I2S_AUDIO_FRAMES_PER_BUF 128

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化 I2S 音频双通道（TX=功放，RX=麦克风）
 * @return ESP_OK 成功
 */
esp_err_t i2s_audio_init(void);

/**
 * @brief 从麦克风读取 PCM 采样
 * @param buf  输出缓冲（32-bit 采样，左对齐 24-bit）
 * @param frames  要读取的采样帧数（每个 32-bit 字为一帧）
 * @return ESP_OK 成功
 */
esp_err_t i2s_audio_read(int32_t *buf, size_t frames);

/**
 * @brief 语音录音专用读取（独占期间唯一允许读的路径）
 * 等待更久以攒够数据，供 audio_voice 录音任务使用。
 */
esp_err_t i2s_audio_read_voice(int32_t *buf, size_t frames, size_t *frames_read);

/**
 * @brief 独占麦克风读取（语音录音用）。
 * 录音期间调用 i2s_audio_read 的其它任务（如 JustFloat 轮询）会立即返回 ESP_ERR_TIMEOUT，
 * 避免把 DMA 数据抢走导致录音读不到。
 * @param exclusive true=开始独占；false=释放
 */
void i2s_audio_set_rx_exclusive(bool exclusive);

/**
 * @brief 写 PCM 采样到功放
 * @param buf  输入缓冲（32-bit 采样）
 * @param frames  采样帧数
 * @return ESP_OK 成功
 */
esp_err_t i2s_audio_write(const int32_t *buf, size_t frames);

/**
 * @brief 获取 RX 通道句柄（内部回环/诊断用）
 */
void *i2s_audio_rx_handle(void);

/**
 * @brief 环回测试：麦克风 → 功放（带音量显示）
 *
 * 循环执行：从麦克风读一帧 → 缩放 → 写功放 → 估算 RMS 打印
 * 适合验证麦克风拾音与功放输出链路。持续运行直到任务被删除。
 *
 * @param volume  环回音量（0.0 ~ 4.0，1.0 为原始音量）
 */
void i2s_audio_loopback_test(float volume);

#ifdef __cplusplus
}
#endif
