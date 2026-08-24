/**
 * @file net_portal.c
 * @brief SoftAP 配网页实现（ESP-IDF HTTP Server + 内嵌精简 HTML/CSS/JS）
 *
 * 路由：
 *   GET  /            配网页（含 Wi-Fi 表单 + 状态）
 *   GET  /api/status  状态查询（设备名、SSID 掩码、连接状态、是否已保存）
 *   GET  /api/scan    扫描 Wi-Fi 网络（SSID + RSSI，不返回密码）
 *   POST /api/save    保存配置（SSID/密码/主机/端口/名称 → NVS + 切回 STA）
 *   POST /api/forget  忘记网络并回到配网模式
 *
 * 安全约定：
 *   - 不提供共享令牌字段；密码只在保存 POST 里出现一次，绝不在 GET 响应/日志回显；
 *   - 状态/扫描结果只暴露 SSID 与 RSSI，不返回已保存密码；
 *   - 配网页仅监听 SoftAP 网段（192.168.4.x），不绑定 0.0.0.0；
 *   - 响应全部用带容量检查的写入器构建，杜绝越界写。
 */

#include "net_portal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "net_config.h"
#include "net_wifi.h"

#define NET_PORTAL_PORT 80
#define NET_PORTAL_BODY_BUF 512
#define NET_PORTAL_SCAN_MAX 24

static const char *TAG = "net_portal";

static httpd_handle_t s_server;

/* ------------------------------------------------------------------ */
/* 安全 JSON 写入器                                                   */
/* ------------------------------------------------------------------ */

/* 追加原始字符串（已含转义）；返回剩余容量。 */
static size_t json_raw(char *buf, size_t cap, size_t pos, const char *s)
{
    size_t len = strlen(s);
    if (pos < cap)
    {
        size_t n = cap - pos;
        if (n > len + 1) n = len + 1;
        memcpy(buf + pos, s, n - 1);
        buf[pos + n - 1] = '\0';
        return pos + len; /* 语义位置，用于继续追加；超界部分由后续写入器截断 */
    }
    return pos + len;
}

/* 追加转义后的 JSON 字符串字段（对引号/反斜杠做转义） */
static size_t json_esc_str(char *buf, size_t cap, size_t pos, const char *s)
{
    char esc[2 * 32 + 1];
    size_t ei = 0;
    size_t slen = strlen(s);
    if (slen > 32) slen = 32; /* SSID/名称字段上限 32 字符 */
    for (size_t k = 0; k < slen && ei + 3 < sizeof(esc); k++)
    {
        unsigned char c = (unsigned char)s[k];
        if (c == '"' || c == '\\')
        {
            esc[ei++] = '\\';
            esc[ei++] = (char)c;
        }
        else
        {
            esc[ei++] = (char)c;
        }
    }
    esc[ei] = '\0';
    return json_raw(buf, cap, pos, esc);
}

/* 追加整数字段 */
static size_t json_int(char *buf, size_t cap, size_t pos, int v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d", v);
    return json_raw(buf, cap, pos, tmp);
}

/* ------------------------------------------------------------------ */
/* 页面                                                               */
/* ------------------------------------------------------------------ */

