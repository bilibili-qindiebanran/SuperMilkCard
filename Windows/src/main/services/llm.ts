import type { ChatStreamRequest, EmotionClassifyItem, LlmChunk } from '@shared/types'
import { getSettings } from './settings'

interface SseChunk {
  choices?: Array<{ delta?: Record<string, unknown> }>
}

/** 从 chunk 的 delta 中提取文本：兼容 content / reasoning_content / 内容为数组 三种形态 */
function extractDeltaText(delta: unknown): string {
  if (!delta || typeof delta !== 'object') return ''
  const d = delta as Record<string, unknown>
  const pick = (v: unknown): string => {
    if (typeof v === 'string') return v
    if (Array.isArray(v)) {
      return v
        .map((p) => (typeof p === 'string' ? p : ((p as { text?: string })?.text ?? '')))
        .join('')
    }
    return ''
  }
  return pick(d.content) || pick(d.reasoning_content) || ''
}

/**
 * 向 OpenAI 兼容端点发起流式对话请求。
 * 逐 token 通过 onChunk 回调推送，最终返回完整文本。
 */
export async function streamChat(
  req: ChatStreamRequest,
  onChunk: (chunk: LlmChunk) => void,
  signal?: AbortSignal
): Promise<string> {
  const { baseUrl, apiKey, model, temperature, maxTokens } = getSettings().llm
  if (!apiKey) throw new Error('请先在设置中填写 API Key')
  if (!baseUrl) throw new Error('请先在设置中填写 Base URL')
  if (!model) throw new Error('请先在设置中填写模型名')

  const url = normalizeBaseUrl(baseUrl)

  // 诊断：统计消息中的图片片段数（图片会导致 payload 变大、后端需要多模态处理）
  const imageParts = req.messages.reduce((acc, m) => {
    if (!Array.isArray(m.content)) return acc
    return acc + m.content.filter((p) => p.type === 'image_url').length
  }, 0)
  const body = JSON.stringify({
    model,
    messages: req.messages,
    temperature,
    max_tokens: maxTokens,
    stream: true
  })
  console.log(
    `[llm] streamChat start: url=${url} model=${model} messages=${req.messages.length} ` +
      `imageParts=${imageParts} bodyKB=${Math.round(Buffer.byteLength(body) / 1024)}`
  )

  const res = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${apiKey}`
    },
    body,
    signal
  })
  console.log(
    `[llm] streamChat fetch resolved: status=${res.status} ok=${res.ok} ` +
      `contentType=${res.headers.get('content-type')}`
  )

  if (!res.ok) {
    const text = await res.text().catch(() => '')
    console.log(`[llm] streamChat non-ok body: ${text.slice(0, 300)}`)
    throw new Error(`请求失败 (HTTP ${res.status})${text ? `：${text.slice(0, 300)}` : ''}`)
  }
  if (!res.body) throw new Error('响应没有内容流')

  const reader = res.body.getReader()
  const decoder = new TextDecoder('utf-8')
  let buffer = ''
  let full = ''
  let dataLines = 0
  let firstTokenAt: number | null = null
  let loggedDeltaKeys = false
  const t0 = Date.now()

  while (true) {
    const { done, value } = await reader.read()
    if (done) break
    buffer += decoder.decode(value, { stream: true })
    const lines = buffer.split('\n')
    buffer = lines.pop() ?? ''
    for (const raw of lines) {
      const line = raw.trim()
      if (!line.startsWith('data:')) continue
      const data = line.slice(5).trim()
      if (data === '[DONE]') continue
      dataLines += 1
      if (firstTokenAt === null) {
        firstTokenAt = Date.now() - t0
        console.log(`[llm] streamChat first SSE data after ${firstTokenAt}ms: ${data.slice(0, 160)}`)
      }
      if (dataLines <= 3) console.log(`[llm] streamChat SSE#${dataLines}: ${data.slice(0, 160)}`)
      let json: SseChunk
      try {
        json = JSON.parse(data) as SseChunk
      } catch {
        continue
      }
      const deltaObj = json.choices?.[0]?.delta
      if (deltaObj && !loggedDeltaKeys) {
        loggedDeltaKeys = true
        console.log(`[llm] streamChat delta keys: ${Object.keys(deltaObj).join(',')}`)
      }
      const delta = extractDeltaText(deltaObj)
      if (delta) {
        full += delta
        onChunk({ chatId: req.chatId, delta })
      }
    }
  }
  console.log(
    `[llm] streamChat done: elapsed=${Date.now() - t0}ms dataLines=${dataLines} ` +
      `chars=${full.length} firstTokenAt=${firstTokenAt ?? 'never'}`
  )
  return full
}

