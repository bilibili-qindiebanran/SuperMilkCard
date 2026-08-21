// 主进程 / 预加载 / 渲染进程共享的类型定义。

export type Role = 'system' | 'user' | 'assistant'

export interface ChatMessage {
  id: string
  role: Role
  content: string
  createdAt: number
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

export type ThemeMode = 'light' | 'dark'

/** 可经 IPC 下发到渲染层的密钥部分（用于设置 Key / 清除 Key） */
export type ApiKeySection = 'llm' | 'tts' | 'stt'

/** Live2D 虚拟形象配置 */
export interface Live2dConfig {
  enabled: boolean
  /** 模型地址（默认内置 Natori 示例模型，可改为本地/自定义 URL） */
  modelUrl: string
  /** 手动表情映射覆盖：key = 模型名（目录名），value = 语义名 → 表达式 id */
  emotionOverrides: Record<string, Record<string, string>>
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
}

export interface PublicTtsConfig {
  engine: 'system' | 'openai'
  model: string
  voice: string
  speed: number
  autoSpeak: boolean
  baseUrl: string
  hasApiKey: boolean
}

export interface PublicSttConfig {
  engine: 'system' | 'openai'
  model: string
  language: string
  baseUrl: string
  hasApiKey: boolean
}

export interface PublicAppSettings {
  llm: PublicLlmConfig
  personas: Persona[]
  activePersonaId: string
  tts: PublicTtsConfig
  stt: PublicSttConfig
  live2d: Live2dConfig
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
      hasApiKey: s.llm.apiKey.trim() !== ''
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
      hasApiKey: s.tts.apiKey.trim() !== ''
    },
    stt: {
      engine: s.stt.engine,
      model: s.stt.model,
      language: s.stt.language,
      baseUrl: s.stt.baseUrl,
      hasApiKey: s.stt.apiKey.trim() !== ''
    },
    live2d: s.live2d,
    theme: s.theme
  }
}

/** 单次流式对话请求（渲染进程 → 主进程，仅携带必要数据，配置由主进程读取） */
export interface ChatStreamRequest {
  chatId: string
  messages: Array<{ role: Role; content: string }>
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

/** 预加载脚本暴露给渲染进程的 API 形状 */
export interface RendererApi {
  llm: {
    chatStream(req: ChatStreamRequest): void
    abort(chatId: string): void
    onChunk(cb: (e: LlmChunk) => void): () => void
    onDone(cb: (e: LlmDone) => void): () => void
    onError(cb: (e: LlmError) => void): () => void
    onAborted(cb: (e: LlmAborted) => void): () => void
  }
  settings: {
    get(): Promise<PublicAppSettings>
    set(partial: SettingsPatch): Promise<PublicAppSettings>
    reset(): Promise<PublicAppSettings>
    /** 写入 / 覆盖某部分的 API Key（仅写入，不回读） */
    setKey(section: ApiKeySection, apiKey: string): Promise<PublicAppSettings>
    /** 清除某部分的 API Key */
    clearKey(section: ApiKeySection): Promise<PublicAppSettings>
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
      systemPrompt: '你是一个名叫"小奶卡"的元气虚拟助手。语气亲切、活泼、带一点俏皮，回复尽量简洁，多用表情和语气词。',
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
      systemPrompt: '你是一个毒舌又傲娇的吐槽角色。嘴上不饶人但内心关心对方，爱开玩笑和调侃，偶尔翻白眼。',
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
    emotionOverrides: {}
  },
  theme: 'light'
}
