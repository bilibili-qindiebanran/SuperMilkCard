/**
 * @file net_portal.c
 * @brief 常驻配置网页实现（ESP-IDF HTTP Server + 内嵌精简 HTML/CSS/JS）
 *
 * 生命周期：设备启动即由 net_app_start() 常驻启动（幂等），SoftAP 配网
 * 与 STA 局域网共用同一 server，分别通过 http://192.168.4.1/ 与
 * http://<STA-IP>/ 访问。
 *
 * 路由：
 *   GET  /                 配置网页（Wi-Fi 卡 + 大模型 LLM 卡）
 *   GET  /api/status       状态查询（设备名、SSID 掩码、连接状态、STA IP、LLM 掩码状态）
 *   GET  /api/scan         扫描 Wi-Fi 网络（SSID + RSSI，不返回密码）
 *   POST /api/save         兼容旧接口：一次保存 Wi-Fi/STT（保留）
 *   POST /api/wifi         保存 Wi-Fi/Windows 主机配置（独立）
 *   POST /api/stt          保存 ESP32 直连 STT 配置（独立，不影响 Wi-Fi/LLM）
 *   POST /api/llm          保存 LLM 配置（Key 空=保持不变）
 *   POST /api/llm/key      设置/更新 LLM Key
 *   POST /api/llm/clear-key 清除 LLM Key
 *   POST /api/forget       忘记网络并回到配网模式
 *
 * 安全约定：
 *   - 不提供共享令牌字段；Wi-Fi 密码/STT Key/LLM Key 只在对应 POST 请求里出现，
 *     绝不在 GET 响应/日志回显；
 *   - 状态接口只返回掩码与布尔状态（hasApiKey 等）；
 *   - 页面仅监听局域网接口，不绑定 0.0.0.0；响应全部用带容量检查的写入器构建。
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
#define NET_PORTAL_BODY_BUF 4096  /* LLM 卡片含多行角色提示词，需更大请求体 */
#define NET_PORTAL_SCAN_MAX 24
#define NET_PORTAL_ROLE_PROMPT_MAX (NET_CFG_ROLE_PROMPT_MAX - 1)

static const char *TAG = "net_portal";

static httpd_handle_t s_server;

/* ------------------------------------------------------------------ */
/* 简易 JSON 字段提取（不引入大型 JSON 库）                            */
/* ------------------------------------------------------------------ */

/* 提取 JSON 字符串字段（key 后的 "value"），命中返回 true 并写入 out */
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

/* 提取 JSON 数值字段，返回是否命中；命中时写入 out */
static bool json_get_number(const char *json, const char *key, double *out)
{
    if (!json || !key || !out) return false;
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    const char *colon = strchr(p + strlen(needle), ':');
    if (!colon) return false;
    const char *q = colon;
    while (q[0] && (q[0] == ' ' || q[0] == '\t')) q++;
    if (q[0] < '0' || q[0] > '9') return false;
    *out = strtod(q, NULL);
    return true;
}

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

/* 追加转义后的 JSON 字符串字段（对引号/反斜杠做转义），最多 max_chars 字符。
 * 直接写入输出缓冲（带容量检查），支持长字段（Base URL/角色提示词等）。 */
static size_t json_esc_str_n(char *buf, size_t cap, size_t pos, const char *s, size_t max_chars)
{
    size_t slen = strlen(s);
    if (slen > max_chars) slen = max_chars;
    for (size_t k = 0; k < slen; k++)
    {
        unsigned char c = (unsigned char)s[k];
        if (c == '"' || c == '\\')
        {
            if (pos + 2 < cap) buf[pos] = '\\';
            pos++;
        }
        if (pos < cap) buf[pos] = (char)c;
        pos++;
    }
    if (pos < cap) buf[pos] = '\0';
    else buf[cap - 1] = '\0';
    return pos;
}

