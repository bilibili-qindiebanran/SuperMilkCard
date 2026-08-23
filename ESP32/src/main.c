/**
 * @file main.c
 * @brief IP5306-I2C 电源管理 + I2S 音频采集，串口 JustFloat 协议输出
 *
 * 流程：
 *  1. 初始化 I2C 总线，检测 IP5306（地址 0x75）并读取电源状态
 *  2. 初始化 I2S 音频（ICS-43434 麦克风采集）
 *  3. 开机打印初始化日志后，进入 JustFloat 协议轮询输出
 *
 * JustFloat 帧（VOFA+ 可解析）：5 个 float + 帧尾 0x00 0x00 0x80 0x7F
 *   ch1=charging  ch2=charge_full  ch3=light_load
 *   ch4=麦克风 RMS  ch5=麦克风峰值
 *
 * IP5306 安全策略：默认【只读】，不做寄存器写入。
 */

#include <math.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ip5306.h"
#include "i2s_audio.h"

static const char *TAG = "main";

static void print_config_regs(void)
{
    static const struct
    {
        uint8_t reg;
        const char *name;
    } regs[] = {
        {IP5306_REG_SYS_CTL0, "SYS_CTL0    (0x00)"},
        {IP5306_REG_SYS_CTL1, "SYS_CTL1    (0x01)"},
        {IP5306_REG_SYS_CTL2, "SYS_CTL2    (0x02)"},
        {IP5306_REG_CHG_CTL0, "Charger_CTL0(0x20)"},
        {IP5306_REG_CHG_CTL1, "Charger_CTL1(0x21)"},
        {IP5306_REG_CHG_CTL2, "Charger_CTL2(0x22)"},
        {IP5306_REG_CHG_CTL3, "Charger_CTL3(0x23)"},
        {IP5306_REG_CHG_DIG_CTL0, "CHG_DIG_CTL0(0x24)"},
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
    {
        uint8_t v = 0;
        if (ip5306_read_reg(regs[i].reg, &v) == ESP_OK)
        {
            ESP_LOGI(TAG, "  %s = 0x%02X", regs[i].name, v);
        }
        else
        {
            ESP_LOGE(TAG, "  %s read failed", regs[i].name);
        }
    }
}

/* JustFloat 帧尾（VOFA+ 协议：0x00 0x00 0x80 0x7F） */
static const uint8_t JUSTFLOAT_TAIL[4] = {0x00, 0x00, 0x80, 0x7F};

static void print_status(void)
{
    ip5306_status_t st;
    if (ip5306_get_status(&st) != ESP_OK)
    {
        ESP_LOGE(TAG, "get status failed");
        return;
    }
    ESP_LOGI(TAG,
             "status: charging=%d full=%d light_load=%d | key: short=%d long=%d double=%d",
             st.charging, st.charge_full, st.light_load,
             st.key_short, st.key_long, st.key_double);
}

/**
 * @brief 以 JustFloat 协议输出 IP5306 状态 + 麦克风音量（VOFA+ 可解析）
 *
 * 帧格式：N 个 float（小端 4 字节）+ 帧尾 0x00 0x00 0x80 0x7F
 * 通道：
 *   ch1=charging  ch2=charge_full  ch3=light_load
 *   ch4=麦克风 RMS  ch5=麦克风峰值
 * 输出频率：调用方控制（当前 20Hz，每 50ms 一帧）
 */
static void print_status_justfloat(void)
{
    ip5306_status_t st;
    if (ip5306_get_status(&st) != ESP_OK)
    {
        return;
    }

    /* 读麦克风一帧，计算 RMS 和峰值 */
    static int32_t mic_buf[I2S_AUDIO_FRAMES_PER_BUF * 2];
    float mic_rms = 0.0f, mic_peak = 0.0f;
    if (i2s_audio_read(mic_buf, I2S_AUDIO_FRAMES_PER_BUF) == ESP_OK)
    {
        int64_t acc = 0;
        int32_t peak = 0;
        for (size_t i = 0; i < I2S_AUDIO_FRAMES_PER_BUF; i++)
        {
            int32_t l = mic_buf[i * 2] >> 8; /* 24-bit 左对齐 → 16-bit */
            int32_t a = (l < 0) ? -l : l;
            if (a > peak) peak = a;
            acc += (int64_t)l * l;
        }
        mic_rms = sqrtf((float)acc / I2S_AUDIO_FRAMES_PER_BUF);
        mic_peak = (float)peak;
    }

    float data[5] = {
        st.charging ? 1.0f : 0.0f,
        st.charge_full ? 1.0f : 0.0f,
        st.light_load ? 1.0f : 0.0f,
        mic_rms,
        mic_peak,
    };
    /* 直接写二进制到 console（USB-Serial/JTAG），避免日志时间戳污染 */
    fwrite(data, sizeof(float), 5, stdout);
    fwrite(JUSTFLOAT_TAIL, 1, 4, stdout);
    fflush(stdout);
}

void app_main(void)
{
    ESP_LOGI(TAG, "IP5306-I2C comm test starting...");

    esp_err_t err = ip5306_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ip5306_init failed, abort");
        return;
    }

    /* 0. 直接读 IP5306 状态寄存器验证在线（i2c_master_probe 对 IP5306 不适用） */
    uint8_t reg0 = 0xFF;
    err = ip5306_read_reg(IP5306_REG_READ0, &reg0);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, ">>> IP5306 online! REG_READ0(0x70)=0x%02X (bit3 charge_en=%d)",
                 reg0, (reg0 >> 3) & 1);
    }
    else
    {
        ESP_LOGE(TAG,
                 ">>> IP5306 not responding: %s. Check wiring SDA=%d/SCL=%d, 2.2k pull-up, "
                 "chip power (BAT/VIN), and that it's the IP5306_I2C custom version.",
                 esp_err_to_name(err), IP5306_PIN_SDA, IP5306_PIN_SCL);
        return;
    }

    /* 1. 扫描（确认在线并打印） */
    int found = ip5306_scan_bus();
    if (found == 0)
    {
        ESP_LOGE(TAG, "IP5306 scan failed, abort.");
        return;
    }

    /* 2. 出厂配置寄存器 */
    ESP_LOGI(TAG, "--- factory config registers ---");
    print_config_regs();

    /* 3. 电源状态 */
    ESP_LOGI(TAG, "--- power status ---");
    print_status();

    /* 4. I2S 音频初始化（ICS-43434 麦克风 + MAX98357A 功放） */
    ESP_LOGI(TAG, "--- I2S audio init ---");
    err = i2s_audio_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2s_audio_init failed: %s, abort audio test.", esp_err_to_name(err));
    }
    else
    {
        /* I2S 音频初始化成功，但不启用环回（避免啸叫）。
         * 麦克风/功放已就绪，供后续功能使用。 */
        ESP_LOGI(TAG, "I2S audio ready (mic + amp), loopback disabled.");
    }

    /* IP5306 状态轮询（JustFloat 协议输出，供 VOFA+ 等上位机解析） */
    while (1)
    {
        print_status_justfloat();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
