import type { ChatStreamRequest, LlmChunk } from '@shared/types'
import { getSettings } from './settings'

interface SseChunk {
  choices?: Array<{ delta?: { content?: string } }>
}

function normalizeBaseUrl(baseUrl: string): string {
  const trimmed = baseUrl.trim().replace(/\/+$/, '')
  if (/\/chat\/completions$/i.test(trimmed)) return trimmed
  return `${trimmed}/chat/completions`
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
  const res = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${apiKey}`
    },
    body: JSON.stringify({
      model,
      messages: req.messages,
      temperature,
      max_tokens: maxTokens,
      stream: true
    }),
    signal
  })

  if (!res.ok) {
    const text = await res.text().catch(() => '')
    throw new Error(`请求失败 (HTTP ${res.status})${text ? `：${text.slice(0, 300)}` : ''}`)
  }
  if (!res.body) throw new Error('响应没有内容流')

  const reader = res.body.getReader()
  const decoder = new TextDecoder('utf-8')
  let buffer = ''
  let full = ''

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
      let json: SseChunk
      try {
        json = JSON.parse(data) as SseChunk
      } catch {
        continue
      }
      const delta = json.choices?.[0]?.delta?.content
      if (delta) {
        full += delta
        onChunk({ chatId: req.chatId, delta })
      }
    }
  }
  return full
}