/* 短字段（SSID/名称/主机名，≤32 字符） */
static size_t json_esc_str(char *buf, size_t cap, size_t pos, const char *s)
{
    return json_esc_str_n(buf, cap, pos, s, 32);
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
    "<title>奶片助手 · 配置</title>"
    "<style>"
    "body{font-family:system-ui,-apple-system,'Segoe UI',sans-serif;background:#eef4fb;color:#1a1a1a;margin:0;padding:16px}"
    "h1{font-size:20px;margin:0 0 4px}.sub{color:#5d6875;font-size:13px;margin-bottom:16px}"
    ".card{background:#fff;border:1px solid #d7e5f5;border-radius:16px;padding:16px;margin-bottom:14px;box-shadow:0 2px 8px rgba(109,143,181,.15)}"
    ".llmcard{background:linear-gradient(160deg,#f4efff 0%,#ece5ff 100%);border-color:#d8ccf7}"
    ".llmcard label{color:#5a4a9c}"
    "label{display:block;font-size:13px;color:#5d6875;margin:10px 0 4px}"
    "input,textarea{width:100%;box-sizing:border-box;padding:10px;border:1px solid #d7e5f5;border-radius:10px;font-size:15px;background:#f7faff}"
    "textarea{min-height:88px;resize:vertical;font-family:inherit}"
    "input:focus,textarea:focus{outline:none;border-color:#0f6cbd}"
    "button{margin-top:14px;width:100%;padding:12px;border:none;border-radius:10px;font-size:15px;cursor:pointer}"
    ".primary{background:#0f6cbd;color:#fff}.secondary{background:#dceeff;color:#0b5a9c;margin-top:8px}"
    ".purple{background:#7a5cd6;color:#fff}"
    ".status{margin-top:10px;font-size:13px;color:#5d6875;white-space:pre-line}"
    ".ok{color:#107c10}.err{color:#c77700}"
    ".row{display:flex;gap:8px}.row input:first-child{flex:1}"
    "button.small{width:auto;padding:6px 14px;margin:0;font-size:13px;background:#dceeff;color:#0b5a9c;border-radius:10px}"
    "button.smallpurple{width:auto;padding:6px 14px;margin:0;font-size:13px;background:#e4d9ff;color:#5a4a9c;border-radius:10px}"
    ".numrow{display:flex;align-items:center;gap:8px}"
    ".numrow input{flex:1;min-width:0}"
    ".numrow button{width:36px;flex:0 0 36px;margin:0;padding:8px 0}"
    ".keyrow{display:flex;gap:8px;align-items:center}"
    ".keyrow button{flex:0 0 auto;width:auto;margin:0;padding:8px 14px}"
    ".keyrow span{flex:0 0 auto;font-size:13px;font-weight:600}"
    "select{width:100%;padding:10px;border:1px solid #d7e5f5;border-radius:10px;font-size:14px;background:#f7faff;color:#1a1a1a}"
    ".tag{display:inline-block;background:#107c10;color:#fff;border-radius:8px;padding:2px 8px;font-size:12px}"
    "</style></head><body>"
    "<h1>奶片助手 · 配置</h1>"
    "<div class='sub'>热点：SuperMilkCard（无密码）· 设备名称：<span id='dev'>-</span>"
    " · 局域网：<span id='ip'>-</span> · 配置页仅限可信局域网</div>"
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
    "<button class='primary' onclick='saveWifi()'>保存并连接</button>"
    "<div class='status' id='st'></div>"
    "</div>"
    "<div class='card'>"
    "<label for='sttUrl'>STT WebSocket 地址</label>"
    "<input id='sttUrl' value='wss://dashscope.aliyuncs.com/api-ws/v1/inference'>"
    "<label for='sttKey'>DashScope API Key（仅保存到设备，不回显）</label>"
    "<input id='sttKey' type='password' placeholder='sk-...'>"
    "<label for='sttModel'>STT 模型</label>"
    "<input id='sttModel' value='qwen-audio-3.0-asr-flash-streaming'>"
    "<button class='primary' onclick='saveStt()'>保存 STT 设置</button>"
    "<div class='status' id='st2'></div>"
    "</div>"
    "<div class='card llmcard' id='llmCard'>"
    "<h2 style='margin:0 0 4px;font-size:17px;color:#5a4a9c'>大模型（LLM）</h2>"
    "<div class='sub' style='margin-bottom:6px;color:#7a6ab0'>独立角色对话使用；仅保存到设备，不回显 Key</div>"
    "<label for='llmUrl'>Base URL</label>"
    "<input id='llmUrl' placeholder='https://api.deepseek.com'>"
    "<label>API Key</label>"
    "<div class='keyrow'><span id='llmKeyStatus'>未配置</span>"
    "<button class='smallpurple' onclick='setKey()'>设置 API Key</button>"
    "<button class='smallpurple' onclick='clearKey()'>清除</button></div>"
    "<label for='llmModel'>模型</label>"
    "<input id='llmModel' placeholder='deepseek-chat'>"
    "<label>Temperature</label>"
    "<div class='numrow'><input id='llmTemp' type='number' step='0.1' min='0' max='2' value='0.8'>"
    "<button class='smallpurple' onclick='step(\"llmTemp\",-0.1,0,2)'>-</button>"
    "<button class='smallpurple' onclick='step(\"llmTemp\",0.1,0,2)'>+</button></div>"
    "<label>最大回复 Token</label>"
    "<div class='numrow'><input id='llmMax' type='number' step='64' min='64' max='2048' value='1024'>"
    "<button class='smallpurple' onclick='step(\"llmMax\",-64,64,2048)'>-</button>"
    "<button class='smallpurple' onclick='step(\"llmMax\",64,64,2048)'>+</button></div>"
    "<label>上下文上限（估算 Token）</label>"
    "<div class='numrow'><input id='llmCtx' type='number' step='256' min='512' max='12000' value='8000'>"
    "<button class='smallpurple' onclick='step(\"llmCtx\",-256,512,12000)'>-</button>"
    "<button class='smallpurple' onclick='step(\"llmCtx\",256,512,12000)'>+</button></div>"
    "<label for='rolePrompt'>角色提示词</label>"
    "<textarea id='rolePrompt' placeholder='你是奶片助手…'></textarea>"
    "<button class='purple' onclick='saveLlm()'>保存 LLM 与角色设置</button>"
    "<div class='status' id='st3'></div>"
    "</div>"
    "<div class='card'><div id='scan'></div></div>"
    "<script>"
    "function $(id){return document.getElementById(id)}"
    "function setStatus(id,msg,ok){var el=$(id);el.textContent=msg;el.className='status '+(ok?'ok':'err')}"
    "async function j(url,opts){var r=await fetch(url,opts);var t=await r.text();try{return JSON.parse(t)}catch(e){return {ok:false,error:'bad_json'}}}"
    "function esc(s){return s.replace(/[&<>\"']/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]})}"
    "function num(id){var v=parseFloat($(id).value);return isNaN(v)?null:v}"
    "function step(id,d,min,max){var el=$(id);var v=parseFloat(el.value);if(isNaN(v))v=min;v=Math.min(max,Math.max(min,v+d));el.value=Math.round(v*100)/100}"
    "function showScan(list){var s=$('scan');if(!list.length){s.innerHTML='<div class=sub>未扫描到网络，请重试。</div>';return}"
    "s.innerHTML='<div class=sub>选择网络：</div><select onchange=\\'$(\"ssid\").value=this.value\\'>'+"
    "list.map(function(a){return '<option value=\"'+esc(a.ssid)+'\">'+esc(a.ssid)+'（RSSI '+a.rssi+'）</option>'}).join('')+'</select>'}"
    "async function scanNetworks(){setStatus('st','正在扫描…');try{var d=await j('/api/scan');showScan(d.networks||[]);"
    "setStatus('st','扫描完成。','true')}catch(e){setStatus('st','扫描失败，请重试。')}}"
    "async function saveWifi(){var body={ssid:$('ssid').value.trim(),pass:$('pass').value,host:$('host').value.trim(),"
    "port:parseInt($('port').value)||9000,name:$('name').value.trim()};"
    "if(!body.ssid){setStatus('st','请填写 Wi-Fi 名称。');return}"
    "setStatus('st','保存并连接中…');try{var d=await j('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    "setStatus('st',d.ok?'保存成功，设备正在连接 '+d.ssid+' …':'保存失败：'+(d.error||''),d.ok)}catch(e){setStatus('st','网络请求失败。')}}"
    "async function saveStt(){var body={sttUrl:$('sttUrl').value.trim(),sttKey:$('sttKey').value,sttModel:$('sttModel').value.trim()};"
    "setStatus('st2','保存中…');try{var d=await j('/api/stt',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    "setStatus('st2',d.ok?'STT 设置已保存。':'保存失败：'+(d.error||''),d.ok)}catch(e){setStatus('st2','网络请求失败。')}}"
    "async function saveLlm(){var body={llmUrl:$('llmUrl').value.trim(),llmModel:$('llmModel').value.trim(),"
    "llmTemperature:num('llmTemp'),llmMaxTokens:num('llmMax'),llmContextTokens:num('llmCtx'),rolePrompt:$('rolePrompt').value};"
    "setStatus('st3','保存中…');try{var d=await j('/api/llm',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
    "setStatus('st3',d.ok?'LLM 设置已保存。':'保存失败：'+(d.error||''),d.ok);if(d.ok&&d.hasApiKey){$('llmKeyStatus').textContent='已配置'}}catch(e){setStatus('st3','网络请求失败。')}}"
    "async function setKey(){var k=prompt('输入 API Key（仅保存到设备）：');if(k==null)return;k=k.trim();if(!k)return;"
    "setStatus('st3','保存 Key 中…');try{var d=await j('/api/llm/key',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({apiKey:k})});"
    "setStatus('st3',d.ok?'API Key 已配置。':'失败：'+(d.error||''),d.ok);if(d.ok)$('llmKeyStatus').textContent='已配置'}catch(e){setStatus('st3','网络请求失败。')}}"
    "async function clearKey(){setStatus('st3','清除中…');try{var d=await j('/api/llm/clear-key',{method:'POST'});"
    "setStatus('st3',d.ok?'API Key 已清除。':'失败：'+(d.error||''),d.ok);if(d.ok&&d.hasApiKey===false)$('llmKeyStatus').textContent='未配置'}catch(e){setStatus('st3','网络请求失败。')}}"
    "async function load(){try{var d=await j('/api/status');if(d.name)$('dev').textContent=d.name;"
    "if(d.staIp)$('ip').textContent=d.staIp;"
    "if(d.saved&&d.ssid){$('host').value=d.host||'';$('port').value=d.tcpPort||9000;"
    "if(d.connected){setStatus('st','已连接 Wi-Fi：'+d.ssid)}}"
    "$('sttUrl').value=d.sttUrl||$('sttUrl').value;$('sttModel').value=d.sttModel||$('sttModel').value;"
    "$('llmUrl').value=d.llmUrl||'';$('llmModel').value=d.llmModel||'';"
    "if(d.llmTemperature)$('llmTemp').value=d.llmTemperature;if(d.llmMaxTokens)$('llmMax').value=d.llmMaxTokens;"
    "if(d.llmContextTokens)$('llmCtx').value=d.llmContextTokens;if(d.rolePrompt)$('rolePrompt').value=d.rolePrompt;"
    "if(d.hasApiKey){$('llmKeyStatus').textContent='已配置'}else{$('llmKeyStatus').textContent='未配置'}"
    "}catch(e){}}"
    "async function forget(){setStatus('st','正在忘记网络…');try{var d=await j('/api/forget',{method:'POST'});"
    "setStatus('st',d.ok?'已忘记网络，可重新配置。':'失败：'+(d.error||''),d.ok)}catch(e){setStatus('st','网络请求失败。')}}"
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

    char sta_ip[16] = "";
    net_wifi_get_sta_ip(sta_ip, sizeof(sta_ip));

    /* 角色提示词最长 1535 字节，加上转义与其余字段，缓冲取 4608 字节（堆分配） */
    size_t cap = 4608;
    char *buf = malloc(cap);
    if (buf == NULL)
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send_chunk(req, "{\"ok\":false,\"error\":\"no_memory\"}", HTTPD_RESP_USE_STRLEN);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }

    char temp[8];
    size_t pos = 0;
    pos = json_raw(buf, cap, pos, "{\"ok\":true,\"name\":\"");
    pos = json_esc_str(buf, cap, pos, cfg.name[0] ? cfg.name : NET_CFG_DEFAULT_NAME);
    pos = json_raw(buf, cap, pos, cfg.has_ssid ? "\",\"saved\":true,\"ssid\":\"" : "\",\"saved\":false,\"ssid\":\"");
    pos = json_esc_str(buf, cap, pos, cfg.has_ssid ? cfg.ssid : "");
    pos = json_raw(buf, cap, pos, "\",\"host\":\"");
    pos = json_esc_str(buf, cap, pos, cfg.host);
    pos = json_raw(buf, cap, pos, "\",\"tcpPort\":");
    pos = json_int(buf, cap, pos, (int)cfg.tcp_port);
    pos = json_raw(buf, cap, pos, ",\"staIp\":\"");
    pos = json_esc_str_n(buf, cap, pos, sta_ip, 15);
    pos = json_raw(buf, cap, pos, "\",\"sttUrl\":\"");
    pos = json_esc_str_n(buf, cap, pos, cfg.stt_url, NET_CFG_STT_URL_MAX - 1);
    pos = json_raw(buf, cap, pos, ",\"sttModel\":\"");
    pos = json_esc_str_n(buf, cap, pos, cfg.stt_model, NET_CFG_STT_MODEL_MAX - 1);
    /* LLM 配置：仅返回掩码状态与展示字段，绝不返回 Key 明文 */
    pos = json_raw(buf, cap, pos, "\",\"llmUrl\":\"");
    pos = json_esc_str_n(buf, cap, pos, cfg.llm.base_url, NET_CFG_LLM_URL_MAX - 1);
    pos = json_raw(buf, cap, pos, "\",\"llmModel\":\"");
    pos = json_esc_str_n(buf, cap, pos, cfg.llm.model, NET_CFG_LLM_MODEL_MAX - 1);
    snprintf(temp, sizeof(temp), "%.1f", cfg.llm.temperature);
    pos = json_raw(buf, cap, pos, "\",\"llmTemperature\":");
    pos = json_raw(buf, cap, pos, temp);
    pos = json_raw(buf, cap, pos, ",\"llmMaxTokens\":");
    pos = json_int(buf, cap, pos, (int)cfg.llm.max_tokens);
    pos = json_raw(buf, cap, pos, ",\"llmContextTokens\":");
    pos = json_int(buf, cap, pos, (int)cfg.llm.context_tokens);
    pos = json_raw(buf, cap, pos, ",\"hasApiKey\":");
    pos = json_raw(buf, cap, pos, cfg.llm.api_key[0] ? "true" : "false");
    pos = json_raw(buf, cap, pos, ",\"rolePrompt\":\"");
    pos = json_esc_str_n(buf, cap, pos, cfg.llm.role_prompt, NET_CFG_ROLE_PROMPT_MAX - 1);
    pos = json_raw(buf, cap, pos, net_wifi_is_connected() ? "\",\"connected\":true}" : "\",\"connected\":false}");
    (void)pos;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    free(buf);
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
/* POST 保存接口：/api/save /api/wifi /api/stt /api/llm(/key/clear-key) */
/* ------------------------------------------------------------------ */

