/**
 * @file ip5306.c
 * @brief IP5306-I2C 电源管理芯片驱动实现
 *
 * 使用 ESP-IDF 新的 i2c_master 驱动（esp_i2c_master.h，IDF v5.2+）。
 * 通讯协议：
 *   WRITE: [Start][0xEA][REG][DATA][Stop]
 *   READ : [Start][0xEA][REG][Restart][0xEB][DATA][Stop]
 * 7 位从机地址 0x75，见 ip5306.h 与 docs/ip5306-i2c-通讯协议.md。
 */

#include "ip5306.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "ip5306";

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

esp_err_t ip5306_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK; /* 已初始化 */
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = IP5306_PIN_SDA,
        .scl_io_num = IP5306_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* 方便验证；量产建议外部 2.2kΩ 上拉 */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IP5306_SLAVE_ADDR, /* 0x75 */
        .scl_speed_hz = IP5306_I2C_CLK_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "IP5306 on I2C0 SDA=%d SCL=%d @ %d Hz, addr=0x%02X",
             IP5306_PIN_SDA, IP5306_PIN_SCL, IP5306_I2C_CLK_HZ, IP5306_SLAVE_ADDR);
    return ESP_OK;
}

esp_err_t ip5306_read_reg(uint8_t reg, uint8_t *val)
{
    if (s_dev == NULL || val == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 静默失败（不打印日志）：轮询阶段频繁读取，失败日志会污染 JustFloat 串口流。
     * 100ms 超时：IP5306 正常响应只需数微秒；用较大超时避免误判 NACK */
    esp_err_t err = i2c_master_transmit_receive(
        s_dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
    return err;
}

esp_err_t ip5306_write_reg(uint8_t reg, uint8_t val)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = { reg, val };
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write reg 0x%02X failed: %s", reg, esp_err_to_name(err));
    }
    return err;
}

esp_err_t ip5306_read_modify_write(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t cur = 0;
    esp_err_t err = ip5306_read_reg(reg, &cur);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t next = (uint8_t)((cur & ~mask) | (val & mask));
    if (next == cur) {
        return ESP_OK; /* 无变化，不写 */
    }
    ESP_LOGI(TAG, "reg 0x%02X: 0x%02X -> 0x%02X (mask 0x%02X)", reg, cur, next, mask);
    return ip5306_write_reg(reg, next);
}

esp_err_t ip5306_get_status(ip5306_status_t *st)
{
    if (st == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(st, 0, sizeof(*st));

    uint8_t r0 = 0, r1 = 0, r2 = 0, r3 = 0;
    esp_err_t err = ip5306_read_reg(IP5306_REG_READ0, &r0);
    if (err != ESP_OK) {
        return err;
    }
    err = ip5306_read_reg(IP5306_REG_READ1, &r1);
    if (err != ESP_OK) {
        return err;
    }
    err = ip5306_read_reg(IP5306_REG_READ2, &r2);
    if (err != ESP_OK) {
        return err;
    }
    err = ip5306_read_reg(IP5306_REG_READ3, &r3);
    if (err != ESP_OK) {
        return err;
    }

    st->charging = (r0 & IP5306_READ0_BIT_CHARGE_EN) != 0;
    st->charge_full = (r1 & IP5306_READ1_BIT_FULL) != 0;
    st->light_load = (r2 & IP5306_READ2_BIT_LIGHTLOAD) != 0;
    st->key_short = (r3 & IP5306_READ3_BIT_KEY_SHORT) != 0;
    st->key_long = (r3 & IP5306_READ3_BIT_KEY_LONG) != 0;
    st->key_double = (r3 & IP5306_READ3_BIT_KEY_DOUBLE) != 0;

    /* 按键事件按手册要求"写 1 清零"复位 */
    if (r3 != 0) {
        ip5306_write_reg(IP5306_REG_READ3, r3);
    }
    return ESP_OK;
}

int ip5306_scan_bus(void)
{
    /* 注意：i2c_master_probe 对 IP5306 这类"必须写寄存器地址才能读"的从机
     * 不适用（probe 只发地址+读，IP5306 不响应），会全部超时。
     * 因此这里改为直接尝试读 REG_READ0(0x70) 验证从机是否在线。 */
    ESP_LOGI(TAG, "checking IP5306 at 0x%02X (SDA=%d, SCL=%d)...",
             IP5306_SLAVE_ADDR, IP5306_PIN_SDA, IP5306_PIN_SCL);
    uint8_t v = 0;
    esp_err_t err = ip5306_read_reg(IP5306_REG_READ0, &v);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "IP5306 found at 0x%02X, REG_READ0(0x70)=0x%02X", IP5306_SLAVE_ADDR, v);
        return 1;
    }
    ESP_LOGE(TAG, "IP5306 not responding at 0x%02X: %s", IP5306_SLAVE_ADDR, esp_err_to_name(err));
    return 0;
}
