/**
 * @file net_tcp.c
 * @brief TCP 业务服务端 + UDP 发现广播实现
 *
 * 帧协议（AA55 大端，见 ESP32/接口文档.md）：
 *   [0]=0xAA [1]=0x55 [2]=type [3..6]=len(u32 BE) [7..]=payload
 *   type: 0x01 HELLO / 0x02 TEXT / 0x03 AUDIO
 *
 * 关键点：
 *   - 只允许一个已握手客户端；新连接到来且已有客户端 → 立即关闭新连接
 *   - HELLO 帧只含 id/name（无密码/令牌）
 *   - TEXT JSON 字段做白名单/长度校验：expression/motion 未知回退 neutral/idle，
 *     messagePreview 截断至 96 UTF-8 字节；avatarId 首期忽略
 *   - 所有 UI 相关状态经 app_state 发布（UI 不直接触碰网络）
 *   - 锁保护跨任务发送
 */

#include "net_tcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "../ui/app_state.h"
#include "net_config.h"
#include "net_wifi.h"

#define NET_TCP_PORT_KEY "tcp_port"
#define NET_TCP_RECV_TIMEOUT_MS 1000
#define NET_TCP_RECV_BUF 2048
#define NET_TCP_MSG_PREVIEW_MAX 96
#define NET_TCP_BROADCAST_INTERVAL_MS 2000
#define NET_TCP_UDP_PORT 4210

static const char *TAG = "net_tcp";

/* 帧头常量（与接口文档一致） */
#define FRAME_MAGIC0 0xAA
#define FRAME_MAGIC1 0x55
#define FRAME_TYPE_HELLO 0x01
#define FRAME_TYPE_TEXT 0x02
#define FRAME_TYPE_AUDIO 0x03
#define FRAME_HEADER_LEN 7
#define FRAME_MAX_PAYLOAD (4 * 1024 * 1024)

static int s_listen_fd = -1;
static int s_udp_fd = -1;
static int s_client_fd = -1;
static bool s_task_started;

/* 跨任务发送锁 */
static SemaphoreHandle_t s_send_lock;

/* ------------------------------------------------------------------ */
/* 帧编解码                                                           */
/* ------------------------------------------------------------------ */

static void frame_header(uint8_t *hdr, uint8_t type, uint32_t len)
{
    hdr[0] = FRAME_MAGIC0;
    hdr[1] = FRAME_MAGIC1;
    hdr[2] = type;
    hdr[3] = (uint8_t)((len >> 24) & 0xFF);
    hdr[4] = (uint8_t)((len >> 16) & 0xFF);
    hdr[5] = (uint8_t)((len >> 8) & 0xFF);
    hdr[6] = (uint8_t)(len & 0xFF);
}

static esp_err_t send_frame(int fd, uint8_t type, const uint8_t *payload, uint32_t len)
{
    uint8_t hdr[FRAME_HEADER_LEN];
    frame_header(hdr, type, len);

    int sent = send(fd, hdr, sizeof(hdr), 0);
    if (sent != (int)sizeof(hdr)) return ESP_FAIL;
    if (len > 0 && payload)
    {
        sent = send(fd, payload, len, 0);
        if (sent != (int)len) return ESP_FAIL;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 业务：TEXT JSON 解析                                               */
/* ------------------------------------------------------------------ */

/* 提取 JSON 字符串字段值（找到 key 后的 "value"；不引入大型 JSON 库）。
 * 返回是否命中；命中时写入 out（截断到 out_size，含 '\0'）。 */
static bool json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0) return false;

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    const char *colon = strchr(p + strlen(needle), ':');
    if (!colon) return false;
    const char *q = strchr(colon, '"');
    if (!q) return false;
    q++;
    size_t o = 0;
    while (q[o] && q[o] != '"' && o + 1 < out_size) o++;
    memcpy(out, q, o);
    out[o] = '\0';
    return true;
}

/* 处理 live2d_state：白名单 + 长度校验后发布到 app_state */
static void handle_live2d_state(const char *json)
{
    char expression[32] = "neutral";
    char motion[32] = "idle";
    char preview[NET_TCP_MSG_PREVIEW_MAX + 1] = "";

    json_get_string(json, "expression", expression, sizeof(expression));
    json_get_string(json, "motion", motion, sizeof(motion));
    json_get_string(json, "messagePreview", preview, sizeof(preview));

    /* 未知情绪/动作安全回退 */
    app_expr_t expr = app_expr_from_str(expression);
    app_motion_t mot = app_motion_from_str(motion);

    ESP_LOGI(TAG, "live2d_state: expr=%s motion=%s preview=\"%s\"",
             app_expr_to_str(expr), app_motion_to_str(mot), preview);

    app_state_publish_live2d(true, expr, mot, preview);
}

/* 处理 chat 文本：作为摘要保留在 live2d 快照（供互动页展示） */
static void handle_chat(const char *json, const char *role, const char *content)
{
    (void)json;
    (void)role;
    if (content && content[0]) app_state_publish_live2d_message(content);
}

