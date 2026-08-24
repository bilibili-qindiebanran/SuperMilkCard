/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new ST77926 touch driver
 *
 * @note The I2C communication should be initialized before use this function.
 * @note This driver implements the coordinate read path only. Host-download,
 *       firmware upgrade, gesture wakeup and self-test are not implemented.
 *
 * @param io LCD panel IO handle, it should be created by `esp_lcd_new_panel_io_i2c()`
 * @param config Touch panel configuration
 * @param tp Touch panel handle
 * @return
 *      - ESP_OK: on success
 */
esp_err_t esp_lcd_touch_new_i2c_st77926(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *tp);

/**
 * @brief I2C address of the ST77926 touch controller
 */
#define ESP_LCD_TOUCH_IO_I2C_ST77926_ADDRESS     (0x55)

/**
 * @brief ST77926 touch controller identification info
 */
typedef struct {
    uint8_t chip_id;       /*!< Chip ID (0x83/0x84) */
    uint8_t fw_version;    /*!< Firmware version */
    uint16_t x_res;        /*!< Raw X resolution */
    uint16_t y_res;        /*!< Raw Y resolution */
    uint8_t max_touches;   /*!< Max touch points */
} esp_lcd_touch_st77926_info_t;

/**
 * @brief Get identification info of the ST77926 touch controller
 *
 * @param tp Touch panel handle, created by `esp_lcd_touch_new_i2c_st77926()`
 * @param info Output identification info
 * @return
 *      - ESP_OK: on success
 */
esp_err_t esp_lcd_touch_st77926_get_info(esp_lcd_touch_handle_t tp, esp_lcd_touch_st77926_info_t *info);

/**
 * @brief Touch IO configuration structure
 */
#define ESP_LCD_TOUCH_IO_I2C_ST77926_CONFIG()               \
    {                                                       \
        .scl_speed_hz = 400000,                             \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_ST77926_ADDRESS,   \
        .control_phase_bytes = 1,                           \
        .lcd_cmd_bits = 16,                                 \
        .flags =                                            \
        {                                                   \
            .disable_control_phase = 1,                     \
        }                                                   \
    }

#ifdef __cplusplus
}
#endif
