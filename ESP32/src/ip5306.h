/**
 * @file ip5306.h
 * @brief IP5306-I2C 电源管理芯片驱动（英集芯，2.1A/2.4A 移动电源 SoC）
 *
 * 通讯协议要点（详见 docs/ip5306-i2c-通讯协议.md）：
 *  - I2C 从机，最高 400kbps，8-bit 寄存器地址 + 8-bit 数据，MSB 先行
 *  - 7 位从机地址 0x75（8 位写地址 0xEA / 读地址 0xEB）
 *  - 寄存器写入必须"读 -> 改 -> 写"：保留 Reserved 位原值，不能随意改动
 *  - 芯片内部无电压/电流 ADC，只提供状态标志位（充电/充满/轻载/按键事件）
 *
 * 注意：IP5306 标准品默认不支持 I2C，需单独定制 IP5306_I2C 版本。
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 硬件配置                                                             */
/* ------------------------------------------------------------------ */

#ifndef IP5306_PIN_SDA
#define IP5306_PIN_SDA 9 /* 默认 GPIO9 = SDA，可通过 platformio.ini build_flags 覆盖 */
#endif
#ifndef IP5306_PIN_SCL
#define IP5306_PIN_SCL 10 /* 默认 GPIO10 = SCL */
#endif

#define IP5306_I2C_CLK_HZ 400000    /* I2C 时钟 400kHz（芯片支持上限） */
#define IP5306_SLAVE_ADDR 0x75      /* 7 位从机地址 */
#define IP5306_SLAVE_ADDR_WR 0xEA   /* 8 位写地址 */
#define IP5306_SLAVE_ADDR_RD 0xEB   /* 8 位读地址 */
#define IP5306_ACK_CHECK_EN true    /* 使能 I2C ACK 校验 */
#define IP5306_ACK_VAL 0            /* ACK 应答值 */
#define IP5306_NACK_VAL 1           /* NACK 应答值 */

/* ------------------------------------------------------------------ */
/* 寄存器地址定义（详见 docs/ip5306-i2c-通讯协议.md）                      */
/* ------------------------------------------------------------------ */

/* 系统控制寄存器（R/W） */
#define IP5306_REG_SYS_CTL0      0x00 /* 升压/充电/负载开机/常开/按键关机 使能 */
#define IP5306_REG_SYS_CTL1      0x01 /* boost 控制方式 / WLED / 低电关机 等 */
#define IP5306_REG_SYS_CTL2      0x02 /* 轻载自动关机时间 */

/* 充电控制寄存器（R/W） */
#define IP5306_REG_CHG_CTL0      0x20 /* 充满截止电压档位 */
#define IP5306_REG_CHG_CTL1      0x21 /* 停充电流 / 充电欠压环 VOUT */
#define IP5306_REG_CHG_CTL2      0x22 /* 电池电压 / 恒压充电加压 */
#define IP5306_REG_CHG_CTL3      0x23 /* 充电恒流环路选择（VIN/BAT 端 CC） */
#define IP5306_REG_CHG_DIG_CTL0  0x24 /* VIN 端充电电流数字设置 */

/* 只读状态寄存器（R） */
#define IP5306_REG_READ0         0x70 /* bit3: 充电使能标志 */
#define IP5306_REG_READ1         0x71 /* bit3: 充满标志 */
#define IP5306_REG_READ2         0x72 /* bit2: 输出轻载标志 */
#define IP5306_REG_READ3         0x77 /* bit2/1/0: 按键双击/长按/短按事件 */

/* ------------------------------------------------------------------ */
/* 关键位定义                                                           */
/* ------------------------------------------------------------------ */

/* SYS_CTL0 (0x00) */
#define IP5306_CTL0_BIT_BOOST_EN      (1 << 5) /* Boost 升压使能 */
#define IP5306_CTL0_BIT_CHARGER_EN    (1 << 4) /* 充电使能 */
#define IP5306_CTL0_BIT_LOAD_AUTO_ON  (1 << 2) /* 插入负载自动开机 */
#define IP5306_CTL0_BIT_BOOST_ALWAYS  (1 << 1) /* BOOST 输出常开 */
#define IP5306_CTL0_BIT_KEY_PWR_OFF   (1 << 0) /* 按键关机使能 */

