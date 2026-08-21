# AI 聊天机器人项目计划（SuperMilkCard · Windows 端）

> 本文档是 Windows 桌面端「AI 聊天机器人」的完整开发计划。
> 现状：`Windows/` 已是可运行的 Electron + Vue 3 + TypeScript 脚手架，尚未实现业务功能。
> 本计划在此基础上增量开发，不破坏现有结构。

---

## 1. 项目概述

在现有 Electron + Vue 3 + TS 桌面应用内，构建一个**带 Live2D 虚拟形象**的 AI 聊天机器人。核心体验：

- 用户与一个「有性格」的 AI 机器人对话；
- 机器人有 Live2D 立绘形象，能根据 AI 回复的情绪/指令做出动作和表情；
- 支持文本输入、语音输入（STT）与语音朗读回复（TTS）；
- 回复以**流式打字机**效果输出；
- API Key、Base URL、各类模型均可自定义，兼容 OpenAI 及任何 OpenAI 协议的服务（DeepSeek / Moonshot / Ollama / 通义 / 本地 LLM 等）。

---

## 2. 功能需求清单

| # | 需求 | 说明 |
|---|------|------|
| F1 | AI 聊天 | 多轮对话，文本消息收发 |
| F2 | 流式输出 | 回复逐字/逐 token 渲染，可中断 |
| F3 | 上下文管理 | 保留对话历史、估算 token、超限自动裁剪/压缩 |
| F4 | 机器人人格 | 可编辑系统提示词（人设），支持多个人格预设切换 |
| F5 | Live2D 动作 | 加载 Live2D 模型；AI 回复可触发动作/表情 |
| F6 | 自定义 API Key | Base URL / API Key / 模型名可配置，本地持久化 |
| F7 | 文转语音 TTS | 回复可朗读；可配置 TTS 引擎与模型/音色 |
| F8 | 语音转文字 STT | 麦克风输入转文字；可配置 STT 引擎与模型 |
| F9 | 模型配置 | LLM / TTS / STT 各自独立配置 |
| F10 | 美观界面 | 现代化聊天 UI + Live2D 舞台，浅色/深色主题 |

---

## 3. 技术选型

| 领域 | 方案 | 理由 |
|------|------|------|
| 桌面框架 | Electron 39 + electron-vite 5 | 已就绪，沿用 |
| 前端 | Vue 3.5 + TypeScript | 已就绪，沿用 |
| UI 组件库 | naive-ui 2.x | 已安装，组件丰富、主题可定制 |
| 状态管理 | Pinia 4.x | 已安装 |
| 路由 | vue-router 5.x | 已安装（用于设置页/聊天页等视图） |
| Live2D 渲染 | `pixi.js` + `pixi-live2d-display` | 浏览器内渲染 Cubism 模型，API 简洁，社区成熟 |
| LLM 流式 | OpenAI 兼容 `chat/completions`（SSE） | 通用协议，主进程 Node `fetch` 流式读取 |
| TTS | 首选 Web Speech API（内置），可扩展 OpenAI TTS / Edge TTS | 内置免费；云端可配置 |
| STT | 首选 Web Speech API（`webkitSpeechRecognition`），可扩展 Whisper API | 内置免费；云端可配置 |
| 配置持久化 | `electron-store` | 主进程读写 `userData` 下 JSON，类型安全 |
| 样式 | SCSS/CSS 变量 + naive-ui 主题 | 实现浅/深色主题 |

> 关键约束：渲染进程当前 CSP 为 `default-src 'self'`，禁止直接外网请求。
> 因此所有外网调用（LLM / 云 TTS / 云 STT）统一放到 **主进程**，通过 IPC 与渲染进程通信。

---

## 4. 系统架构