static void handle_text_payload(const char *json, size_t len)
{
    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, json, len);
    buf[len] = '\0';

    char type[32] = "";
    json_get_string(buf, "type", type, sizeof(type));

    if (strcmp(type, "live2d_state") == 0)
    {
        handle_live2d_state(buf);
    }
    else if (strcmp(type, "chat") == 0)
    {
        char role[16] = "";
        char content[NET_TCP_MSG_PREVIEW_MAX + 1] = "";
        json_get_string(buf, "role", role, sizeof(role));
        json_get_string(buf, "content", content, sizeof(content));
        handle_chat(buf, role, content);
    }
    /* 其他类型（audio_start/end、text 等）首期忽略 */

    free(buf);
}

/* ------------------------------------------------------------------ */
/* 客户端连接生命周期                                                 */
/* ------------------------------------------------------------------ */

static void on_client_connected(int fd)
{
    /* 更新 win 连接状态 */
    app_state_publish_live2d_conn(true);

    /* 立即发送 HELLO 帧 */
    net_config_t cfg;
    net_config_load(&cfg);
    char hello[256];
    snprintf(hello, sizeof(hello), "{\"id\":\"%s\",\"name\":\"%s\"}",
             net_config_device_id(), cfg.name[0] ? cfg.name : NET_CFG_DEFAULT_NAME);
    send_frame(fd, FRAME_TYPE_HELLO, (const uint8_t *)hello, (uint32_t)strlen(hello));
    ESP_LOGI(TAG, "HELLO sent: %s", hello);
}

static void on_client_closed(void)
{
    if (s_client_fd >= 0)
    {
        close(s_client_fd);
        s_client_fd = -1;
    }
    app_state_publish_live2d_conn(false);
    ESP_LOGI(TAG, "client disconnected");
}

/* 处理单个客户端的完整会话（阻塞，直到断开） */
static void client_session(int fd)
{
    on_client_connected(fd);

    uint8_t *buf = malloc(NET_TCP_RECV_BUF);
    if (!buf)
    {
        on_client_closed();
        return;
    }

    /* 应用层简单累积缓冲：仅支持逐帧（每帧 ≤ recv 缓冲），半包由 Windows 端处理 */
    uint32_t want = FRAME_HEADER_LEN; /* 期望接收字节数 */
    uint8_t hdr[FRAME_HEADER_LEN];
    uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    uint32_t got = 0;

    while (s_client_fd >= 0)
    {
        int r = recv(fd, buf, NET_TCP_RECV_BUF, 0);
        if (r <= 0)
        {
            if (r < 0 && errno == EAGAIN) continue; /* 超时：继续轮询 */
            break; /* 对端关闭或出错 */
        }

        const uint8_t *p = buf;
        int remaining = r;
        while (remaining > 0 && s_client_fd >= 0)
        {
            if (want == FRAME_HEADER_LEN)
            {
                /* 累积帧头 */
                int need = (int)(FRAME_HEADER_LEN - got);
                int take = remaining < need ? remaining : need;
                memcpy(hdr + got, p, (size_t)take);
                got += (uint32_t)take;
                p += take;
                remaining -= take;
                if (got == FRAME_HEADER_LEN)
                {
                    /* 校验魔数 */
                    if (hdr[0] != FRAME_MAGIC0 || hdr[1] != FRAME_MAGIC1)
                    {
                        /* 重同步：去掉魔数错位字节，重新累积帧头 */
                        memmove(hdr, hdr + 1, FRAME_HEADER_LEN - 1);
                        got = FRAME_HEADER_LEN - 1;
                        continue;
                    }
                    payload_len = ((uint32_t)hdr[3] << 24) | ((uint32_t)hdr[4] << 16) |
                                  ((uint32_t)hdr[5] << 8) | (uint32_t)hdr[6];
                    if (payload_len > FRAME_MAX_PAYLOAD)
                    {
                        /* 非法长度：丢弃并重新同步 */
                        got = 0;
                        want = FRAME_HEADER_LEN;
                        continue;
                    }
                    if (payload_len == 0)
                    {
                        handle_text_payload((const char *)hdr + 3, 0); /* 空负载帧忽略 */
                        want = FRAME_HEADER_LEN;
                        got = 0;
                        continue;
                    }
                    payload = malloc(payload_len);
                    if (!payload)
                    {
                        want = FRAME_HEADER_LEN;
                        got = 0;
                        continue;
                    }
                    want = FRAME_HEADER_LEN + payload_len;
                }
            }
            else
            {
                /* 累积负载 */
                uint32_t need = want - got;
                int take = remaining < (int)need ? remaining : (int)need;
                memcpy(payload + (got - FRAME_HEADER_LEN), p, (size_t)take);
                got += (uint32_t)take;
                p += take;
                remaining -= take;
                if (got == want)
                {
                    uint8_t type = hdr[2];
                    if (type == FRAME_TYPE_TEXT && payload_len > 0)
                    {
                        handle_text_payload((const char *)payload, payload_len);
                    }
                    else if (type == FRAME_TYPE_AUDIO)
                    {
                        /* 首期无音频解码；忽略 */
                    }
                    free(payload);
                    payload = NULL;
                    want = FRAME_HEADER_LEN;
                    got = 0;
                }
            }
        }
    }

    free(payload);
    free(buf);
    on_client_closed();
}