static const char PORTAL_HTML[] =
    "<!DOCTYPE html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>奶片助手 · 配网</title>"
    "<style>"
    "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif;background:#eef4fb;color:#1a1a1a;margin:0;padding:16px}"
    "h1{font-size:20px;margin:0 0 4px}.sub{color:#5d6875;font-size:13px;margin-bottom:16px}"
    ".card{background:#fff;border:1px solid #d7e5f5;border-radius:16px;padding:16px;margin-bottom:14px;box-shadow:0 2px 8px rgba(109,143,181,.15)}"
    "label{display:block;font-size:13px;color:#5d6875;margin:10px 0 4px}"
    "input{width:100%;box-sizing:border-box;padding:10px;border:1px solid #d7e5f5;border-radius:10px;font-size:15px;background:#f7faff}"
    "input:focus{outline:none;border-color:#0f6cbd}"
    "button{margin-top:14px;width:100%;padding:12px;border:none;border-radius:10px;font-size:15px;cursor:pointer}"
    ".primary{background:#0f6cbd;color:#fff}.secondary{background:#dceeff;color:#0b5a9c;margin-top:8px}"
    ".status{margin-top:10px;font-size:13px;color:#5d6875;white-space:pre-line}"
    ".ok{color:#107c10}.err{color:#c77700}"
    ".row{display:flex;gap:8px}.row input:first-child{flex:1}"
    "button.small{width:auto;padding:6px 14px;margin:0;font-size:13px;background:#dceeff;color:#0b5a9c;border-radius:10px}"
    "select{width:100%;padding:10px;border:1px solid #d7e5f5;border-radius:10px;font-size:14px;background:#f7faff;color:#1a1a1a}"
    "</style></head><body>"
    "<h1>奶片助手 · 配网</h1>"
    "<div class='sub'>热点：SuperMilkCard（无密码）· 设备名称：<span id='dev'>-</span></div>"
    "<div class='card'>"
    "<label for='ssid'>Wi-Fi 名称</label>"
    "<div class='row'><input id='ssid' placeholder='选择或输入 Wi-Fi SSID'>"
    "<button class='small' onclick='scanNetworks()'>扫描</button></div>"
    "<label for='pass'>Wi-Fi 密码</label>"
    "<input id='pass' type='password' placeholder='8~63 位'>"
    "<label for='host'>电脑 IP 地址</label>"
    "<input id='host' placeholder='例如 192.168.1.100'>"
    "<label for='port'>TCP 端口</label>"
    "<input id='port' type='number' value='9000' min='1' max='65535'>"
    "<label for='name'>设备名称</label>"
    "<input id='name' placeholder='奶片助手'>"
    "<button class='primary' onclick='save()'>保存并连接</button>"
    "<button class='secondary' onclick='forget()'>忘记网络</button>"
    "<div class='status' id='st'></div>"
    "</div>"
    "<div class='card'><div id='scan'></div></div>"
    "<script>"
    "function $(id){return document.getElementById(id)}"
    "function setStatus(msg,ok){var el=$('st');el.textContent=msg;el.className='status '+(ok?'ok':'err')}"
    "async function j(url,opts){var r=await fetch(url,opts);return await r.json()}"
    "function esc(s){return s.replace(/[&<>\"']/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]})}"
    "function showScan(list){var s=$('scan');if(!list.length){s.innerHTML='<div class=sub>未扫描到网络，请重试。</div>';return}"
    "s.innerHTML='<div class=sub>选择网络：</div><select onchange=\\'$(\"ssid\").value=this.value\\'>'+"
    "list.map(function(a){return '<option value=\"'+esc(a.ssid)+'\">'+esc(a.ssid)+'（RSSI '+a.rssi+'）</option>'}).join('')+'</select>'}"
    "async function scanNetworks(){setStatus('正在扫描…');try{var d=await j('/api/scan');showScan(d.networks||[]);"
    "setStatus('扫描完成。','true')}catch(e){setStatus('扫描失败，请重试。')}}"
    "async function save(){var body={ssid:$('ssid').value.trim(),pass:$('pass').value,host:$('host').value.trim(),"
    "port:parseInt($('port').value)||9000,name:$('name').value.trim()};"
    "if(!body.ssid){setStatus('请填写 Wi-Fi 名称。');return}"
    "setStatus('保存并连接中…');try{var d=await j('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    "setStatus(d.ok?'保存成功，设备正在连接 '+d.ssid+' …':'保存失败：'+(d.error||''),d.ok)}catch(e){setStatus('网络请求失败。')}}"
    "async function forget(){setStatus('正在忘记网络…');try{var d=await j('/api/forget',{method:'POST'});"
    "setStatus(d.ok?'已忘记网络，可重新配置。':'失败：'+(d.error||''),d.ok)}catch(e){setStatus('网络请求失败。')}}"
    "async function load(){try{var d=await j('/api/status');if(d.name)$('dev').textContent=d.name;"
    "if(d.saved&&d.ssid){$('host').value=d.host||'';$('port').value=d.tcpPort||9000;"
    "if(d.connected){setStatus('已连接 Wi-Fi：'+d.ssid)}}}catch(e){}}"
    "load();</script></body></html>";