/* 读取请求体；返回 false 表示请求体超过上限（payload_too_large） */
static bool read_body(httpd_req_t *req, char *buf, size_t cap)
{
    buf[0] = '\0';
    if (req->content_len >= cap)
    {
        ESP_LOGW(TAG, "body too large: %u >= %u", (unsigned)req->content_len, (unsigned)cap);
        return false;
    }
    size_t total = 0;
    while (total < req->content_len)
    {
        int ret = httpd_req_recv(req, buf + total, req->content_len - total);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            break;
        }
        total += (size_t)ret;
    }
    buf[total] = '\0';
    return true;
}

/* 响应 JSON（简化封装，避免每个 handler 重复写 chunk） */
static esp_err_t respond_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, json, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* 兼容旧接口 /api/save：Wi-Fi + STT 一起保存 */
static esp_err_t api_save_handler(httpd_req_t *req)
{
    char body[NET_PORTAL_BODY_BUF];
    if (!read_body(req, body, sizeof(body)))
        return respond_json(req, "{\"ok\":false,\"error\":\"payload_too_large\"}");

    char ssid[NET_CFG_SSID_MAX] = "", pass[NET_CFG_PASS_MAX] = "";
    char host[NET_CFG_HOST_MAX] = "", name[NET_CFG_NAME_MAX] = "";
    char stt_url[NET_CFG_STT_URL_MAX] = "", stt_key[NET_CFG_STT_KEY_MAX] = "", stt_model[NET_CFG_STT_MODEL_MAX] = "";
    double port_d = NET_CFG_DEFAULT_TCP_PORT;

    json_get_string(body, "ssid", ssid, sizeof(ssid));
    json_get_string(body, "pass", pass, sizeof(pass));
    json_get_string(body, "host", host, sizeof(host));
    json_get_string(body, "name", name, sizeof(name));
    json_get_string(body, "sttUrl", stt_url, sizeof(stt_url));
    json_get_string(body, "sttKey", stt_key, sizeof(stt_key));
    json_get_string(body, "sttModel", stt_model, sizeof(stt_model));
    json_get_number(body, "port", &port_d);

    if (ssid[0] == '\0')
        return respond_json(req, "{\"ok\":false,\"error\":\"missing_ssid\"}");

    ESP_LOGI(TAG, "save(legacy): ssid=%s host=%s port=%u name=%s (password not logged)",
             ssid, host, (unsigned)(uint16_t)port_d, name);

    esp_err_t err = net_config_save_stt(stt_url, stt_key, stt_model);
    if (err == ESP_OK)
        err = net_wifi_request_connect(ssid, pass, host, (uint16_t)port_d, name);
    if (err != ESP_OK)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        return respond_json(req, msg);
    }
    return respond_json(req, "{\"ok\":true}");
}