/* ------------------------------------------------------------------ */
/* TCP 服务任务                                                       */
/* ------------------------------------------------------------------ */

static void tcp_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "tcp task started");

    /* 读端口配置（可能被配网页更新） */
    net_config_t cfg;
    net_config_load(&cfg);

    while (1)
    {
        if (s_listen_fd < 0)
        {
            int lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (lfd < 0)
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            int one = 1;
            setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

            struct sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(cfg.tcp_port);
            addr.sin_addr.s_addr = INADDR_ANY;
            if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
            {
                ESP_LOGE(TAG, "bind :%u failed", (unsigned)cfg.tcp_port);
                close(lfd);
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            if (listen(lfd, 2) != 0)
            {
                close(lfd);
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
            s_listen_fd = lfd;
            ESP_LOGI(TAG, "listening on :%u", (unsigned)cfg.tcp_port);
        }

        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int fd = accept(s_listen_fd, (struct sockaddr *)&from, &from_len);
        if (fd < 0)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        ESP_LOGI(TAG, "incoming connection from %s:%u", inet_ntoa(from.sin_addr),
                 ntohs(from.sin_port));

        /* 只允许一个客户端 */
        if (s_client_fd >= 0)
        {
            ESP_LOGW(TAG, "client already connected, rejecting");
            close(fd);
            continue;
        }
        s_client_fd = fd;

        struct timeval tv = {0};
        tv.tv_sec = 0;
        tv.tv_usec = NET_TCP_RECV_TIMEOUT_MS * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        client_session(fd);
        s_client_fd = -1;
    }
}

/* ------------------------------------------------------------------ */
/* UDP 发现广播                                                       */
/* ------------------------------------------------------------------ */

static void broadcast_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "udp broadcast task started");

    s_udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp_fd < 0)
    {
        ESP_LOGE(TAG, "udp socket failed");
        vTaskDelete(NULL);
        return;
    }
    int one = 1;
    setsockopt(s_udp_fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

    while (1)
    {
        if (net_wifi_is_connected() || net_wifi_is_provisioning())
        {
            net_config_t cfg;
            net_config_load(&cfg);
            char json[256];
            snprintf(json, sizeof(json),
                     "{\"id\":\"%s\",\"name\":\"%s\",\"tcpPort\":%u,\"wsPort\":9001}",
                     net_config_device_id(),
                     cfg.name[0] ? cfg.name : NET_CFG_DEFAULT_NAME,
                     (unsigned)cfg.tcp_port);

            struct sockaddr_in to = {0};
            to.sin_family = AF_INET;
            to.sin_port = htons(NET_TCP_UDP_PORT);
            to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sendto(s_udp_fd, json, (size_t)strlen(json), 0,
                   (struct sockaddr *)&to, sizeof(to));
        }
        vTaskDelay(pdMS_TO_TICKS(NET_TCP_BROADCAST_INTERVAL_MS));
    }
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                           */
/* ------------------------------------------------------------------ */

esp_err_t net_tcp_start(void)
{
    if (s_task_started) return ESP_OK;

    s_send_lock = xSemaphoreCreateMutex();
    if (!s_send_lock) return ESP_ERR_NO_MEM;

    if (xTaskCreate(tcp_task, "tcp_server", 8192, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(broadcast_task, "udp_bcast", 4096, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    s_task_started = true;
    return ESP_OK;
}

bool net_tcp_is_client_connected(void)
{
    return s_client_fd >= 0;
}

esp_err_t net_tcp_send_json(const char *json)
{
    if (json == NULL) return ESP_ERR_INVALID_ARG;
    int fd = s_client_fd;
    if (fd < 0) return ESP_ERR_NOT_FOUND;
    if (xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t err = send_frame(fd, FRAME_TYPE_TEXT, (const uint8_t *)json,
                               (uint32_t)strlen(json));
    xSemaphoreGive(s_send_lock);
    return err;
}

esp_err_t net_tcp_send_live2d_command(const char *command)
{
    char json[96];
    snprintf(json, sizeof(json), "{\"type\":\"live2d_command\",\"command\":\"%s\"}",
             command ? command : "");
    return net_tcp_send_json(json);
}

esp_err_t net_tcp_send_audio(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    int fd = s_client_fd;
    if (fd < 0) return ESP_ERR_NOT_FOUND;
    if (xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t err = send_frame(fd, FRAME_TYPE_AUDIO, data, len);
    xSemaphoreGive(s_send_lock);
    return err;
}
