/**
 * @file net_time.c
 * @brief 系统时间同步（SNTP）与系统时钟维护
 *
 * 由 net_app_start() 初始化；在获得 IP 后自动发起 SNTP 同步。
 * 同步成功后：
 *   - 设置时区偏移（CST+8）并校准系统时间
 *   - 发布 app_state 时钟（UI 首页显示 HH:MM）
 *   - 修复系统时间停留在 1970 年导致 mbedtls 证书校验
 *     “证书未生效”从而无法连接 WSS（DashScope STT）的问题
 *
 * 时间来源优先级：
 *   1. RTC 定时器（warm reboot 时保留上次同步的时间，立即发布）
 *   2. SNTP（冷启动获得 IP 后自动同步）
 */

#include "net_time.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../ui/app_state.h"
#include "net_config.h"
#include "net_wifi.h"

#define TAG "net_time"
#define TIME_SYNC_TASK_STACK 4096
#define TIME_SYNC_TASK_PRIO  2

/* 中国标准时间（UTC+8，无夏令时） */
#define TIMEZONE_CST "+8"

/* 时间有效性阈值：2024-01-01T00:00:00Z。早于此视为未同步（1970 冷启动） */
#define TIME_VALID_EPOCH 1704067200

/* 首版使用的 NTP 服务器（阿里云 / 腾讯云） */
static const char *const s_ntp_servers[] = {
    "ntp.aliyun.com",
    "ntp.tencent.com",
    "ntp1.aliyun.com",
};

static bool s_synced;
static bool s_sntp_started; /* SNTP 初始化是否已尝试过 */

static void sntp_task(void *arg);

/* 从系统时钟读取本地时间并发布到 UI */
static void publish_clock(bool synced, const char *source)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    app_state_publish_clock(synced, (uint8_t)tm.tm_hour, (uint8_t)tm.tm_min, source);
}

static void sync_cb(struct timeval *tv)
{
    (void)tv;
    if (s_synced) return;
    s_synced = true;
    publish_clock(true, "sntp");
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    ESP_LOGI(TAG, "system time synced: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

void net_time_on_ip_got(void)
{
    /* sntp_task 仅在 Wi-Fi 就绪后才初始化 SNTP；若启动时网络未就绪而
     * 推迟（s_sntp_started == false），这里在 IP 事件时拉起。
     * SNTP 已初始化后由 lwIP 自动按退避重试，无需手动重启。 */
    if (!s_sntp_started)
    {
        ESP_LOGI(TAG, "IP got, starting SNTP task");
        xTaskCreate(sntp_task, "sntp_sync", TIME_SYNC_TASK_STACK, NULL,
                    TIME_SYNC_TASK_PRIO, NULL);
    }
}

static void sntp_task(void *arg)
{
    (void)arg;

    /* 等待网络就绪（IP 已获取）再启动 SNTP，避免 DNS/网络未通时浪费重试 */
    for (int i = 0; i < 200 && !net_wifi_is_connected(); i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!net_wifi_is_connected())
    {
        ESP_LOGW(TAG, "Wi-Fi not connected, SNTP deferred");
        /* 由 net_time_on_ip_got 在 IP 事件时再次拉起 */
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi ready, starting SNTP (servers: %s, %s, %s)",
             s_ntp_servers[0], s_ntp_servers[1], s_ntp_servers[2]);
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST(s_ntp_servers[0], s_ntp_servers[1], s_ntp_servers[2]));
    cfg.sync_cb = sync_cb;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    s_sntp_started = true;
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    /* 等待首次同步（最多 20s）；超时由 lwIP 自动退避重试，无需处理 */
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000));
    ESP_LOGI(TAG, "SNTP sync wait: %s", err == ESP_OK ? "OK" : esp_err_to_name(err));

    vTaskDelete(NULL);
}

esp_err_t net_time_init(void)
{
    if (s_sntp_started) return ESP_OK;

    /* 设置时区：影响 localtime() 输出（无论 RTC 还是 SNTP 时间） */
    setenv("TZ", TIMEZONE_CST, 1);
    tzset();

    /* warm reboot：RTC 定时器保留上次同步的时间，立即发布，首页时钟无需等待 SNTP */
    if (time(NULL) >= TIME_VALID_EPOCH)
    {
        publish_clock(true, "rtc");
    }

    if (xTaskCreate(sntp_task, "sntp_sync", TIME_SYNC_TASK_STACK, NULL,
                    TIME_SYNC_TASK_PRIO, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
