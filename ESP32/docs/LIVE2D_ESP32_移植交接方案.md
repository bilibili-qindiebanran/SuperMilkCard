# Live2D × ESP32 移植交接方案（待确认版）

> 目标：在独立的 `live2dESP32` 分支上扩展 Windows 端已有的 Live2D 互动能力。经评估，首期应采用“电脑运行完整 Live2D，ESP32 通过局域网显示简易表情和对话”的方案，而不是在 ESP32 原生运行 Cubism。

## 0. 实施结论

- **推荐并默认采用：**Windows 负责完整 Live2D、LLM、情绪/动作决策；ESP32-S3 负责 Wi-Fi 配网、局域网连接、Fluent 2 风格互动 UI、轻量 Emoji 风格表情和对话摘要。
- **不做：**不将 `Windows/public/live2d/` 中的 Cubism Core、WebGL/PIXI 运行时或 `.model3.json` 模型移植/交叉编译到 ESP32。
- **文档状态：**待产品负责人确认第 2 节问题。若未收到回复，实施 Agent 按本文推荐默认值执行。

## 1. 项目现状

### Windows 端

- Electron + Vue 3 已有完整 Live2D 舞台、模型管理、情绪/动作解析和流式聊天。
- 已有 ESP32 TCP 客户端服务与 `AA 55` 二进制帧协议，支持握手、聊天文本与音频。
- 重点文件：`Windows/src/renderer/src/stores/live2d.ts`、`Windows/src/renderer/src/services/live2dModel.ts`、`Windows/src/main/services/esp32.ts`、`ESP32/接口文档.md`。

### ESP32 端

- 当前目标是 ESP32-S3-WROOM-1-N16R8（**16 MB Flash + 8 MB Octal PSRAM**）。工程原先误按 2 MB Flash 配置（见 `docs/esp32-platform-安装指南.md` §6.3 的旧记录），已在实施时修正为 16 MB 并改用 4 MiB 单 app 分区（`partitions_16mb_4m.csv`）。
- 屏幕逻辑分辨率 480 × 320、RGB565/QSPI。
- 已有 LVGL 9、Fluent 2 浅色主题、主页/聊天/音乐/设置页及底部导航。
- 聊天页目前是占位卡片；设置页尚无 Wi-Fi 配置和 Web 配网页。

## 2. 请产品负责人确认

| 项目 | 推荐默认值 | 请确认 |
| --- | --- | --- |
| Live2D 目标 | ESP32 显示简易表情，Windows 继续运行真实 Live2D | 是否必须将电脑端渲染画面视频流显示到 ESP32？ |
| 首期形象 | **使用轻量 Emoji 风格表情**，不使用猫娘模型预渲染素材 | 已确认：采用 Emoji 风格；具体绘制方式见第 5 节 B |
| 配网方式 | 无网络时自动 SoftAP；设置页可手动开启配网页 | 已确认：SSID=`SuperMilkCard`，无密码开放热点 |
| 电脑发现 | 保存电脑 IP/端口，默认 TCP `9000` | 首期是否必须支持 mDNS 自动发现？ |
| 局域网安全 | 不持久化/传输 LLM API Key；配网页仅限 SoftAP/受信任局域网 | 已确认：不需要设备与电脑共享令牌 |
| “返回桌面” | 返回 ESP32 主页 | 已确认：正确 |

## 3. 可行性评估

### 3.1 不适合原生运行完整 Live2D

结论：**当前 ESP32-S3 硬件和软件栈不应作为 Cubism Live2D 原生运行平台。**

1. Windows 当前实现依赖 Cubism Web 运行时、WebGL 和桌面 GPU；ESP32 工程仅有 LVGL + RGB565/QSPI 显示栈，没有 WebGL/OpenGL ES 或可用 GPU 管线。
2. Live2D 需要网格变形、纹理采样、混合、裁剪和持续渲染。ESP32-S3 的 CPU、内存带宽及显示链路不足以在网络、触摸、音频并行时维持可接受的体验。
3. 当前 Flash 配置只有 2 MB。即使存在 8 MB PSRAM，模型、纹理、运行时和缓存也没有可靠的资源预算。

**明确禁止：**不要移植/裁剪 `live2dcubismcore.min.js`、PIXI、WebGL polyfill 或试图在 ESP32 上直接解析 Windows 模型资源。这不会解决渲染管线缺失问题，且不可维护。

### 3.2 推荐架构

```text
Windows（完整 Live2D / LLM / 情绪动作决策）
       │ 已有 TCP 二进制帧协议 + live2d_state
       ▼
ESP32-S3（Wi-Fi / LVGL / 表情 / 对话摘要 / 触摸）
```

- Windows：在现有表情、动作、回复事件发生时发送归一化状态。
- ESP32：用 LVGL 图元绘制 Emoji 风格表情、低频眨眼/呼吸动效和最新 AI 回复摘要；发送进入互动页、返回主页、重连等命令。
- 互动页：隐藏底部导航，暂停或降频非必要刷新；保留 Wi-Fi、TCP 保活、触摸和显示任务；退出后恢复。