```
┌───────────────────────────── 渲染进程 (Renderer) ─────────────────────────────┐
│  Vue App                                                                       │
│  ├─ 视图：聊天页 ChatView / 设置页 SettingsView（集中管理全部配置，含人格）      │
│  ├─ 组件：ChatPanel · MessageList · InputBar · Live2DStage · SettingsForm       │
│  ├─ Pinia stores：chat · settings · live2d · audio                             │
│  └─ 本地能力：Web Speech API（TTS 朗读 / STT 识别）                              │
└───────────────▲───────────────────────────────────────────────────────────────┘
                │ contextBridge (window.api.*)  /  IPC 事件（流式 chunk）
┌───────────────┴─────────────── 预加载 (Preload) ───────────────────────────────┐
│  window.api：llm.chatStream / settings.get|set / stt.transcribe / tts.synthesize│
└───────────────────────────────▲───────────────────────────────────────────────┘
                                │ ipcMain / ipcRenderer
┌───────────────────────────────┴─────────────── 主进程 (Main) ──────────────────┐
│  services/                                                                     │
│  ├─ llm.ts         OpenAI 兼容流式请求，逐 token 通过事件推送                    │
│  ├─ tts.ts         云 TTS 合成（返回音频数据）                                   │
│  ├─ stt.ts         云 STT 转写（上传音频 → 文本）                                │
│  ├─ settings.ts    electron-store 读写配置                                      │
│  └─ ipc.ts         IPC 注册与流式通道封装                                       │
└───────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. 目录结构规划（`Windows/src/` 内新增/改动）

```
src/
├─ main/                        # 主进程
│  ├─ index.ts                  # （改）注册 IPC、初始化 settings
│  ├─ services/
│  │  ├─ llm.ts                 # LLM 流式
│  │  ├─ tts.ts                 # 云 TTS
│  │  ├─ stt.ts                 # 云 STT
│  │  ├─ settings.ts            # electron-store 封装
│  │  └─ context.ts             # 上下文裁剪/token 估算（纯函数，可复用）
│  └─ ipc.ts                    # IPC 通道定义
├─ preload/
│  ├─ index.ts                  # （改）暴露 window.api
│  └─ index.d.ts                # （改）API 类型
└─ renderer/src/
   ├─ App.vue                   # （改）布局：Live2D 舞台 + 聊天面板 + 路由视图
   ├─ router/index.ts           # 新增：路由
   ├─ stores/
   │  ├─ chat.ts                # 消息列表、流式写入、上下文
   │  ├─ settings.ts            # 配置（镜像主进程，带缓存）
   │  ├─ live2d.ts              # 模型加载、动作/表情映射
   │  └─ audio.ts               # TTS/STT 状态
   ├─ services/
   │  ├─ speech.ts              # Web Speech API 封装（TTS 朗读 + STT 识别）
   │  └─ live2dModel.ts         # pixi-live2d-display 加载与指令
   ├─ components/
   │  ├─ chat/ChatPanel.vue · MessageBubble.vue · InputBar.vue
   │  ├─ live2d/Live2DStage.vue · EmotionTag.vue
   │  └─ settings/SettingsForm.vue · ModelConfigCard.vue
   ├─ views/
   │  ├─ ChatView.vue · SettingsView.vue（人格编辑并入设置页）
   ├─ types/                    # 共享类型（可放 packages 或直接定义）
   ├─ composables/              # useChatStream / useSpeech 等
   └─ assets/                   # 主题变量、Live2D 模型资源