/* POST /api/wifi：仅保存 Wi-Fi/Windows 主机配置（不影响 STT/LLM） */
static esp_err_t api_wifi_handler(httpd_req_t *req)
{
    char body[NET_PORTAL_BODY_BUF];
    if (!read_body(req, body, sizeof(body)))
        return respond_json(req, "{\"ok\":false,\"error\":\"payload_too_large\"}");

    char ssid[NET_CFG_SSID_MAX] = "", pass[NET_CFG_PASS_MAX] = "";
    char host[NET_CFG_HOST_MAX] = "", name[NET_CFG_NAME_MAX] = "";
    double port_d = NET_CFG_DEFAULT_TCP_PORT;

    json_get_string(body, "ssid", ssid, sizeof(ssid));
    json_get_string(body, "pass", pass, sizeof(pass));
    json_get_string(body, "host", host, sizeof(host));
    json_get_string(body, "name", name, sizeof(name));
    json_get_number(body, "port", &port_d);

    if (ssid[0] == '\0')
        return respond_json(req, "{\"ok\":false,\"error\":\"missing_ssid\"}");

    ESP_LOGI(TAG, "wifi save: ssid=%s host=%s port=%u name=%s (password not logged)",
             ssid, host, (unsigned)(uint16_t)port_d, name);

    esp_err_t err = net_wifi_request_connect(ssid, pass, host, (uint16_t)port_d, name);
    if (err != ESP_OK)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        return respond_json(req, msg);
    }
    return respond_json(req, "{\"ok\":true}");
}