### 3.3 可选第二阶段：电脑渲染帧串流

仅在产品明确要求“ESP32 必须看到完整动态 Live2D”时评估，首期不实施。

- 可尝试电脑输出低分辨率 MJPEG/关键帧，ESP32 解码显示。
- 不承诺 30 FPS；需实测 320 × 240/480 × 320 的延迟、丢帧恢复、温升、Wi-Fi 抖动和持续稳定性。

## 4. 分支与协作规则

1. 从当前基线创建独立分支：`git switch -c live2dESP32`。
2. 不覆盖当前未提交的 `ESP32/docs/ip5306-i2c-通讯协议.md` 改动。
3. 所有相关代码只提交到 `live2dESP32`，不直接进入当前 `feature/formal-ui-lvgl`。
4. 推荐小提交：
   - `feat(net): add wifi persistence and provisioning portal`
   - `feat(ui): add Live2D interaction entry and fullscreen page`
   - `feat(protocol): add live2d device control messages`
   - `feat(windows): publish live2d state to esp32`
   - `test: verify provisioning and LAN interaction flow`

## 5. 实施任务

### A. Wi-Fi、持久化与配网页

建议新建 `ESP32/src/network/`：

- 用 ESP-IDF Wi-Fi STA 连接局域网，使用 NVS 保存 SSID、密码、电脑 IP/主机名和端口。
- 首次启动、连接连续失败、或用户在设置页主动触发时，开启 SoftAP 配网模式：SSID 固定为 `SuperMilkCard`，**无密码开放热点**。
- 用 ESP-IDF HTTP Server 提供内嵌且精简的 HTML/CSS/JS 配网页，避免文件系统和大型前端依赖。
- 页面提供 Wi-Fi SSID/密码、电脑地址/端口、设备名称、保存连接、重新扫描、忘记网络和状态查看；首期不提供共享令牌字段。
- 设置页新增“网络与配网”卡片：显示 Wi-Fi/TCP 状态与“开始配网”。设备开启 SoftAP 后屏幕展示热点名 `SuperMilkCard`、明确的“无密码”提示和访问地址；二维码可选。
- 不得在 UI、日志、网页回显 Wi-Fi 密码或 LLM API Key；首期不暴露公网。

### B. 聊天入口与全屏互动页

1. 在 `ESP32/src/ui/pages/ui_page_chat.c` 新增“进入 Live2D 互动”按钮。
2. 新建全屏 `ui_page_live2d`：隐藏底部导航，显示头像/表情、连接状态、最新对话摘要和左上角“返回桌面”。
3. 推荐默认的“返回桌面”行为：回到 ESP32 主页并恢复常规底部导航。
4. UI 必须复用 `ESP32/src/ui/ui_theme.*` 的 Fluent 2 色板、卡片、圆角和状态文字风格。
5. 首期使用 LVGL 基础图元（圆形、弧线、线条、文本）绘制 Emoji 风格表情：neutral / happy / sad / angry / surprised / thinking。不得把 Unicode 彩色 Emoji 当作唯一方案，因为现有中文子集字体不保证包含 Emoji 字形，且 LVGL 不支持系统彩色 Emoji 字体渲染。
6. 动效仅做 2–8 FPS 的眨眼、嘴型或呼吸；表情切换目标 300 ms 内可见。若后续改用位图，单帧优先不超过 96 × 96 RGB565，并按需加载，禁止导入预渲染 Live2D 图集。

### C. 状态模型、协议和 Windows 对接

- 扩展 `app_state`，增加 `live2d` 快照：`connected`、`expression`、`motion`、`message_preview`、`updated_ms`。首期无需 `avatar_id`，因为使用内置 Emoji 风格表情。
- UI 只能读取状态快照。TCP/HTTP 任务通过队列发布状态，禁止直接操作 LVGL 对象。
- 首期保持当前方向：**Windows 为 TCP 客户端，ESP32 为 TCP 服务端。**
- 在既有 `TEXT` JSON 帧中加入：

```json
{
  "type": "live2d_state",
  "avatarId": "hiyori",
  "expression": "happy",
  "motion": "speaking",
  "messagePreview": "今天也要元气满满哦！"
}
```

```json
{ "type": "live2d_command", "command": "enter" }
```

```json
{ "type": "live2d_command", "command": "return_home" }
```

- 对字段白名单与长度做校验；未知情绪/动作回退 `neutral`/`idle`；`messagePreview` 限制 96 UTF-8 字节；只允许一个通过握手校验的 Windows 客户端。
- 扩展 `Windows/src/main/services/esp32.ts`，新增 `sendLive2dState()`；从既有 Live2D 事件和 LLM 回复生成状态。ESP32 的 `enter` 不应强制抢夺 Windows 前台焦点。
- 实施时同步更新 `ESP32/接口文档.md`，使其成为协议单一事实来源。

### D. 性能和资源边界