/* SYS_CTL1 (0x01) */
#define IP5306_CTL1_BIT_BOOST_CTRL_SEL (1 << 7) /* 1:长按关boost 0:双击关boost */
#define IP5306_CTL1_BIT_WLED_CTRL_SEL  (1 << 6) /* WLED 控制方式选择 */
#define IP5306_CTL1_BIT_BOOST_SHORT    (1 << 5) /* 短按开关 boost */
#define IP5306_CTL1_BIT_VIN_OUT_BOOST  (1 << 2) /* VIN 拔出后是否开启 boost */
#define IP5306_CTL1_BIT_BATLOW_OFF     (1 << 0) /* Batlow 3.0V 低电关机使能 */

/* SYS_CTL2 (0x02) bit3:2 轻载自动关机时间 */
#define IP5306_CTL2_LIGHTLOAD_MASK  (0x03 << 2)
#define IP5306_CTL2_LIGHTLOAD_8S    (0x00 << 2)
#define IP5306_CTL2_LIGHTLOAD_16S   (0x01 << 2)
#define IP5306_CTL2_LIGHTLOAD_32S   (0x02 << 2)
#define IP5306_CTL2_LIGHTLOAD_64S   (0x03 << 2)

/* REG_READ0 (0x70) */
#define IP5306_READ0_BIT_CHARGE_EN  (1 << 3) /* 1:充电开启 0:充电关闭 */

/* REG_READ1 (0x71) */
#define IP5306_READ1_BIT_FULL       (1 << 3) /* 1:已经充满 0:还在充电 */

/* REG_READ2 (0x72) */
#define IP5306_READ2_BIT_LIGHTLOAD  (1 << 2) /* 1:轻负载 0:重负载 */

/* REG_READ3 (0x77) 按键事件（读后写 1 清零） */
#define IP5306_READ3_BIT_KEY_DOUBLE (1 << 2) /* 双击 */
#define IP5306_READ3_BIT_KEY_LONG   (1 << 1) /* 长按 */
#define IP5306_READ3_BIT_KEY_SHORT  (1 << 0) /* 短按 */

/* ------------------------------------------------------------------ */
/* 状态结构体                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    bool charging;       /* 充电中 */
    bool charge_full;    /* 已充满 */
    bool light_load;     /* 输出轻载 */
    bool key_short;      /* 发生短按（读后自动清零） */
    bool key_long;       /* 发生长按（读后自动清零） */
    bool key_double;     /* 发生双击（读后自动清零） */
} ip5306_status_t;

/* ------------------------------------------------------------------ */
/* 驱动 API                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化 I2C 总线（I2C_NUM_0，400kHz）并创建 IP5306 设备句柄
 * @return ESP_OK 成功
 */
esp_err_t ip5306_init(void);

/**
 * @brief 读取指定寄存器
 * @param reg  寄存器地址
 * @param val  输出读取到的 8-bit 数据
 * @return ESP_OK 成功
 */
esp_err_t ip5306_read_reg(uint8_t reg, uint8_t *val);

/**
 * @brief 写入指定寄存器（整字节覆盖，调用方需自行保证 Reserved 位不被动）
 * @param reg  寄存器地址
 * @param val  要写入的 8-bit 数据
 * @return ESP_OK 成功
 */
esp_err_t ip5306_write_reg(uint8_t reg, uint8_t val);

/**
 * @brief 读 -> 改 -> 写（推荐写入方式）
 *
 * 先读出寄存器当前值，仅将 mask 覆盖的位更新为 val 中的对应位，
 * 其余位（含 Reserved）保持原值不变。
 *
 * @param reg  寄存器地址
 * @param mask 要修改的位掩码
 * @param val  新值（只取 mask 命中的位）
 * @return ESP_OK 成功
 */
esp_err_t ip5306_read_modify_write(uint8_t reg, uint8_t mask, uint8_t val);

/**
 * @brief 读取电源状态（充电/充满/轻载/按键事件）
 *
 * 按键事件标志读回后会按手册要求"写 1 清零"自动复位。
 *
 * @param st  输出状态结构体
 * @return ESP_OK 成功
 */
esp_err_t ip5306_get_status(ip5306_status_t *st);

/**
 * @brief 扫描 I2C 总线，打印所有在线从机地址
 * @return 发现的设备数量（含 0x75）
 */
int ip5306_scan_bus(void);

#ifdef __cplusplus
}
#endif