/* POST /api/stt：仅保存 ESP32 直连 STT 配置 */
static esp_err_t api_stt_handler(httpd_req_t *req)
{
    char body[NET_PORTAL_BODY_BUF];
    if (!read_body(req, body, sizeof(body)))
        return respond_json(req, "{\"ok\":false,\"error\":\"payload_too_large\"}");

    char stt_url[NET_CFG_STT_URL_MAX] = "", stt_key[NET_CFG_STT_KEY_MAX] = "", stt_model[NET_CFG_STT_MODEL_MAX] = "";
    json_get_string(body, "sttUrl", stt_url, sizeof(stt_url));
    json_get_string(body, "sttKey", stt_key, sizeof(stt_key));
    json_get_string(body, "sttModel", stt_model, sizeof(stt_model));

    esp_err_t err = net_config_save_stt(stt_url, stt_key, stt_model);
    if (err != ESP_OK)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        return respond_json(req, msg);
    }
    ESP_LOGI(TAG, "stt saved (key not logged)");
    return respond_json(req, "{\"ok\":true}");
}

/* 校验并归一化 Base URL：必须 https://，去尾随 '/'; 返回是否有效 */
static bool normalize_base_url(char *out, size_t out_size, const char *in)
{
    if (!in || !in[0] || out_size == 0) return false;
    if (strncmp(in, "https://", 8) != 0) return false;

    size_t len = strlen(in);
    while (len > 0 && in[len - 1] == '/') len--;
    if (len == 0 || len >= out_size) return false;
    memcpy(out, in, len);
    out[len] = '\0';
    return true;
}

