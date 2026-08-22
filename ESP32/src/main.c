/**
 * @file main.c
 * @brief IP5306-I2C 通讯验证主程序
 *
 * 流程：
 *  1. 初始化 I2C 总线并扫描从机地址（确认 0x75 在线）
 *  2. 读取电源状态（充电中/已充满/轻载/按键事件）
 *  3. 读取系统控制与充电配置寄存器，打印出厂值
 *  4. 循环定时刷新状态（观察按键事件）
 *
 * 安全策略：本程序默认【只读】，不做任何寄存器写入，避免误动电源行为。······················
 * 如需写入，请使用 ip5306_read_modify_write()（见 ip5306.h），并按
 * "读 -> 改 -> 写"规范操作。下方附有注释掉的示例。
 */

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ip5306.h"

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

#if 0 /* 写入示例（默认关闭）：按需启用，使用前确认对电源行为的影响 */
    /* 例：使能按键关机功能（SYS_CTL0 bit0），保留其他位不变 */
    ip5306_read_modify_write(IP5306_REG_SYS_CTL0,
                             IP5306_CTL0_BIT_KEY_PWR_OFF,
                             IP5306_CTL0_BIT_KEY_PWR_OFF);
    /* 例：设置轻载自动关机时间为 16S（SYS_CTL2 bit3:2） */
    ip5306_read_modify_write(IP5306_REG_SYS_CTL2,
                             IP5306_CTL2_LIGHTLOAD_MASK,
                             IP5306_CTL2_LIGHTLOAD_16S);
#endif

    /* 4. 循环刷新状态 */
    while (1)
    {
        print_status();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