/** 可判定的情绪基类（与渲染层 ExpressionSemantic 一致）；强度以 @N 后缀表示 */
const TIER_RE = /^(neutral|happy|sad|angry|surprised|blush)(@[2-9])?$/

/** 从 LLM 回复文本中容错提取 JSON 数组（去代码块标记/前后赘述） */
function extractJsonArray(text: string): Array<{ id?: string; emotion?: string }> {
  const start = text.indexOf('[')
  const end = text.lastIndexOf(']')
  if (start === -1 || end === -1 || end <= start) throw new Error('LLM 未返回有效的 JSON 数组')
  const parsed = JSON.parse(text.slice(start, end + 1))
  if (!Array.isArray(parsed)) throw new Error('LLM 返回结果不是数组')
  return parsed as Array<{ id?: string; emotion?: string }>
}

/**
 * 用 LLM 一次性为多个表达式预填「情绪 + 强度等级」标注。
 * 输入每个表达式的 id/名称/作者参数描述，返回 表达式 id → tierKey（如 happy、happy@2）。
 * 供「用 AI 重新识别表情」使用；未配置 LLM 或请求失败时抛错，由调用方回退到本地规则。
 */
export async function classifyEmotions(
  items: EmotionClassifyItem[]
): Promise<Record<string, string>> {
  const { baseUrl, apiKey, model, maxTokens } = getSettings().llm
  if (!apiKey) throw new Error('请先在设置中填写 API Key')
  if (!baseUrl) throw new Error('请先在设置中填写 Base URL')
  if (!model) throw new Error('请先在设置中填写模型名')

  const lines = items
    .map((it) => {
      const name = it.name && it.name !== it.id ? `，名称: "${it.name}"` : ''
      return `- id: "${it.id}"${name}${it.description ? `，作者参数描述: ${it.description}` : ''}`
    })
    .join('\n')

  const userPrompt = [
    '把下列 Live2D 模型的每个表情归类为一种情绪，可带强度等级。',
    '情绪可选：neutral（正常/默认）、happy（开心）、sad（难过）、angry（生气）、surprised（惊讶）、blush（害羞/脸红）。',
    '强度用 @2（更）、@3（非常）、@4（超）表示；普通强度不带后缀（如 "happy"、"happy@2"）。',
    '只输出 JSON 数组，不要输出其它文字或代码块标记，格式：',
    '[{"id":"exp_02","emotion":"happy"},{"id":"exp_03","emotion":"sad"}]\n',
    '表情列表：\n',
    lines
  ].join('\n')

  const url = resolveEndpoint(baseUrl, 'chat')
  const res = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${apiKey}`
    },
    body: JSON.stringify({
      model,
      messages: [
        { role: 'system', content: '你是 Live2D 表情情绪标注助手，只输出 JSON 数组。' },
        { role: 'user', content: userPrompt }
      ],
      temperature: 0.2,
      max_tokens: Math.max(maxTokens, 2000),
      stream: false
    })
  })

  if (!res.ok) {
    const text = await res.text().catch(() => '')
    throw new Error(`请求失败 (HTTP ${res.status})${text ? `：${text.slice(0, 300)}` : ''}`)
  }
  const data = (await res.json()) as {
    choices?: Array<{ message?: { content?: string }; text?: string }>
  }
  const content = data.choices?.[0]?.message?.content ?? data.choices?.[0]?.text ?? ''

  const entries = extractJsonArray(content)
  const result: Record<string, string> = {}
  for (const e of entries) {
    const emo = e.emotion?.trim().toLowerCase()
    if (!e.id || !emo || !TIER_RE.test(emo)) continue
    result[e.id] = emo
  }
  return result
}