/* POST /api/llm：保存 Base URL/模型/温度/Token 上限/角色提示词；Key 空=保持不变 */
static esp_err_t api_llm_handler(httpd_req_t *req)
{
    char body[NET_PORTAL_BODY_BUF];
    if (!read_body(req, body, sizeof(body)))
        return respond_json(req, "{\"ok\":false,\"error\":\"payload_too_large\"}");

    net_llm_config_t llm;
    {
        net_config_t cfg;
        net_config_load(&cfg);
        llm = cfg.llm; /* 保留现有 Key（save_llm 本就不写 Key） */
    }

    char url[NET_CFG_LLM_URL_MAX] = "", model[NET_CFG_LLM_MODEL_MAX] = "";
    char role_prompt[NET_CFG_ROLE_PROMPT_MAX] = "";
    double temp = 0, max_tokens = 0, ctx_tokens = 0;
    bool has_url = json_get_string(body, "llmUrl", url, sizeof(url));
    bool has_model = json_get_string(body, "llmModel", model, sizeof(model));
    bool has_prompt = json_get_string(body, "rolePrompt", role_prompt, sizeof(role_prompt));
    bool has_temp = json_get_number(body, "llmTemperature", &temp);
    bool has_max = json_get_number(body, "llmMaxTokens", &max_tokens);
    bool has_ctx = json_get_number(body, "llmContextTokens", &ctx_tokens);

    if (has_url && !normalize_base_url(llm.base_url, sizeof(llm.base_url), url))
        return respond_json(req, "{\"ok\":false,\"error\":\"invalid_url\"}");

    if (has_model)
    {
        if (!model[0] || strlen(model) > NET_CFG_LLM_MODEL_MAX - 1)
            return respond_json(req, "{\"ok\":false,\"error\":\"invalid_model\"}");
        /* 仅接受可打印字符 */
        for (const char *p = model; *p; p++)
        {
            if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F)
                return respond_json(req, "{\"ok\":false,\"error\":\"invalid_model\"}");
        }
        snprintf(llm.model, sizeof(llm.model), "%s", model);
    }

    if (has_prompt)
    {
        if (strlen(role_prompt) > NET_CFG_ROLE_PROMPT_MAX - 1)
            return respond_json(req, "{\"ok\":false,\"error\":\"payload_too_large\"}");
        snprintf(llm.role_prompt, sizeof(llm.role_prompt), "%s", role_prompt);
    }

    if (has_temp && (temp < 0.0 || temp > 2.0))
        return respond_json(req, "{\"ok\":false,\"error\":\"invalid_temperature\"}");
    if (has_max && (max_tokens < 64.0 || max_tokens > 2048.0))
        return respond_json(req, "{\"ok\":false,\"error\":\"invalid_max_tokens\"}");
    if (has_ctx && (ctx_tokens < 512.0 || ctx_tokens > 12000.0))
        return respond_json(req, "{\"ok\":false,\"error\":\"invalid_context_tokens\"}");
    if (has_temp) llm.temperature = (float)temp;
    if (has_max) llm.max_tokens = (uint16_t)max_tokens;
    if (has_ctx) llm.context_tokens = (uint16_t)ctx_tokens;

    esp_err_t err = net_config_save_llm(&llm);
    if (err != ESP_OK)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        return respond_json(req, msg);
    }
    ESP_LOGI(TAG, "llm saved: url=%s model=%s temp=%.1f max=%u ctx=%u (key not touched)",
             llm.base_url, llm.model, llm.temperature, (unsigned)llm.max_tokens,
             (unsigned)llm.context_tokens);
    return respond_json(req, llm.api_key[0] ? "{\"ok\":true,\"hasApiKey\":true}" : "{\"ok\":true,\"hasApiKey\":false}");
}

