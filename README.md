# SuperMilkCard 🥛

一款「有灵魂的」AI 聊天机器人项目，核心是一个带 **Live2D 虚拟形象** 的桌面客户端：用户与一个有性格的 AI 对话，AI 的回复不仅能**流式输出**，还能通过约定的情绪/动作标签**实时驱动虚拟形象的表情与动作**，并支持**语音朗读（TTS）** 与 **语音输入（STT）**。

## 项目结构

```
SuperMilkCard/
├─ Windows/            # Electron 桌面客户端（已实现）
│  ├─ src/             # Electron + Vue 3 + TypeScript
│  └─ docs/PLAN.md     # 产品/技术开发计划文档
├─ ESP32/              # 嵌入式（规划占位，暂只有 .gitkeep）
├─ .gitignore
└─ README.md
```

> **Windows/** 是本项目当前完整可运行的成果，详细说明见 [Windows/README.md](Windows/README.md)。
> **ESP32/** 为后续规划（如桌面端的小硬件伴侣），当前未实现。

## 项目定位

- **Windows/**：基于 Electron 的跨平台 AI 聊天桌面应用 —— Live2D 形象 + 流式对话 + 语音交互。
- 兼容 OpenAI 协议的服务：DeepSeek / Moonshot / Ollama / 通义 / 本地 LLM 等均可接入。

## 快速开始

进入 `Windows/` 目录：

```bash
npm install
npm run dev        # 开发模式（热更新）
```

完整的功能、配置、打包说明请见 [Windows/README.md](Windows/README.md)。

## 相关文档

- [Windows 端访问说明](Windows/README.md)
- [产品与技术开发计划](Windows/docs/PLAN.md)

## 许可

Live2D 示例模型与 Cubism 核心遵循 Live2D 官方许可，使用自有模型时请自行确认商用合规性（详见 `Windows/README.md`）。
