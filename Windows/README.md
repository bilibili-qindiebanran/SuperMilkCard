# SuperMilkCard（Windows 桌面端）

AI 聊天机器人桌面应用：带 **Live2D 虚拟形象**、**流式对话**、**语音交互**。

技术栈：**Electron + Vue 3 + TypeScript**，采用 electron-vite 工程，UI 使用 naive-ui，状态管理使用 Pinia，Live2D 渲染基于 pixi.js + pixi-live2d-display。

## 功能

- **AI 聊天 + 流式输出**：OpenAI 兼容协议（`chat/completions` 的 SSE 流式），对接 DeepSeek / Moonshot / Ollama / 通义 / 本地 LLM 等均可；逐字渲染，可**停止 / 重新生成 / 清空**。
- **AI 控制 Live2D 动作**：AI 回复中的 `[emotion: xxx]` / `[action: xxx]` 标签驱动虚拟形象的表情与动作；标签自动剥离，不出现在气泡文本中。内置可爱猫娘 **Mao** 官方示例模型。
- **自定义模型与表情**：可导入本地 Live2D 模型文件夹；通过「表情名关键词 + 官方标准参数评分」**双层推理**识别模型表情语义，无需手工配置序号；支持按语义手动覆盖表情映射。
- **自定义 API Key**：Base URL / API Key / 模型名在专用设置页配置，本地持久化；LLM / TTS / STT 三组配置相互独立。
- **上下文管理**：对话历史保留、token 估算、超限自动裁剪（滑动窗口，system 恒保留）。
- **机器人人格**：内置「元气 / 冷静 / 毒舌」预设，可新增、编辑、切换、删除；人格包含名称、头像、系统提示词、默认表情。
- **文转语音（TTS）**：系统语音（Web Speech API，免费）或 OpenAI 兼容 `/audio/speech`（如 `tts-1`）；支持单条朗读与「自动朗读回复」开关。
- **语音转文字（STT）**：系统语音识别（`SpeechRecognition`，免费）或 Whisper 兼容 `/audio/transcriptions` 云端转写；支持语音输入。
- **主题**：浅色 / 深色主题切换（CSS 变量 + naive-ui darkTheme，粉色主色调）。
- **Live2D 交互**：滚轮缩放、拖拽平移；点击形象可触发动作。

## 环境要求

- [Node.js](https://nodejs.org/) **20 及以上**（electron-vite 5 / Vite 7 需要）。
- npm（随 Node.js 提供）。

## 开发

```bash
npm install        # 安装依赖
npm run dev        # 开发模式（热更新）
npm run typecheck  # 类型检查（node + web）
npm run lint       # ESLint
npm run format     # Prettier 格式化
```

## 构建与打包（macOS / Windows / Linux）

```bash
npm run build        # 先 typecheck 再用 electron-vite 构建到 out/
npm run build:mac    # macOS：产出 dist/*.dmg 与 dist/SuperMilkCard-*-mac.zip
npm run build:win    # Windows：产出 dist/*-setup.exe（NSIS 安装包）
npm run build:linux  # Linux：AppImage / snap / deb
```

- 应用名 / `appId` / 图标在 [`electron-builder.yml`](electron-builder.yml) 中配置（图标位于 `build/`）。
- `build:mac` 需在 macOS 上执行；`build:win` 推荐在 Windows 上执行（macOS 上部分步骤可能需要 wine）。

## 使用说明

1. 首次运行进入「设置」页，填写 **LLM** 的 Base URL、API Key、模型名。
2. 在「人格」里选择/编辑机器人人设（内置三种，可自定义）。
3. 回到「聊天」页开始对话；AI 带 `[emotion: …]` 标签的回复会自动驱动 Live2D 表情与动作。
4. 可选：在「语音 / Live2D」里启用 TTS/STT、切换或导入模型、调整主题。

### 配置持久化

配置通过主进程读取/写入 `userData` 目录下的 `settings.json`；API Key 仅保存在本地 `userData`，不暴露给渲染层（渲染层仅持有配置读写通道）。

## Live2D 模型说明

- 内置示例模型 **Mao**（Live2D Cubism 官方示例猫娘，位于 `src/renderer/public/live2d/mao/`）与 Cubism 4 核心运行时 `live2dcubismcore.min.js`。
- 模型通过自定义 `live2d://` 协议加载，由主进程在**用户导入目录 → 内置目录**中依次查找，规避打包后 `file://` 的 XHR 限制。
- 在「设置 → Live2D 形象」中可切换模型、拖拽导入自己的模型文件夹，并按语义覆盖表情映射。
- 模型与核心遵循 Live2D 官方许可，请自行确认商用合规性。

## 目录结构

```
src/
├─ main/        # 主进程：LLM 流式、TTS/STT、settings、live2d:// 协议与模型管理
│  ├─ index.ts        # 窗口创建 + 注册 live2d:// 协议
│  ├─ ipc.ts          # IPC 通道注册（settings / llm / tts / stt / live2d）
│  └─ services/
│     ├─ llm.ts           # OpenAI 兼容流式请求（SSE）
│     ├─ tts.ts           # 云 TTS 合成（/audio/speech）
│     ├─ stt.ts           # 云 STT 转写（/audio/transcriptions）
│     ├─ settings.ts      # settings.json 读写（深合并 + 缓存）
│     └─ live2dModels.ts  # 模型扫描 / 导入（内置 + 用户目录）
├─ preload/     # 预加载：通过 contextBridge 暴露 window.api
├─ shared/      # 共享类型 / token 估算与上下文裁剪 / 情绪标签解析
└─ renderer/    # Vue 渲染进程：聊天页、设置页、Live2D 舞台、语音
   └─ src/
      ├─ views/          # ChatView / SettingsView
      ├─ components/     # Live2DStage 等
      ├─ stores/         # chat / settings / live2d / audio（Pinia）
      ├─ services/       # live2dModel（表情语义推理）、speech（TTS/STT）
      ├─ router/         # hash 路由
      └─ public/live2d/  # Live2D 模型资源
```

## 架构与数据流

```
渲染进程 (Vue + Pinia)
   │  window.api.*（contextBridge）
   ▼
预加载 (preload)
   │  ipcRenderer / ipcMain
   ▼
主进程 (main)
   ├─ llm.ts   → 外部 LLM（SSE 流式）
   ├─ tts.ts   → 外部 /audio/speech
   ├─ stt.ts   → 外部 /audio/transcriptions
   └─ settings.ts → userData/settings.json
```

- 渲染进程 CSP 限制不允许直接外网请求，所有外网调用统一放入主进程，通过 IPC 与渲染进程通信。
- 流式聊天：渲染进程发 `llm:chat-stream` → 主进程逐 token 推送 `llm:chunk`，结束推 `llm:done`；错误推 `llm:error`，中断推 `llm:aborted`。

## 相关文档

- 产品与技术开发计划：[`docs/PLAN.md`](docs/PLAN.md)
