// 主进程 / 预加载 / 渲染进程共享的类型定义。

export type Role = 'system' | 'user' | 'assistant'

export interface ChatMessage {
  id: string
  role: Role
  content: string
  /** 多模态图片附件（仅用户消息可携带） */
  images?: ImageAttachment[]
  createdAt: number
}

/** 用户发送的图片附件（以 data URL 形式随消息传递，供多模态 LLM 使用） */
export interface ImageAttachment {
  /** data URL，形如 data:image/<mime>;base64,... */
  dataUrl: string
  /** MIME 类型，如 image/png */
  mimeType: string
  /** 文件名（可选，仅用于展示） */
  name?: string
}

/** OpenAI 兼容的多模态消息内容片段 */
export type ContentPart =
  { type: 'text'; text: string } | { type: 'image_url'; image_url: { url: string } }

/** 把消息（文本 + 可选图片）转成 OpenAI 兼容的 content：无图时保持纯字符串，有图时用片段数组 */
export function buildMessageContent(msg: {
  content: string
  images?: ImageAttachment[]
}): string | ContentPart[] {
  if (!msg.images || msg.images.length === 0) return msg.content
  const parts: ContentPart[] = []
  if (msg.content) parts.push({ type: 'text', text: msg.content })
  for (const img of msg.images) parts.push({ type: 'image_url', image_url: { url: img.dataUrl } })
  return parts
}

/** LLM 相关配置 */
export interface LlmConfig {
  baseUrl: string
  apiKey: string
  model: string
  temperature: number
  /** 上下文窗口上限（估算 token 数） */
  maxContextTokens: number
  /** 单次回复最大 token */
  maxTokens: number
}

/** 机器人人格 */
export interface Persona {
  id: string
  name: string
  avatar: string
  /** 系统提示词，即人设 */
  systemPrompt: string
  /** 默认表情（Live2D expression id） */
  defaultExpression: string
}

/** TTS 文转语音配置 */
export interface TtsConfig {
  engine: 'system' | 'openai'
  /** 云端方案模型名，如 tts-1 */
  model: string
  voice: string
  speed: number
  /** 回复后自动朗读 */
  autoSpeak: boolean
  /** OpenAI 兼容 Base URL（为空时回退 LLM 的 baseUrl） */
  baseUrl: string
  /** OpenAI 兼容 API Key（为空时回退 LLM 的 apiKey） */
  apiKey: string
}

/** STT 语音转文字配置 */
export interface SttConfig {
  engine: 'system' | 'openai'
  /** 云端方案模型名，如 whisper-1 */
  model: string
  language: string
  /** OpenAI 兼容 Base URL（为空时回退 LLM 的 baseUrl） */
  baseUrl: string
  /** OpenAI 兼容 API Key（为空时回退 LLM 的 apiKey） */
  apiKey: string
}

/** ESP32 连接配置 */
export interface Esp32Config {
  /** 是否启用 ESP32 连接 */
  enabled: boolean
  /** 设备识别码（芯片 ID / MAC 地址），用于局域网发现与连接校验 */
  deviceId: string
  /** 手动配置的主机地址（设备未自动发现时使用） */
  host: string
  /** ESP32 TCP 服务器端口 */
  tcpPort: number
  /** ESP32 WebSocket 服务器端口（性能数据推送） */
  wsPort: number
  /** 重连基础间隔（毫秒），实际按退避递增、上限 30s */
  reconnectIntervalMs: number
}

/** 局域网内发现的 ESP32 设备 */
export interface Esp32Device {
  id: string
  name: string
  host: string
  tcpPort: number
  wsPort: number
  lastSeen: number
}

/** 性能监测项目 */
export type PerfMetric = 'cpu' | 'gpu' | 'memory'

/** 性能监测配置 */
export interface PerfConfig {
  enabled: boolean
  /** 采样间隔（毫秒） */
  intervalMs: number
  /** 选中的监测项目 */
  metrics: PerfMetric[]
  /** 是否通过 WebSocket 推送至 ESP32 */
  pushOverWs: boolean
}

/** ESP32 连接状态 */
export type Esp32ConnectionState = 'idle' | 'connecting' | 'connected' | 'reconnecting' | 'error'

export interface Esp32Status {
  connected: boolean
  state: Esp32ConnectionState
  message: string
}

export interface PerfSample {
  ts: number
  cpu?: number | null
  gpu?: number | null
  memory?: number | null
}

/** ESP32 → 软件 的文本消息 */
export interface Esp32TextMessage {
  content: string
}

/** ESP32 语音转写结果 */
export interface Esp32VoiceTextMessage {
  text: string
}

export interface Esp32SendResult {
  ok: boolean
  message?: string
}

