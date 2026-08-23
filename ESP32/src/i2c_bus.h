/**
 * @file i2c_bus.h
 * @brief 共享 I2C 总线模块：唯一创建并持有 I2C_NUM_0 总线
 *
 * 背景：IP5306（0x75）与触摸控制器（0x55）共用 I2C0（SDA=GPIO9 / SCL=GPIO10）。
 * 本模块唯一创建总线，各外设通过 i2c_bus_add_device() 申请自己的设备句柄，
 * 禁止再次调用 i2c_new_master_bus() 重复建总线。
 *
 * 互斥：依赖 IDF i2c_master 驱动内建线程安全（同一 device handle 的访问
 *       在驱动内部串行化），应用层无需额外加锁。
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化共享 I2C0 总线（SDA=GPIO9, SCL=GPIO10, 400kHz, 内部上拉）
 * @return ESP_OK 成功（重复调用直接返回 ESP_OK）
 */
esp_err_t i2c_bus_init(void);

/**
 * @brief 获取共享总线句柄（供 esp_lcd_new_panel_io_i2c() 复用）
 * @return 总线句柄，未初始化时为 NULL
 */
i2c_master_bus_handle_t i2c_bus_get(void);

/**
 * @brief 在共享总线上注册一个 7 位地址设备
 * @param addr  7 位从机地址
 * @param clk_hz  I2C 时钟频率
 * @param out_dev  输出设备句柄
 * @return ESP_OK 成功
 */
esp_err_t i2c_bus_add_device(uint8_t addr, uint32_t clk_hz, i2c_master_dev_handle_t *out_dev);

#ifdef __cplusplus
}
#endif
