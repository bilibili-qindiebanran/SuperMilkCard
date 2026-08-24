/**
 * @file i2s_audio.c
 * @brief I2S 音频驱动实现（ICS-43434 麦克风 + MAX98357A 功放）
 *
 * 使用 ESP-IDF esp_driver_i2s 标准模式（IDF v5.x+）：
 *   - 两个独立通道：TX（功放，I2S_NUM_0）、RX（麦克风，I2S_NUM_0 同总线）
 *   - i2s_new_channel + i2s_channel_init_std_mode
 *   - Philips 格式，32-bit slot / 32-bit 数据，48kHz
 *
 * 注意：ICS-43434 要求 64 BCLK/帧，因此用 32-bit slot × 2（左+右）= 64。
 * 麦克风 LR 接地输出左声道，数据在 32-bit slot 高位（24-bit 有效）。
 */

#include "i2s_audio.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2s_audio";

static i2s_chan_handle_t s_tx = NULL; /* MAX98357A 功放 */
static i2s_chan_handle_t s_rx = NULL; /* ICS-43434 麦克风 */

/* I2S 读取互斥：JustFloat 轮询与语音录音并发读同一 RX 通道，需串行化 */
static SemaphoreHandle_t s_rx_lock;

/* SPKMODE 脚配置：拉高（默认增益档，MAX98357A 手册 GAIN_SLOT=VDD 时增益 9dB 且 L+R 混合） */
static void spkmode_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << I2S_AUDIO_SPKMODE),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    ESP_LOGI(TAG, "spkmode gpio_config err=%s", esp_err_to_name(err));
    err = gpio_set_level(I2S_AUDIO_SPKMODE, 1);
    ESP_LOGI(TAG, "spkmode gpio_set_level(1) err=%s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "SPKMODE(GPIO%d) after set = %d (期望 1=Left 声道)", I2S_AUDIO_SPKMODE,
             gpio_get_level(I2S_AUDIO_SPKMODE));
}