```

---

## 6. 模块详细设计

### 6.1 LLM 对话与流式输出（F1、F2、F6）

- **协议**：OpenAI 兼容 `POST {baseUrl}/chat/completions`，`stream: true`，SSE 响应。
- **实现位置**：主进程 `llm.ts` 用 Node 18+ 原生 `fetch` 读取 `ReadableStream`，逐行解析 `data: {...}` 块。
- **推送方式**：`webContents.send('llm:chunk', { chatId, delta })`，`llm:done` / `llm:error` / `llm:abort`。
- **中断**：渲染进程可调用 `llm.abort(chatId)`，主进程调用 `AbortController.abort()`。
- **请求参数**：`model`、`messages`、`temperature`、`max_tokens`、`stream` 由配置驱动。
- **安全性**：API Key 仅存主进程 `userData` 配置，不暴露给渲染层（渲染层只拿到「是否已配置」状态）。

### 6.2 上下文管理（F3）

- 数据模型：`{ role: 'system'|'user'|'assistant', content: string }` 消息数组。
- **token 估算**：纯函数，按字符数粗略估算（中文≈1 token/字，英文≈4 字符/token），后续可换 `gpt-tokenizer`/`js-tiktoken` 精算。
- **裁剪策略**（在发送前执行）：
  1. 固定保留 `system`（人格）与最近若干轮；
  2. 超出 `maxContextTokens` 时，从最早的 `user/assistant` 开始丢弃（滑动窗口）；
  3. 可选「自动摘要」：丢弃前先把旧对话压缩成一条摘要消息（后续版本）。
- **配置项**：`maxContextTokens`、`historyKeepTurns`、是否开启摘要。
- 支持「清空对话 / 新会话」按钮。

### 6.3 机器人人格（F4）

- 人格 = 一条（或一组）`system` 消息 + 展示信息（名字、头像、简介）。
- **预设**：内置 2~3 个（如「元气少女」「冷静助手」「毒舌吐槽」），用户可新建/编辑/删除。
- **存储**：随 settings 持久化；在设置页 `SettingsView` 内提供编辑表单与实时预览。
- 与 Live2D 联动：人格可声明「默认表情/动作」映射。

### 6.4 Live2D 集成与 AI 动作控制（F5）

- **渲染**：`pixi-live2d-display` 加载 `.model3.json` 模型，挂到 `Live2DStage.vue` 的 canvas。
- **动作**：调用模型的 `motion(group, index)`、`expression(id)` 方法。
- **AI 控制动作的两种机制（分层实现）**：
  1. **结构化标签（推荐，MVP）**：系统提示词要求 AI 在回复中以约定格式输出情绪标签，如
     `[emotion: happy]` 或 `[action: wave]`；前端解析标签 → 映射到 Live2D 动作/表情，标签不出现在气泡文本中。
  2. **情绪分类（进阶）**：对回复文本做关键词/情绪打分，映射到预定义动作组（`idle / happy / sad / angry / surprised / thinking`）。
- **动作映射表**：`emotion → { motion: [...], expression: '...' }`，可配置、可扩展。
- **模型资源**：内置一个可商用/测试模型；支持用户在设置中导入本地模型目录。
- **渲染性能**：Live2D 单独 canvas，避免与 DOM 动画互相阻塞；`requestAnimationFrame` 由库内部管理。

### 6.5 文转语音 TTS（F7、F9）

- **方案一（默认，零配置）**：Web Speech API `SpeechSynthesis`，用系统音色朗读回复；支持「朗读开关」「音色/语速/音量」选择。
- **方案二（云端，可配置）**：主进程 `tts.ts` 调 OpenAI `/audio/speech`（或其他 TTS 端点），返回音频 base64/buffer → 渲染层播放。
- **配置**：TTS 引擎（系统/云端）、`model`（如 `tts-1`）、`voice`、`speed`。
- **触发**：消息气泡「🔊 朗读」按钮 + 「自动朗读回复」开关。

### 6.6 语音转文字 STT（F8、F9）

- **方案一（默认）**：Web Speech API `SpeechRecognition`/`webkitSpeechRecognition`，实时识别，识别中显示候选文本，结束时填入输入框。
- **方案二（云端）**：`MediaRecorder` 录制成音频 → 主进程 `stt.ts` 调 Whisper `/audio/transcriptions` → 返回文本。
- **配置**：STT 引擎、`model`（如 `whisper-1`）、语言。
- **交互**：输入框旁「🎤 按住说话/点击录音」按钮，带录音动画与状态提示。

### 6.7 设置与模型配置（F6、F9）

> **专门的设置页 `SettingsView`（独立路由 `/settings`）集中管理全部配置项**：
> 聊天页只负责对话，所有配置统一在设置页完成，通过顶部导航进入。

- 设置页分组（一个页面、分区卡片，覆盖全部配置）：
  - **LLM（大模型）**：Base URL、API Key、模型名、temperature、maxContextTokens、maxTokens。
  - **TTS（文转语音）**：引擎（系统/云端）、模型、音色、语速、自动朗读开关。
  - **STT（语音转文字）**：引擎（系统/云端）、模型、语言。
  - **人格（机器人）**：当前人格 + 人设编辑（名称、头像、系统提示词、默认表情）。
  - **外观**：浅色/深色主题。
- **持久化**：`electron-store` 存 JSON；设置项校验（URL 格式、必填项）。
- **Key 展示**：输入框遮蔽，支持显示/隐藏、测试连接按钮。
- **保存方式**：「保存」按钮显式提交 + 改动即时预览，保存后回写主进程并持久化。

### 6.8 UI 界面（F10）

- **布局**：左侧/中央 Live2D 舞台（机器人形象），右侧聊天面板；或「顶部舞台 + 底部对话」自适应。
- **聊天体验**：消息气泡（用户/机器人两侧）、流式打字、打字指示器、时间戳、朗读按钮、复制按钮、重新生成、停止生成。
- **输入区**：文本框自适应高度、发送/停止按钮、🎤 语音按钮。
- **主题**：CSS 变量 + naive-ui `darkTheme`，浅/深色切换。
- **细节**：空状态引导、加载态、错误提示（如 Key 无效、网络失败）。

---

## 7. IPC 与类型约定（Preload 暴露的 `window.api`）

```ts
window.api = {
  llm: {
    chatStream(req: ChatStreamRequest): { chatId: string }
    abort(chatId: string): void
  },
  settings: {
    get(): Promise<AppSettings>
    set(partial: Partial<AppSettings>): Promise<AppSettings>
  },
  tts: {
    synthesize(req: TtsRequest): Promise<{ audioBase64: string }> // 云端方案
  },
  stt: {
    transcribe(audioBase64: string, opts): Promise<{ text: string }> // 云端方案
  },
  on(channel, cb): () => void   // 订阅 llm:chunk / llm:done / llm:error
}
```

- 事件通道：`llm:chunk` / `llm:done` / `llm:error` / `llm:aborted`。
- 类型定义放 `renderer/src/types` 与 `main/services` 共享，`preload/index.d.ts` 提供全局 `window.api` 类型。

---

## 8. 分阶段实施计划（里程碑）

| 阶段 | 内容 | 产出 / 验收 |
|------|------|------------|
| M0 | 计划与脚手架核对 | 本计划文档；确认现有依赖与缺口 |
| M1 | 基础架构 | 主进程 settings + IPC + 预加载 API + Pinia stores 骨架；设置页可读写配置 |
| M2 | 文本聊天 + 流式 | 可配置 Key 后完成多轮流式对话；支持停止/重新生成 |
| M3 | 上下文管理 + 人格 | 历史保留、token 裁剪、人格预设与编辑 |
| M4 | Live2D 集成 | 模型渲染 + 动作/表情 API + AI 情绪标签 → 动作映射 |
| M5 | TTS + STT | 系统语音朗读/识别可用；云端方案可配置 |
| M6 | 界面打磨 + 打包 | 浅/深主题、动效、错误处理；打包产出 **macOS + Windows** 两版本 |

> 建议按 M1→M2→M3 打通「可对话」最小闭环，再叠加 Live2D 与语音，最后打磨 UI。

---

## 9. 风险与注意事项

- **Live2D SDK 授权**：Cubism 官方 SDK 有使用条款；`pixi-live2d-display` 需自备合规模型，避免使用无授权模型资源。
- **Web Speech API 兼容性**：`webkitSpeechRecognition` 在部分环境需联网且对非 Chrome 内核支持有限；云端 STT 作为兜底。
- **CSP 限制**：外网请求必须走主进程，避免渲染层直接 fetch 被 CSP 拦截。
- **流式 IPC 性能**：chunk 高频时需节流/合并（如每 16ms 合并一次），避免渲染卡顿。
- **API Key 安全**：仅存本地 `userData`，不做云端同步；日志中脱敏。
- **token 估算精度**：先用粗估，后续可选接入精确 tokenizer。

---

## 10. 验收标准（对应需求）

1. 配置 Base URL / API Key / 模型后，能与机器人进行多轮对话（F1/F6）。
2. 回复以流式逐字显示，可中途停止（F2）。
3. 长对话自动裁剪且不丢人格设定；可清空历史（F3）。
4. 可切换/编辑人格，回复风格随之变化（F4）。
5. Live2D 模型正常显示，且 AI 回复能触发对应动作/表情（F5）。
6. 回复可朗读（TTS），麦克风可输入并转文字（F7/F8）。
7. LLM/TTS/STT 三组模型可在专门设置页独立配置并持久化（F9）。
8. 界面美观，浅/深主题切换正常；`npm run dev` 通过（F10）。
9. 可打包出 **macOS**（dmg）与 **Windows**（nsis 安装包）两个平台的安装产物（新增）。

---

## 11. 打包与发布（macOS / Windows 双平台）

- **目标**：同一套代码，产出两个平台安装包。
  - **Windows**：`npm run build:win` → NSIS 安装程序（`*-setup.exe`），含桌面快捷方式、卸载程序。
  - **macOS**：`npm run build:mac` → `.dmg` 磁盘映像（`*.dmg`）。
- **配置来源**：`electron-builder.yml` 已定义 `win`（nsis）与 `mac`（dmg）段；`package.json` 提供 `build:win` / `build:mac` 脚本。
- **平台约束**：`build:mac` 需在 macOS 上执行；`build:win` 在 macOS/Windows 均可（Windows 打包在 macOS 上可能需 wine 做部分签名/改包，建议在各自平台构建对应产物）。
- **产物目录**：`Windows/dist/`（electron-vite 构建）与 `Windows/release/`（electron-builder 打包），已加入根 `.gitignore`。
- **待完善**：应用名/图标/`appId` 由占位值改为正式值（`productName`、`win.executableName`、`appId`）；macOS 若需分发可补签名/公证。
