# SuperMilkCard 🥛

一款「有灵魂的」AI 聊天机器人项目。核心是一个带 **Live2D 虚拟形象** 的桌面客户端：用户与一个有性格的 AI 对话，AI 的回复不仅能**流式输出**，还能通过约定的情绪/动作标签**实时驱动虚拟形象的表情与动作**，并支持**语音朗读（TTS）** 与 **语音输入（STT）**。

---

## 目录

- [项目定位](#项目定位)
- [功能特性](#功能特性)
- [项目结构](#项目结构)
- [架构概览](#架构概览)
- [快速开始](#快速开始)
- [可用脚本](#可用脚本)
- [构建产物](#构建产物)
- [安全说明](#安全说明)
- [相关文档](#相关文档)
- [许可](#许可)

---

## 项目定位

- **Windows/**：基于 Electron 的跨平台 AI 聊天桌面应用 —— Live2D 形象 + 流式对话 + 语音交互，当前**已完整实现**。
- **ESP32/**：嵌入式规划占位（如桌面端的小硬件伴侣），**当前未实现**，仅保留占位目录。

对接的 LLM 服务遵循 **OpenAI 兼容协议**（`/v1/chat/completions` 的 SSE 流式），因此可接入 DeepSeek / Moonshot / Ollama / 通义 / 本地 LLM 等。

## 功能特性

- **流式对话**：逐字渲染、可停止 / 重新生成 / 清空。
- **Live2D 形象**：`[emotion: …]` / `[action: …]` 标签驱动表情与动作，标签自动剥离；内置猫娘示例模型，支持导入自定义模型。
- **表情语义推理**：关键词 + 官方标准参数评分的双层推理，无需手工配置序号。
- **自定义 API Key**：Base URL / API Key / 模型名在设置页配置，本地持久化；LLM / TTS / STT 三组独立。
- **上下文管理**：token 估算 + 超限滑动窗口裁剪（system 恒保留）。
- **机器人人格**：内置元气 / 冷静 / 毒舌预设，可增改删。
- **语音交互**：系统语音与云端 TTS / STT 双通道（含回退策略）。
- **主题**：浅色 / 深色切换，粉色主色调。

> 更详尽的功能、配置、打包与安全说明见 [Windows/README.md](Windows/README.md)。

## 项目结构

```
SuperMilkCard/
├─ Windows/                         # Electron 桌面客户端（已实现）
│  ├─ src/
│  │  ├─ main/                      # 主进程：LLM 流式、TTS/STT、settings、live2d:// 协议与模型管理
│  │  ├─ preload/                   # contextBridge 暴露 window.api
│  │  ├─ shared/                    # 主 / 渲染共享：类型、上下文裁剪、情绪解析
│  │  └─ renderer/                  # Vue 渲染进程：聊天页、设置页、Live2D 舞台、语音
│  ├─ build/                        # 打包图标等资源（必提交）
│  ├─ docs/
│  │  └─ PLAN.md                    # 产品/技术开发计划
│  ├─ electron-builder.yml          # 打包配置
│  ├─ package.json
│  └─ README.md
├─ ESP32/                           # 嵌入式（规划占位，暂只有 .gitkeep）
├─ .gitignore
└─ README.md
```

## 架构概览

整体采用 Electron 的三进程模型，外网请求统一收敛到主进程：

```
渲染进程 (Vue 3 + Pinia)
   │  window.api.*（contextBridge）
   ▼
预加载 (preload)           ← 仅做 IPC 转发/封装
   │  ipcRenderer / ipcMain
   ▼
主进程 (main)
   ├─ llm.ts     → 外部 LLM（SSE 流式，拼装 Authorization）
   ├─ tts.ts     → 外部 /audio/speech
   ├─ stt.ts     → 外部 /audio/transcriptions
   └─ settings.ts → userData/settings.json（API Key 仅存于此）
```

要点：

- 渲染层受 CSP 限制，**不会直接外网请求**。
- **API Key 不下发渲染层**：渲染层只拿到脱敏后的配置（无 `apiKey`，仅 `hasApiKey` 布尔），密钥写入口走独立 `set-key` / `clear-key` 通道。
- 流式聊天通过主进程逐 token 推送事件（`llm:chunk / done / error / aborted`）。

## 快速开始

```bash
# 进入桌面客户端
cd Windows

npm install        # 安装依赖
npm run dev        # 开发模式（HMR 热更新）
```

首次运行在「设置」页填写 **LLM** 的 Base URL / API Key / 模型名，即可开始对话。完整使用与配置说明见 [Windows/README.md](Windows/README.md)。

## 可用脚本

以下均在 `Windows/` 目录下执行：

| 命令 | 说明 |
|---|---|
| `npm run dev` | 开发模式（热更新） |
| `npm run start` | 预览已构建产物 |
| `npm run typecheck` | 类型检查（node + web，含 vue-tsc 模板检查） |
| `npm run lint` | ESLint |
| `npm run format` | Prettier 格式化 |
| `npm run build` | 类型检查 + electron-vite 构建 |
| `npm run build:unpack` | 构建 + 仅生成未打包目录（--dir） |
| `npm run build:mac` | 打包 macOS（`.dmg` / zip，输出到 `Windows/dist/`） |
| `npm run build:win` | 打包 Windows（NSIS `setup.exe`） |
| `npm run build:linux` | 打包 Linux（AppImage / snap / deb） |

## 构建产物

- 构建中间产物输出到 `Windows/out/`；最终安装包输出到 `Windows/dist/`（如 `*.dmg`、`*-setup.exe`、`*.zip`、`mac-arm64/`、`win-unpacked/`）。
- 两者均已在 [`.gitignore`](.gitignore) 中忽略，不会进入 Git。

## 安全说明

- **API Key 仅存主进程** 的 `userData/settings.json`（位于仓库之外，不会进入 Git），不暴露给渲染层。
- 主/渲染层请求统一收敛到主进程，渲染层不直接外网请求。

## 相关文档

- [Windows 端访问说明](Windows/README.md)
- [产品与技术开发计划](Windows/docs/PLAN.md)

## 许可

Live2D 示例模型与 Cubism 核心遵循 Live2D 官方许可，使用自有模型时请自行确认商用合规性（详见 [Windows/README.md](Windows/README.md)）。