esp_err_t i2s_audio_init(void)
{
    if (s_tx != NULL && s_rx != NULL) {
        return ESP_OK;
    }

    spkmode_init();

    /* 验证 SD_MODE 输出电平（读回确认引脚驱动正常） */
    int sd_lvl = gpio_get_level(I2S_AUDIO_SPKMODE);
    ESP_LOGI(TAG, "SD_MODE(GPIO%d) readback=%d (期望 1=Left 声道，0=Shutdown!)",
             I2S_AUDIO_SPKMODE, sd_lvl);

    /* 总线通道配置：全双工，TX+RX。
     * 显式 I2S_NUM_0（避免 AUTO 端口分配问题），dma_frame_num 匹配缓冲 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = I2S_AUDIO_FRAMES_PER_BUF; /* 128，与读写缓冲一致 */
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 时钟：48kHz，MCLK=384x（24-bit 位宽要求 MCLK 为采样率的 3 的倍数）。
     * ICS-43434 需 64 BCLK/帧，由 32-bit slot×2 保证 */
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_AUDIO_SAMPLE_RATE);
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

    /* 槽位：24-bit 数据位宽 / 32-bit slot（64 BCLK/帧，满足 ICS-43434 要求）
     * 24-bit 匹配 ICS-43434 原生数据格式，RX 读到完整 24-bit 值 */
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_STEREO);
    slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT; /* 保持 64 BCLK/帧 */

    /* 单份 GPIO 配置：BCLK=17, WS=8, DOUT=7（功放）, DIN=18（麦克风） */
    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_AUDIO_BCLK,
        .ws = I2S_AUDIO_WS,
        .dout = I2S_AUDIO_DOUT,
        .din = I2S_AUDIO_DIN, /* 麦克风 SD（IO18） */
        .invert_flags = {0},
    };
    i2s_std_config_t std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = gpio_cfg,
    };

    /* TX 通道（功放） */
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TX init_std failed: %s", esp_err_to_name(err));
        return err;
    }
    /* RX 通道（麦克风）：同总线配置 */
    err = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RX init_std failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TX enable failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50)); /* 等 TX 启动稳定 */
    err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RX enable failed: %s", esp_err_to_name(err));
        return err;
    }

    s_rx_lock = xSemaphoreCreateMutex();
    if (s_rx_lock == NULL) {
        ESP_LOGE(TAG, "rx_lock create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "I2S audio ready: BCLK=%d WS=%d DIN(mic)=%d DOUT(amp)=%d @ %d Hz",
             I2S_AUDIO_BCLK, I2S_AUDIO_WS, I2S_AUDIO_DIN, I2S_AUDIO_DOUT,
             I2S_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t i2s_audio_read(int32_t *buf, size_t frames)
{
    if (s_rx == NULL || buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 串行化：JustFloat 轮询与语音录音并发读同一 RX 通道 */
    if (s_rx_lock && xSemaphoreTake(s_rx_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    size_t bytes_read = 0;
    /* 非阻塞读取：DMA 有数据立即返回，无数据立即超时（不拖慢 JustFloat 轮询） */
    esp_err_t err = i2s_channel_read(s_rx, buf, frames * sizeof(int32_t), &bytes_read,
                                     pdMS_TO_TICKS(5));
    if (s_rx_lock) xSemaphoreGive(s_rx_lock);
    if (err != ESP_OK) {
        return err; /* 不打印日志，避免污染串口 */
    }
    return ESP_OK;
}

esp_err_t i2s_audio_write(const int32_t *buf, size_t frames)
{
    if (s_tx == NULL || buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx, buf, frames * sizeof(int32_t), &bytes_written,
                                      pdMS_TO_TICKS(200));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TX write failed: %s (bytes=%u)", esp_err_to_name(err), (unsigned)bytes_written);
        return err;
    }
    if (bytes_written != frames * sizeof(int32_t)) {
        ESP_LOGW(TAG, "TX short write: %u/%u bytes", (unsigned)bytes_written,
                 (unsigned)(frames * sizeof(int32_t)));
    }
    return ESP_OK;
}

void *i2s_audio_rx_handle(void)
{
    return s_rx;
}

void i2s_audio_loopback_test(float volume)
{
    /* 缓冲区：32-bit 采样。RX 是立体声槽（左=麦克风，右=0），只取左槽
     * 用 static 放 BSS，避免占用任务栈导致栈溢出崩溃 */
    static int32_t rx_buf[I2S_AUDIO_FRAMES_PER_BUF * 2];
    static int32_t tx_buf[I2S_AUDIO_FRAMES_PER_BUF * 2];

    /* 音量缩放（0.0 ~ 4.0），转成 Q 定点避免浮点开销 */
    int32_t vol_q = (int32_t)(volume * 4096.0f);
    if (vol_q < 0) vol_q = 0;

    uint32_t frames = I2S_AUDIO_FRAMES_PER_BUF;

    /* 自激啸叫抑制策略：
     * 1. 噪声门限：帧峰值低于门限时输出静音（切断麦克风↔扬声器正反馈回路）
     * 2. 保守 AGC：增益上限 1.0x，防止啸叫被逐级放大
     * 3. 低默认音量 */
    const int32_t noise_gate = 200;    /* 噪声门限：16-bit 有效值低于此视为噪声，输出静音 */
    int32_t agc_gain_q = 4096;         /* 初始 1.0x */
    const int32_t agc_target = 8000;   /* 目标峰值（约 -12dBFS） */
    const int32_t agc_max = 8192;      /* 最大增益 2.0x */
    const int32_t agc_min = 1024;      /* 最小增益 0.25x */

    while (1) {
        if (i2s_audio_read(rx_buf, frames) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* 一次性诊断：打印 RX 原始数据，确认 16-bit 位宽下数据格式 */
        static int diag_once;
        if (diag_once < 3) {
            diag_once++;
            ESP_LOGI(TAG, "RX raw[0..3]: 0x%08X 0x%08X 0x%08X 0x%08X",
                     (uint32_t)rx_buf[0], (uint32_t)rx_buf[1],
                     (uint32_t)rx_buf[2], (uint32_t)rx_buf[3]);
        }

        /* 统计本帧峰值（RX 是 24-bit 左对齐，右移 8 位得到 16-bit 有效值） */
        int32_t frame_peak = 0;
        for (size_t i = 0; i < frames; i++) {
            int32_t l = rx_buf[i * 2] >> 8; /* 24-bit 左对齐 → 16-bit 有效 */
            int32_t a = (l < 0) ? -l : l;
            if (a > frame_peak) frame_peak = a;
        }

        /* 噪声门限：信号过弱则完全静音（消除自激啸叫的根源） */
        if (frame_peak < noise_gate) {
            i2s_audio_write(tx_buf, frames); /* tx_buf 全 0（static 初始为 0） */
            continue;
        }

        /* 保守 AGC */
        if (frame_peak > agc_target) {
            agc_gain_q -= 64;
        } else if (frame_peak < (agc_target / 4)) {
            agc_gain_q += 16;
        }
        if (agc_gain_q > agc_max) agc_gain_q = agc_max;
        if (agc_gain_q < agc_min) agc_gain_q = agc_min;

        /* 应用 AGC + 用户音量，写入功放 */
        int64_t rms_acc = 0;
        int32_t peak = 0;
        for (size_t i = 0; i < frames; i++) {
            int32_t l = rx_buf[i * 2] >> 8;              /* 24-bit 左对齐 → 16-bit */
            int32_t v = ((int64_t)l * vol_q * agc_gain_q) >> 24; /* 缩放 */
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            tx_buf[i * 2] = v;
            tx_buf[i * 2 + 1] = v;
            int32_t a = (v < 0) ? -v : v;
            if (a > peak) peak = a;
            rms_acc += (int64_t)v * v;
        }
        i2s_audio_write(tx_buf, frames);

        /* 打印音量指示（每 ~500ms 一次，含 AGC 状态） */
        static uint32_t ticks;
        if ((xTaskGetTickCount() - ticks) > pdMS_TO_TICKS(500)) {
            ticks = xTaskGetTickCount();
            int32_t rms = (int32_t)sqrtf((float)rms_acc / frames);
            int bars = (rms * 20) / 32768;
            if (bars > 20) bars = 20;
            char bar[24];
            memset(bar, 0, sizeof(bar));
            for (int b = 0; b < bars; b++) bar[b] = '#';
            ESP_LOGI(TAG, "mic rms=%5d peak=%5d agc=%.2fx [%s]",
                     rms, peak, (float)agc_gain_q / 4096.0f, bar);
        }
    }
}
