/**
 * @file uart_justfloat.c
 * @brief UART0 JustFloat 协议输出实现
 *
 * 帧格式：N 个 float（小端 4 字节）+ 帧尾 0x00 0x00 0x80 0x7F
 * 使用 ESP-IDF uart 驱动，输出到 GPIO43/44（需外接 USB 转串口）。
 */

#include "uart_justfloat.h"

#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "uart_jf";

/* JustFloat 帧尾（VOFA+ 协议） */
static const uint8_t JUSTFLOAT_TAIL[4] = {0x00, 0x00, 0x80, 0x7F};

esp_err_t uart_justfloat_init(void)
{
    uart_config_t cfg = {
        .baud_rate = JUSTFLOAT_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(JUSTFLOAT_UART_NUM, 1024, 1024, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(JUSTFLOAT_UART_NUM, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_set_pin(JUSTFLOAT_UART_NUM, JUSTFLOAT_UART_TX, JUSTFLOAT_UART_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "UART0 ready: TX=GPIO%d RX=GPIO%d @ %d baud (JustFloat)",
             JUSTFLOAT_UART_TX, JUSTFLOAT_UART_RX, JUSTFLOAT_UART_BAUD);
    return ESP_OK;
}

void uart_justfloat_send(const float *data, size_t count)
{
    if (data == NULL || count == 0) {
        return;
    }
    /* 发送 float 数组（直接按字节发，小端） */
    uart_write_bytes(JUSTFLOAT_UART_NUM, (const char *)data, count * sizeof(float));
    /* 发送帧尾 */
    uart_write_bytes(JUSTFLOAT_UART_NUM, (const char *)JUSTFLOAT_TAIL, 4);
}