/** AstrBot（AstrAlive 插件）连接配置 */
export interface AstrbotConfig {
  /** 是否把机器人核心处理交给 AstrBot */
  enabled: boolean
  /** AstrBot 插件所在主机 */
  host: string
  /** AstrBot 插件 WebSocket 端口 */
  port: number
  /** 会话 ID（留空由主进程自动生成并持久化） */
  sessionId: string
  /** AstrBot 中配置的人格 ID（可选，缺省用 AstrBot 默认人格） */
  personaId: string
}

/** AstrBot 连接状态 */
export type AstrbotConnectionState = 'idle' | 'connecting' | 'connected' | 'reconnecting' | 'error'

export interface AstrbotStatus {
  connected: boolean
  state: AstrbotConnectionState
  message: string
}

/** 渲染进程 → 主进程 的委托发送请求（sessionId 由主进程读取/生成，渲染层无需传） */
export interface AstrbotSendRequest {
  chatId: string
  content: string
  images?: ImageAttachment[]
  /** 追加到插件端 system_prompt 末尾的情感/动作标签指令（如 live2d.emotionInstruction） */
  systemPromptExtra?: string
}

export interface AstrbotSendResult {
  ok: boolean
  message?: string
}

export type ThemeMode = 'light' | 'dark'

/** 可经 IPC 下发到渲染层的密钥部分（用于设置 Key / 清除 Key） */
export type ApiKeySection = 'llm' | 'tts' | 'stt'

/** Live2D 虚拟形象配置 */
export interface Live2dConfig {
  enabled: boolean
  /** 模型地址（默认内置 Natori 示例模型，可改为本地/自定义 URL） */
  modelUrl: string
  /** 手动表情映射覆盖：key = 模型名（目录名），value = tierKey（如 happy / happy@2）→ 表达式 id */
  emotionOverrides: Record<string, Record<string, string>>
  /** 手动新增的表情档位：key = 模型名，value = tierKey 列表（如 ['happy@2']） */
  tierAdditions: Record<string, string[]>
  /** 手动隐藏的自动识别档位：key = 模型名，value = 被移除的 tierKey 列表 */
  tierRemovals: Record<string, string[]>
  /** LLM/AI 预填或人工修正后的「表达式 id → tierKey」：key = 模型名（目录名），value = 表达式 id → 语义@等级 */
  expressionEmotions: Record<string, Record<string, string>>
}

/** Live2D 可用模型信息 */
export interface Live2dModelInfo {
  id: string
  /** 模型名（目录名） */
  name: string
  /** 通过 live2d:// 协议加载的地址 */
  modelUrl: string
}

export interface AppSettings {
  llm: LlmConfig
  /** 人格预设列表（至少一项） */
  personas: Persona[]
  /** 当前启用的人格 id */
  activePersonaId: string
  tts: TtsConfig
  stt: SttConfig
  live2d: Live2dConfig
  esp32: Esp32Config
  perf: PerfConfig
  astrbot: AstrbotConfig
  theme: ThemeMode
}

/**
 * 渲染侧可安全持有的配置（脱敏：不含 apiKey，仅含是否已配置的状态）。
 * apiKey 只在主进程内部以 AppSettings 持有，永不通过 IPC 下发到渲染层。
 */
export interface PublicLlmConfig {
  baseUrl: string
  model: string
  temperature: number
  /** 上下文窗口上限（估算 token 数） */
  maxContextTokens: number
  /** 单次回复最大 token */
  maxTokens: number
  /** 是否已配置 API Key（仅供“已配置/未配置”展示，不回传真实密钥） */
  hasApiKey: boolean
  /** 脱敏后的密钥展示串（前4…后4）；未配置时为 '' */
  maskedKey: string
}

export interface PublicTtsConfig {
  engine: 'system' | 'openai'
  model: string
  voice: string
  speed: number
  autoSpeak: boolean
  baseUrl: string
  hasApiKey: boolean
  maskedKey: string
}

export interface PublicSttConfig {
  engine: 'system' | 'openai'
  model: string
  language: string
  baseUrl: string
  hasApiKey: boolean
  maskedKey: string
}

export interface PublicAppSettings {
  llm: PublicLlmConfig
  personas: Persona[]
  activePersonaId: string
  tts: PublicTtsConfig
  stt: PublicSttConfig
  live2d: Live2dConfig
  esp32: Esp32Config
  perf: PerfConfig
  astrbot: AstrbotConfig
  theme: ThemeMode
}