/* POST /api/llm/key：设置/更新 LLM API Key（不回显） */
static esp_err_t api_llm_key_handler(httpd_req_t *req)
{
    char body[NET_PORTAL_BODY_BUF];
    if (!read_body(req, body, sizeof(body)))
        return respond_json(req, "{\"ok\":false,\"error\":\"payload_too_large\"}");

    char key[NET_CFG_LLM_KEY_MAX] = "";
    json_get_string(body, "apiKey", key, sizeof(key));
    if (!key[0])
        return respond_json(req, "{\"ok\":false,\"error\":\"missing_key\"}");

    esp_err_t err = net_config_set_llm_key(key);
    if (err != ESP_OK)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        return respond_json(req, msg);
    }
    ESP_LOGI(TAG, "llm key updated (not logged)");
    return respond_json(req, "{\"ok\":true,\"hasApiKey\":true}");
}

/* POST /api/llm/clear-key：清除 LLM API Key */
static esp_err_t api_llm_clear_key_handler(httpd_req_t *req)
{
    (void)req;
    esp_err_t err = net_config_clear_llm_key();
    if (err != ESP_OK)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(err));
        return respond_json(req, msg);
    }
    ESP_LOGI(TAG, "llm key cleared");
    return respond_json(req, "{\"ok\":true,\"hasApiKey\":false}");
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
    config.stack_size = 16384;
    config.server_port = NET_PORTAL_PORT;
    config.max_open_sockets = 6;
    config.max_uri_handlers = 12;

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
    uri.uri = "/api/wifi";
    uri.handler = api_wifi_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_POST;
    uri.uri = "/api/stt";
    uri.handler = api_stt_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_POST;
    uri.uri = "/api/llm";
    uri.handler = api_llm_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_POST;
    uri.uri = "/api/llm/key";
    uri.handler = api_llm_key_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_POST;
    uri.uri = "/api/llm/clear-key";
    uri.handler = api_llm_clear_key_handler;
    httpd_register_uri_handler(s_server, &uri);

    uri.method = HTTP_POST;
    uri.uri = "/api/forget";
    uri.handler = api_forget_handler;
    httpd_register_uri_handler(s_server, &uri);

    ESP_LOGI(TAG, "portal started (resident): AP http://192.168.4.1, STA http://<ip>/");
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