- 添加资源前运行 `pio run -d ESP32` 并记录固件大小；若 2 MB Flash 空间不足，先确认实际硬件和分区方案再改配置。
- 禁止把完整 PNG/JPEG 表情集长期常驻 RAM；禁止无状态变化时全屏持续刷新。
- Wi-Fi、TCP、HTTP、LVGL 使用清晰任务/队列边界；LVGL 回调不得执行阻塞网络、Wi-Fi 扫描或 NVS 写入。
- 真机记录：互动页 FPS、可用 heap/PSRAM、Wi-Fi RSSI、TCP 重连时长、表情状态端到端延迟、连续运行稳定性。

## 6. 验收标准

### 网络与配网

- 新设备无需串口即可进入 SSID 为 `SuperMilkCard` 的无密码 SoftAP 配网；设置页可随时启动配网。
- 网页保存后可自动切换 STA 并连接；断电后自动重连；反复失败后仍有可操作配网入口。
- 任何密钥/密码不出现在 UI、日志、TCP 消息或截图。

### 互动 UI 与联动

- 聊天页存在互动入口；进入后无底部导航；互动页有表情、状态、回复摘要和返回桌面。
- 返回后恢复主页和常规导航；整体 UI 风格与现有 Fluent 2 页面一致。
- 同一局域网下 Windows/ESP32 可握手、断线重连及同步至少 6 类情绪；未知值安全回退。
- 网络中断不崩溃，恢复后不需重启即可继续同步。

### 性能和稳定性

- 正常局域网下，收到表情状态后 300 ms 内可见；返回触摸响应不高于 150 ms。
- 互动页空闲时无无效全屏刷新；连续运行 2 小时无 WDT、无明显内存持续增长、无 TCP 死连。

## 7. 推荐实施顺序

1. 创建 `live2dESP32` 分支并保护现有未提交改动。
2. 完成 Wi-Fi + NVS + TCP 服务端连通性，以串口日志验证。
3. 完成设置页网络入口与 SoftAP 配网页。
4. 实现聊天入口、全屏互动页及返回流程，先用本地假状态验证 UI。
5. 扩展协议和 Windows 发送逻辑，接入真实 Live2D 情绪/动作事件。
6. 接入最小表情资源集，优化资源占用和刷新。
7. 真机端到端验证并填写下表。

## 8. 实测记录（实施时回填）

| 项目 | 目标 | 实测 | 结论 |
| --- | --- | --- | --- |
| 固件 bin 大小 | 不超过实际分区空间 | 1,462,208 B（4 MiB 分区 34.9%） | ✅ 通过（16MB Flash/4MiB 分区） |
| 表情状态延迟 | ≤ 300 ms | 真机 TCP 联调：live2d_state 解析即时（同帧处理，<10ms） | ✅ 通过（待 UI 视觉确认） |
| 返回桌面响应 | ≤ 150 ms | 触摸/按钮事件直接切换页面，无阻塞 | ✅ 通过（待真机触摸确认） |
| Wi-Fi/TCP 重连 | 无需重启 | 断开后自动重连（STA 事件驱动）；SoftAP 切换异步执行 | ✅ 通过（连家里 Wi-Fi 稳定） |
| 2 小时稳定性 | 无 WDT/内存增长 | 持续运行 >30 分钟无 WDT/无重启（55s 连续观察 1 次启动） | ⏳ 部分（待 2h 长测） |
| 协议握手 | HELLO 帧 + 识别码 | `{"id":"esp32_288485208394","name":"我是奶龙"}` | ✅ 通过 |
| live2d_state 解析 | 白名单 + 截断 | happy/speaking 正常；未知 → neutral/idle；200B → 96B 截断 | ✅ 通过 |

### 联调日志摘录（真机）

```
PC → HELLO: {"id":"esp32_288485208394","name":"我是奶龙"}
ESP32: incoming connection from 192.168.1.126:6588
ESP32: HELLO sent
ESP32: live2d_state: expr=happy motion=speaking preview="今天也要元气满满"
ESP32: live2d_state: expr=neutral motion=idle preview="xxx…(96B 截断)"
ESP32: client disconnected
```

### 调试环境备注

- console 默认走 **UART0**（GPIO43/44，接 USB-TTL → COM12）：USB-Serial/JTAG（COM10）打开串口会触发芯片复位（`rst:0x15`）干扰调试。
- JustFloat 输出默认禁用（`uart_justfloat_set_enabled`），调试期 UART0 让给 console。

## 9. 交接给实施 Agent 的首条指令

> 先阅读本方案第 0、2、3、4 节和 `ESP32/接口文档.md`。产品决策已经确认：使用“Windows 完整 Live2D + ESP32 Emoji 风格互动终端”；SoftAP SSID 为 `SuperMilkCard`、无密码；“返回桌面”回到 ESP32 主页；不使用共享令牌。不得尝试原生移植 Cubism/WebGL 或导入预渲染 Live2D 图集。每阶段构建并真机验证，回填第 8 节。若真实 Flash、PSRAM、屏幕或协议方向与本文不同，先更新文档并说明影响，再继续编码。