/** 渲染进程 → 主进程的设置补丁（不含 apiKey / hasApiKey，密钥修改走 set-key / clear-key） */
export interface SettingsPatch {
  llm?: Partial<Omit<LlmConfig, 'apiKey'>>
  tts?: Partial<Omit<TtsConfig, 'apiKey'>>
  stt?: Partial<Omit<SttConfig, 'apiKey'>>
  personas?: Persona[]
  activePersonaId?: string
  live2d?: Live2dConfig
  esp32?: Partial<Esp32Config>
  perf?: Partial<PerfConfig>
  astrbot?: Partial<AstrbotConfig>
  theme?: ThemeMode
}

/** 把主进程的完整配置转换为脱敏后的“渲染侧可安全持有”形态 */
export function toPublicSettings(s: AppSettings): PublicAppSettings {
  return {
    llm: {
      baseUrl: s.llm.baseUrl,
      model: s.llm.model,
      temperature: s.llm.temperature,
      maxContextTokens: s.llm.maxContextTokens,
      maxTokens: s.llm.maxTokens,
      hasApiKey: s.llm.apiKey.trim() !== '',
      maskedKey: maskKey(s.llm.apiKey)
    },
    personas: s.personas,
    activePersonaId: s.activePersonaId,
    tts: {
      engine: s.tts.engine,
      model: s.tts.model,
      voice: s.tts.voice,
      speed: s.tts.speed,
      autoSpeak: s.tts.autoSpeak,
      baseUrl: s.tts.baseUrl,
      hasApiKey: s.tts.apiKey.trim() !== '',
      maskedKey: maskKey(s.tts.apiKey)
    },
    stt: {
      engine: s.stt.engine,
      model: s.stt.model,
      language: s.stt.language,
      baseUrl: s.stt.baseUrl,
      hasApiKey: s.stt.apiKey.trim() !== '',
      maskedKey: maskKey(s.stt.apiKey)
    },
    live2d: s.live2d,
    esp32: s.esp32,
    perf: s.perf,
    astrbot: s.astrbot,
    theme: s.theme
  }
}

/** 对 API Key 脱敏：只暴露前4与后4；过短（≤8）则全遮蔽 */
export function maskKey(key: string): string {
  const v = (key || '').trim()
  if (!v) return ''
  if (v.length <= 8) return '*'.repeat(v.length)
  return `${v.slice(0, 4)}\u2026${v.slice(-4)}`
}

/** 单次流式对话请求（渲染进程 → 主进程，仅携带必要数据，配置由主进程读取） */
export interface ChatStreamRequest {
  chatId: string
  messages: Array<{ role: Role; content: string | ContentPart[] }>
}

/** 单个表达式的情绪标注输入（渲染进程 → 主进程，持久化/LLM 预填用） */
export interface EmotionClassifyItem {
  /** 表达式 id（如 exp_02） */
  id: string
  /** 表达式对外名称（通常同 id） */
  name: string
  /** 作者内置参数显示名（cdi3，如 目笑顔+目開閉），供 LLM 判断语义 */
  description: string
}

/** 流式事件负载 */
export interface LlmChunk {
  chatId: string
  delta: string
}

export interface LlmDone {
  chatId: string
  fullText: string
}

export interface LlmError {
  chatId: string
  message: string
}

export interface LlmAborted {
  chatId: string
}

export interface TtsSynthesizeRequest {
  text: string
}

export interface TtsSynthesizeResult {
  audioBase64: string
}

export interface SttTranscribeRequest {
  audioBase64: string
  mimeType: string
}

export interface SttTranscribeResult {
  text: string
}

/** 测试某部分 API Key 是否可用（渲染进程 → 主进程，密钥与 baseUrl 可回退到配置） */
export interface ApiKeyTestRequest {
  section: ApiKeySection
  /** 弹窗中用户新输入的密钥；为空时用主进程当前配置 */
  apiKey: string
  /** 弹窗中当前生效的 Base URL；为空时回退到主进程配置 */
  baseUrl: string
}

export interface ApiKeyTestResult {
  ok: boolean
  message: string
}