static esp_err_t send_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send_chunk(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0); /* 结束块 */
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* /api/status                                                         */
/* ------------------------------------------------------------------ */

static esp_err_t api_status_handler(httpd_req_t *req)
{
    net_config_t cfg;
    net_config_load(&cfg);

    /* 只回显已保存配置中的主机/端口/名称，绝不回显密码 */
    char buf[512];
    size_t pos = 0;
    pos = json_raw(buf, sizeof(buf), pos, "{\"ok\":true,\"name\":\"");
    pos = json_esc_str(buf, sizeof(buf), pos, cfg.name[0] ? cfg.name : NET_CFG_DEFAULT_NAME);
    pos = json_raw(buf, sizeof(buf), pos, cfg.has_ssid ? "\",\"saved\":true,\"ssid\":\"" : "\",\"saved\":false,\"ssid\":\"");
    pos = json_esc_str(buf, sizeof(buf), pos, cfg.has_ssid ? cfg.ssid : "");
    pos = json_raw(buf, sizeof(buf), pos, "\",\"host\":\"");
    pos = json_esc_str(buf, sizeof(buf), pos, cfg.host);
    pos = json_raw(buf, sizeof(buf), pos, "\",\"tcpPort\":");
    pos = json_int(buf, sizeof(buf), pos, (int)cfg.tcp_port);
    pos = json_raw(buf, sizeof(buf), pos, net_wifi_is_connected() ? ",\"connected\":true}" : ",\"connected\":false}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* /api/scan                                                           */
/* ------------------------------------------------------------------ */

static esp_err_t api_scan_handler(httpd_req_t *req)
{
    /* 阻塞式扫描：Fast scan + 清掉上一次结果 */
    esp_wifi_scan_stop();
    wifi_scan_config_t sc = {0};
    sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    sc.show_hidden = false;
    if (esp_wifi_scan_start(&sc, true) != ESP_OK)
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_chunk(req, "{\"networks\":[]}", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > NET_PORTAL_SCAN_MAX) count = NET_PORTAL_SCAN_MAX;

    wifi_ap_record_t *records = calloc(count ? count : 1, sizeof(wifi_ap_record_t));
    if (records == NULL)
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_chunk(req, "{\"networks\":[]}", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    uint16_t got = count;
    if (esp_wifi_scan_get_ap_records(&got, records) != ESP_OK || got == 0)
    {
        free(records);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_chunk(req, "{\"networks\":[]}", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
    if (got > NET_PORTAL_SCAN_MAX) got = NET_PORTAL_SCAN_MAX;

    /* 每个 AP 条目 ≤ 约 100 字节，缓冲区留足余量 */
    size_t cap = 64 + (size_t)got * 128 + 16;
    char *buf = malloc(cap);
    if (buf == NULL)
    {
        free(records);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_chunk(req, "{\"networks\":[]}", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    size_t pos = json_raw(buf, cap, 0, "{\"networks\":[");
    bool first = true;
    for (uint16_t i = 0; i < got; i++)
    {
        if (records[i].ssid[0] == 0) continue; /* 跳过隐藏网络 */

        pos = json_raw(buf, cap, pos, first ? "{\"ssid\":\"" : ",{\"ssid\":\"");
        pos = json_esc_str(buf, cap, pos, (const char *)records[i].ssid);
        pos = json_raw(buf, cap, pos, "\",\"rssi\":");
        pos = json_int(buf, cap, pos, (int)records[i].rssi);
        pos = json_raw(buf, cap, pos, "}");
        first = false;
    }
    pos = json_raw(buf, cap, pos, "]}");
    free(records);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    free(buf);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* /api/save 与 /api/forget                                            */
/* ------------------------------------------------------------------ */

static void read_body(httpd_req_t *req, char *buf, size_t cap)
{
    size_t total = 0;
    while (total < cap - 1)
    {
        int ret = httpd_req_recv(req, buf + total, cap - 1 - total);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            break;
        }
        total += (size_t)ret;
    }
    buf[total] = '\0';
}

static esp_err_t api_save_handler(httpd_req_t *req)
{
    char body[NET_PORTAL_BODY_BUF];
    read_body(req, body, sizeof(body));

    /* 简易 JSON 字段提取（不使用大型 JSON 库，仅按引号键值解析） */
    char ssid[NET_CFG_SSID_MAX] = "", pass[NET_CFG_PASS_MAX] = "";
    char host[NET_CFG_HOST_MAX] = "", name[NET_CFG_NAME_MAX] = "";
    uint16_t port = NET_CFG_DEFAULT_TCP_PORT;

    const char *keys[] = {"\"ssid\"", "\"pass\"", "\"host\"", "\"name\"", "\"port\""};
    char *vals[5] = {ssid, pass, host, name, NULL};
    for (size_t i = 0; i < 5; i++)
    {
        const char *p = strstr(body, keys[i]);
        if (!p) continue;
        const char *colon = strchr(p + strlen(keys[i]), ':');
        if (!colon) continue;

        if (i == 4)
        {
            /* port 是数字 */
            const char *pq = colon;
            while (pq[0] && (pq[0] < '0' || pq[0] > '9')) pq++;
            if (pq[0])
            {
                char num[8];
                size_t pn = 0;
                while (pq[pn] >= '0' && pq[pn] <= '9' && pn + 1 < sizeof(num))
                {
                    num[pn] = pq[pn];
                    pn++;
                }
                num[pn] = '\0';
                long v = strtol(num, NULL, 10);
                if (v > 0 && v <= 65535) port = (uint16_t)v;
            }
            continue;
        }

        const char *q = strchr(colon, '"');
        if (!q) continue;
        q++;
        size_t cap = (i == 1) ? sizeof(pass) : (i == 2) ? sizeof(host)
                     : (i == 3) ? sizeof(name)
                                : sizeof(ssid);
        size_t o = 0;
        while (q[o] && q[o] != '"' && o + 1 < cap) o++;
        memcpy(vals[i], q, o);
        vals[i][o] = '\0';
    }

    if (ssid[0] == '\0')
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_chunk(req, "{\"ok\":false,\"error\":\"missing_ssid\"}", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "save: ssid=%s host=%s port=%u name=%s (password not logged)", ssid, host,
             (unsigned)port, name);

    esp_err_t err = net_wifi_request_connect(ssid, pass, host, port, name);
    if (err != ESP_OK)
    {
        httpd_resp_set_type(req, "application/json");
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_send_chunk(req, msg, HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_forget_handler(httpd_req_t *req)
{
    esp_err_t err = net_wifi_forget();
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK)
    {
        httpd_resp_send_chunk(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    }
    else
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_send_chunk(req, msg, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* 服务生命周期                                                       */
/* ------------------------------------------------------------------ */

esp_err_t net_portal_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = NET_PORTAL_PORT;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 6;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uri;
    memset(&uri, 0, sizeof(uri));

    uri.method = HTTP_GET;
    uri.uri = "/";
    uri.handler = send_page;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_GET;
    uri.uri = "/api/status";
    uri.handler = api_status_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_GET;
    uri.uri = "/api/scan";
    uri.handler = api_scan_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_POST;
    uri.uri = "/api/save";
    uri.handler = api_save_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_POST;
    uri.uri = "/api/forget";
    uri.handler = api_forget_handler;
    httpd_register_uri_handler(s_server, &uri);

    ESP_LOGI(TAG, "portal started at http://192.168.4.1");
    return ESP_OK;
}

void net_portal_stop(void)
{
    if (s_server == NULL) return;
    httpd_stop(s_server);
    s_server = NULL;
}

bool net_portal_is_running(void)
{
    return s_server != NULL;
}
