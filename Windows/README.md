# SuperMilkCard（Windows 桌面端）

> Electron + Vue 3 + TypeScript 实现的 AI 聊天机器人桌面应用：带 **Live2D 虚拟形象**、**流式对话**、**语音交互**。

---

## 目录

- [功能特性](#功能特性)
- [技术栈](#技术栈)
- [环境要求](#环境要求)
- [快速开始](#快速开始)
- [构建与打包](#构建与打包)
- [使用说明](#使用说明)
- [配置持久化](#配置持久化)
- [安全设计（API Key 策略）](#安全设计api-key-策略)
- [IPC 契约](#ipc-契约)
- [Live2D 模型说明](#live2d-模型说明)
- [目录结构](#目录结构)
- [架构与数据流](#架构与数据流)
- [故障排查与 FAQ](#故障排查与-faq)
- [相关文档](#相关文档)
- [许可](#许可)

---

## 功能特性

- **AI 聊天 + 流式输出**：OpenAI 兼容协议（`chat/completions` 的 SSE 流式），对接 **DeepSeek / Moonshot / Ollama / 通义 / 本地 LLM** 等均可；逐字渲染，支持**停止 / 重新生成 / 清空**。
- **AI 控制 Live2D 动作**：AI 回复中的 `[emotion: xxx]` / `[action: xxx]` 标签驱动虚拟形象的表情与动作；标签会在展示时自动剥离，不混入气泡文本。内置可爱猫娘 **Mao** 官方示例模型。
- **自定义模型与表情**：可导入本地 Live2D 模型文件夹；通过「表情名关键词 + 官方标准参数评分」**双层推理**识别模型表情语义，无需手工配置序号；支持按语义手动覆盖表情映射。
- **自定义 API Key**：Base URL / API Key / 模型名在专用设置页配置，本地持久化；LLM / TTS / STT 三组配置相互独立。
- **上下文管理**：对话历史保留、token 估算、超限自动裁剪（滑动窗口，`system` 恒保留）。
- **机器人人格**：内置「元气 / 冷静 / 毒舌」预设，可新增、编辑、切换、删除；人格包含名称、头像、系统提示词、默认表情。
- **文转语音（TTS）**：系统语音（Web Speech API，免费）或 OpenAI 兼容 `/audio/speech`（如 `tts-1`）；支持单条朗读与「自动朗读回复」开关。
- **语音转文字（STT）**：系统语音识别（`SpeechRecognition`，免费）或 Whisper 兼容 `/audio/transcriptions` 云端转写；支持语音输入。
- **主题**：浅色 / 深色主题切换（CSS 变量 + naive-ui `darkTheme`，粉色主色调）。
- **Live2D 交互**：滚轮缩放、拖拽平移；点击形象可触发动作。

## 技术栈

| 层 | 技术 |
|---|---|
| 桌面壳 | Electron 39 + electron-vite 5 |
| 前端 | Vue 3 + TypeScript + Vite 7 |
| UI | naive-ui |
| 状态管理 | Pinia |
| 路由 | vue-router（hash 模式） |
| Live2D 渲染 | pixi.js 6 + pixi-live2d-display 0.4 + @pixi/unsafe-eval |

## 环境要求

- [Node.js](https://nodejs.org/) **20 及以上**（electron-vite 5 / Vite 7 需要）。
- npm（随 Node.js 提供）。

## 快速开始

```bash
cd Windows            # 若尚未进入该目录
npm install           # 安装依赖（postinstall 会执行 electron-builder install-app-deps）
npm run dev           # 开发模式（HMR 热更新）
```

其他常用脚本：

```bash
npm run typecheck     # 类型检查（node + web，含 vue-tsc 模板检查）
npm run lint          # ESLint
npm run format        # Prettier 格式化
npm run start         # 预览已构建产物（electron-vite preview）
```

## 构建与打包（macOS / Windows / Linux）

```bash
npm run build         # 先 typecheck 再用 electron-vite 构建到 out/
npm run build:unpack  # 构建 + 仅生成未打包目录（--dir，用于本地快速调试）
npm run build:mac     # macOS：产出 dist/*.dmg 与 dist/SuperMilkCard-*-mac.zip
npm run build:win     # Windows：产出 dist/*-setup.exe（NSIS 安装包）
npm run build:linux   # Linux：AppImage / snap / deb
```

- 应用名 / `appId` / 图标在 [`electron-builder.yml`](electron-builder.yml) 配置（图标位于 `build/`，为必提交资源）。
- `build:mac` 需在 macOS 上执行；`build:win` 推荐在 Windows 上执行（macOS 上部分步骤可能需要 wine）。
- 产物输出到 `dist/`；构建中间产物输出到 `out/`（两者均已被 `.gitignore` 忽略）。

## 使用说明

1. 首次运行进入「设置」页，填写 **LLM** 的 Base URL、API Key、模型名。
2. 在「人格」里选择/编辑机器人人设（内置三种，可自定义）。
3. 回到「聊天」页开始对话；AI 带 `[emotion: …]` 标签的回复会自动驱动 Live2D 表情与动作。
4. 可选：在「语音 / Live2D」里启用 TTS/STT、切换或导入模型、调整主题。

## 配置持久化

- 配置经主进程读/写 `userData` 目录下的 `settings.json`（Electron 的 `app.getPath('userData')`，位于仓库之外，不会进入 Git）。
- API Key 仅保存在本地 `userData`，**不暴露给渲染层**（详见下文安全设计）。

## 安全设计（API Key 策略）

> 设计目标：**API Key 永不进入渲染进程**，外网请求统一在主进程发起。

- 渲染层通过 `settings:get` / `settings:set` 只能拿到**脱敏后**的配置（`PublicLlmConfig` / `PublicTtsConfig` / `PublicSttConfig`），其中不含 `apiKey` 字段，仅含布尔 `hasApiKey`。
- 密钥的写入口是**独立通道**：
  - `settings:set-key(section, apiKey)`：`apiKey` 非空 → 覆盖为新密钥并持久化。
  - `settings:clear-key(section)`：置空并持久化。
- `settings:set` 的补丁中若含 `apiKey / hasApiKey`，会被主进程 `normalizePatch` 丢弃（渲染层发送前也经 `sanitizePatch` 清理），避免污染 `settings.json`，也避免“传空即清空”的误操作。
- 发起 LLM / TTS / STT 请求时，主进程自行读取 `getSettings()` 拼装 `Authorization`，渲染层只传业务数据（`chatId + messages` / `text` / `audioBase64 + mimeType`）。
- 云 TTS / 云 STT 在自身 `baseUrl / apiKey` 为空时，自动回退到 LLM 的配置。
- 设置页密钥输入框不落库、不回显真实值，仅显示「已配置 / 未配置」。

## IPC 契约

渲染层只通过 `window.api.*`（contextBridge）与主进程通信，全部走 IPC。

> 流式聊天采用「主进程 → 渲染层」事件推送（`broadcast`），其余为请求/响应（`invoke`）。

### `window.api.llm`

| 方法 / 事件 | 通道 | 方向 | 说明 |
|---|---|---|---|
| `chatStream(req)` | `llm:chat-stream` | renderer→main | 发起流式对话，`req = { chatId, messages }` |
| `abort(chatId)` | `llm:abort` | renderer→main | 中断/取消当前流式对话 |
| `onChunk(cb)` | `llm:chunk` | main→renderer | 逐 token 片段 |
| `onDone(cb)` | `llm:done` | main→renderer | 正常完成（含完整文本） |
| `onError(cb)` | `llm:error` | main→renderer | 出错（含 message） |
| `onAborted(cb)` | `llm:aborted` | main→renderer | 被中断 |

### `window.api.settings`

| 方法 | 通道 | 说明 |
|---|---|---|
| `get()` | `settings:get` | 返回脱敏后的 `PublicAppSettings` |
| `set(partial)` | `settings:set` | 合并保存非敏感配置，返回最新脱敏配置 |
| `reset()` | `settings:reset` | 恢复默认配置，返回最新脱敏配置 |
| `setKey(section, apiKey)` | `settings:set-key` | 写入/覆盖某部分的 API Key |
| `clearKey(section)` | `settings:clear-key` | 清除某部分的 API Key |

### `window.api.tts` / `window.api.stt` / `window.api.live2d`

| 方法 | 通道 | 说明 |
|---|---|---|
| `tts.synthesize({ text })` | `tts:synthesize` | 云 TTS 合成，返回音频 base64 |
| `stt.transcribe({ audioBase64, mimeType })` | `stt:transcribe` | 云 STT 转写，返回文本 |
| `live2d.listModels()` | `live2d:list` | 列出可用模型（内置 + 已导入） |
| `live2d.importModel(sourcePath)` | `live2d:import` | 导入本地模型文件夹 |
| `live2d.getPathForFile(file)` | — | 由 `webUtils.getPathForFile` 解析拖拽文件路径 |

## Live2D 模型说明

- 内置示例模型 **Mao**（Live2D Cubism 官方示例猫娘，位于 `src/renderer/public/live2d/mao/`）与 Cubism 4 核心运行时 `live2dcubismcore.min.js`。
- 模型通过自定义 `live2d://` 协议加载，由主进程在**用户导入目录 → 内置目录**中依次查找，规避打包后 `file://` 的 XHR 限制。
- 资源头包含 `Access-Control-Allow-Origin: *`，按扩展名返回对应 `Content-Type`。
- 在「设置 → Live2D 形象」中可切换模型、拖拽导入自己的模型文件夹，并按语义覆盖表情映射。

## 目录结构

```
src/
├─ main/                    # 主进程
│  ├─ index.ts              # 窗口创建 + 注册 live2d:// 协议
│  ├─ ipc.ts                # IPC 通道注册（settings / llm / tts / stt / live2d）
│  └─ services/
│     ├─ llm.ts             # OpenAI 兼容流式请求（SSE，逐 token 推送）
│     ├─ tts.ts             # 云 TTS 合成（/audio/speech）
│     ├─ stt.ts             # 云 STT 转写（/audio/transcriptions）
│     ├─ settings.ts        # settings.json 读写（深合并 + 缓存，含密钥写入）
│     └─ live2dModels.ts    # 模型扫描 / 导入 / live2d:// 资源定位
├─ preload/                 # contextBridge 暴露 window.api
│  ├─ index.ts
│  └─ index.d.ts            # window.api / window.electron 类型声明
├─ shared/                  # 主 / 渲染共享
│  ├─ types.ts              # 类型、DEFAULT_SETTINGS、脱敏结构、RendererApi
│  ├─ context.ts            # token 估算 + 滑动窗口上下文裁剪
│  └─ emotion.ts            # [emotion:…]/[action:…] 标签解析与剥离
└─ renderer/
   ├─ index.html            # CSP 收紧、加载 Cubism Core、标题
   └─ src/
      ├─ views/             # ChatView / SettingsView
      ├─ components/        # live2d/Live2DStage 等
      ├─ stores/            # chat / settings / live2d / audio（Pinia）
      ├─ services/          # live2dModel（表情语义推理）、speech（TTS/STT）
      ├─ router/            # hash 路由
      └─ public/live2d/     # Live2D 模型资源（内置 Mao + Cubism Core）
```

## 架构与数据流

```
渲染进程 (Vue + Pinia)
   │  window.api.*（contextBridge）
   ▼
预加载 (preload)  ← 仅做 IPC 转发、封装，不持有业务逻辑
   │  ipcRenderer / ipcMain
   ▼
主进程 (main)
   ├─ llm.ts       → 外部 LLM（SSE 流式，拼装 Authorization）
   ├─ tts.ts       → 外部 /audio/speech
   ├─ stt.ts       → 外部 /audio/transcriptions
   └─ settings.ts  → userData/settings.json（密钥仅存于此）
```

- 渲染进程受 CSP 限制，**不直接外网请求**；所有外网调用统一放入主进程，通过 IPC 与渲染进程通信。
- **流式聊天链路**：渲染层发 `llm:chat-stream` → 主进程逐 token 推送 `llm:chunk` → 结束推 `llm:done`（携带完整文本）→ 出错推 `llm:error` → 用户取消推 `llm:aborted`。
- **Live2D 资源链路**：渲染层通过 `live2d://<相对路径>` 加载 → `protocol.handle('live2d')` 依次到「用户导入目录 / 内置目录」读取 → 返回带正确 `Content-Type` 的资源。
- **表情驱动链路**：`llm:chunk` 累积文本 → 情绪解析剥离 `[emotion:…]` / `[action:…]` → 推送给 Live2D 舞台 → 通过表情语义双层推理驱动表情/动作。

## 故障排查与 FAQ

- **聊天不回复 / 报「请先填写 API Key」**：到「设置 → LLM」确认 Base URL、API Key、模型名已填写，且服务可访问。
- **点「说话」没有声音**：检查 TTS 配置；若用云端 TTS 需配置 Base URL / API Key，否则回退用系统语音。
- **语音输入无效**：确认浏览器/系统允许麦克风权限；云端 STT 需配置 base URL 与 key。
- **Live2D 模型不显示**：确认「设置 → Live2D 形象」已选中模型；导入的模型文件夹需包含 `*.model3.json`。
- **想换回默认配置**：在「设置」页使用「重置为默认」。
- **改动未生效**：配置保存于 `userData/settings.json`，必要时可在设置页手动清除或重置。

## 相关文档

- 产品与技术开发计划：[`docs/PLAN.md`](docs/PLAN.md)

## 许可

Live2D 示例模型与 Cubism 核心遵循 Live2D 官方许可，使用自有模型时请自行确认商用合规性。