/** 预加载脚本暴露给渲染进程的 API 形状 */
export interface RendererApi {
  llm: {
    chatStream(req: ChatStreamRequest): void
    abort(chatId: string): void
    onChunk(cb: (e: LlmChunk) => void): () => void
    onDone(cb: (e: LlmDone) => void): () => void
    onError(cb: (e: LlmError) => void): () => void
    onAborted(cb: (e: LlmAborted) => void): () => void
    /** LLM 预填表情情绪标注：返回 表达式 id → tierKey（如 happy / happy@2） */
    classify(items: EmotionClassifyItem[]): Promise<Record<string, string>>
  }
  settings: {
    get(): Promise<PublicAppSettings>
    set(partial: SettingsPatch): Promise<PublicAppSettings>
    reset(): Promise<PublicAppSettings>
    /** 写入 / 覆盖某部分的 API Key（仅写入，不回读） */
    setKey(section: ApiKeySection, apiKey: string): Promise<PublicAppSettings>
    /** 清除某部分的 API Key */
    clearKey(section: ApiKeySection): Promise<PublicAppSettings>
    /** 测试 API Key 是否可用 */
    testKey(req: ApiKeyTestRequest): Promise<ApiKeyTestResult>
  }
  tts: {
    synthesize(req: TtsSynthesizeRequest): Promise<TtsSynthesizeResult>
  }
  stt: {
    transcribe(req: SttTranscribeRequest): Promise<SttTranscribeResult>
  }
  live2d: {
    listModels(): Promise<Live2dModelInfo[]>
    importModel(sourcePath: string): Promise<{ models: Live2dModelInfo[]; modelUrl: string }>
    /** 在 preload 中用 webUtils.getPathForFile 获取拖拽文件的绝对路径 */
    getPathForFile(file: File): string
  }
  esp32: {
    connect(): Promise<Esp32Status>
    disconnect(): Promise<Esp32Status>
    getStatus(): Promise<Esp32Status>
    /** 触发一次局域网设备发现 */
    discover(): Promise<Esp32Device[]>
    /** 获取已发现的设备列表 */
    listDevices(): Promise<Esp32Device[]>
    /** 发送聊天文本到 ESP32 */
    sendChat(role: 'user' | 'assistant', content: string): Promise<Esp32SendResult>
    /** 文本转语音后发送到 ESP32 */
    sendTts(text: string): Promise<Esp32SendResult>
    onStatus(cb: (s: Esp32Status) => void): () => void
    onDevices(cb: (d: Esp32Device[]) => void): () => void
    onText(cb: (m: Esp32TextMessage) => void): () => void
    onVoiceText(cb: (m: Esp32VoiceTextMessage) => void): () => void
    onError(cb: (m: { message: string }) => void): () => void
  }
  perf: {
    start(): Promise<void>
    stop(): Promise<void>
    getLatest(): Promise<PerfSample | null>
    onSample(cb: (s: PerfSample) => void): () => void
  }
  astrbot: {
    connect(): Promise<AstrbotStatus>
    disconnect(): Promise<AstrbotStatus>
    getStatus(): Promise<AstrbotStatus>
    sendMessage(req: AstrbotSendRequest): Promise<AstrbotSendResult>
    stop(): void
    onStatus(cb: (s: AstrbotStatus) => void): () => void
  }
}

export const DEFAULT_SETTINGS: AppSettings = {
  llm: {
    baseUrl: 'https://api.openai.com/v1',
    apiKey: '',
    model: 'gpt-4o-mini',
    temperature: 0.8,
    maxContextTokens: 8000,
    maxTokens: 1024
  },
  personas: [
    {
      id: 'genki',
      name: '小奶卡',
      avatar: '🥛',
      systemPrompt:
        '你是一个名叫"小奶卡"的元气虚拟助手。语气亲切、活泼、带一点俏皮，回复尽量简洁，多用表情和语气词。',
      defaultExpression: 'exp_04'
    },
    {
      id: 'calm',
      name: '冷静助手',
      avatar: '🧊',
      systemPrompt: '你是一个冷静、专业、理性的助手。回答条理清晰、客观准确，语气平和克制。',
      defaultExpression: 'exp_03'
    },
    {
      id: 'tsundere',
      name: '毒舌吐槽',
      avatar: '🌶️',
      systemPrompt:
        '你是一个毒舌又傲娇的吐槽角色。嘴上不饶人但内心关心对方，爱开玩笑和调侃，偶尔翻白眼。',
      defaultExpression: 'exp_03'
    }
  ],
  activePersonaId: 'genki',
  tts: {
    engine: 'system',
    model: 'tts-1',
    voice: '',
    speed: 1,
    autoSpeak: false,
    baseUrl: '',
    apiKey: ''
  },
  stt: {
    engine: 'system',
    model: 'whisper-1',
    language: 'zh-CN',
    baseUrl: '',
    apiKey: ''
  },
  live2d: {
    enabled: true,
    modelUrl: 'live2d://mao/Mao.model3.json',
    emotionOverrides: {},
    tierAdditions: {},
    tierRemovals: {},
    expressionEmotions: {}
  },
  esp32: {
    enabled: false,
    deviceId: '',
    host: '',
    tcpPort: 9000,
    wsPort: 9001,
    reconnectIntervalMs: 1000
  },
  perf: {
    enabled: false,
    intervalMs: 1000,
    metrics: ['cpu', 'memory'],
    pushOverWs: true
  },
  astrbot: {
    enabled: false,
    host: 'localhost',
    port: 6199,
    sessionId: '',
    personaId: ''
  },
  theme: 'light'
}
