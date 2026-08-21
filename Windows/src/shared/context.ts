import type { ChatMessage, Role } from './types'

/** 粗略估算 token 数：中文≈1 token/字，其余≈4 字符/token。 */
export function estimateTokens(text: string): number {
  if (!text) return 0
  const cjk = (text.match(/[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]/g) ?? []).length
  const rest = text.length - cjk
  return Math.max(1, Math.ceil(cjk + rest / 4))
}

export interface TrimmedResult {
  messages: Array<{ role: Role; content: string }>
  dropped: number
}

/**
 * 按 token 预算裁剪上下文：system 恒保留，其余从最旧开始丢弃，
 * 保留最新、最重要的对话。返回可直接发给模型的 messages 与丢弃条数。
 */
export function trimToContext(messages: ChatMessage[], maxTokens: number): TrimmedResult {
  const system = messages.filter((m) => m.role === 'system')
  const rest = messages.filter((m) => m.role !== 'system')
  const systemTokens = system.reduce((acc, m) => acc + estimateTokens(m.content), 0)
  let budget = Math.max(0, maxTokens - systemTokens)
  const kept: ChatMessage[] = []
  let dropped = 0

  for (let i = rest.length - 1; i >= 0; i--) {
    const msg = rest[i]
    const t = estimateTokens(msg.content)
    if (t <= budget) {
      kept.unshift(msg)
      budget -= t
    } else {
      dropped++
    }
  }

  return {
    messages: [...system, ...kept].map((m) => ({ role: m.role, content: m.content })),
    dropped
  }
}
