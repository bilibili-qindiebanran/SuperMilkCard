/**
 * @file i2c_bus.c
 * @brief 共享 I2C 总线模块实现
 */

#include "i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "i2c_bus";

/* 引脚配置（与 platformio.ini 一致） */
#ifndef I2C_BUS_PIN_SDA
#define I2C_BUS_PIN_SDA 9
#endif
#ifndef I2C_BUS_PIN_SCL
#define I2C_BUS_PIN_SCL 10
#endif
#define I2C_BUS_CLK_HZ 400000 /* 400kHz（IP5306 与触摸均支持） */

static i2c_master_bus_handle_t s_bus = NULL;

esp_err_t i2c_bus_init(void)
{
    if (s_bus != NULL)
    {
        return ESP_OK; /* 已初始化 */
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_BUS_PIN_SDA,
        .scl_io_num = I2C_BUS_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* 方便验证；量产建议外部 2.2kΩ 上拉 */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C0 bus ready: SDA=%d SCL=%d @ %d Hz",
             I2C_BUS_PIN_SDA, I2C_BUS_PIN_SCL, I2C_BUS_CLK_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get(void)
{
    return s_bus;
}

esp_err_t i2c_bus_add_device(uint8_t addr, uint32_t clk_hz, i2c_master_dev_handle_t *out_dev)
{
    if (s_bus == NULL || out_dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = clk_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, out_dev);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "add device 0x%02X failed: %s", addr, esp_err_to_name(err));
    }
    return err;
}
