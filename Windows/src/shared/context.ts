import type { ChatMessage, ContentPart, ImageAttachment, Role } from './types'
import { buildMessageContent } from './types'

/** 粗略估算 token 数：中文≈1 token/字，其余≈4 字符/token。 */
export function estimateTokens(text: string): number {
  if (!text) return 0
  const cjk = (text.match(/[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]/g) ?? []).length
  const rest = text.length - cjk
  return Math.max(1, Math.ceil(cjk + rest / 4))
}

/** 每张图片在裁剪上下文时计入的粗略 token 成本（避免把超长 base64 当成文本累加） */
const IMAGE_TOKEN_COST = 800

/** 估算单条消息（文本 + 可选图片）的 token 数 */
export function estimateMessageTokens(m: { content: string; images?: ImageAttachment[] }): number {
  return estimateTokens(m.content) + (m.images?.length ?? 0) * IMAGE_TOKEN_COST
}

export interface TrimmedResult {
  messages: Array<{ role: Role; content: string | ContentPart[] }>
  dropped: number
}

/**
 * 按 token 预算裁剪上下文：system 恒保留，其余从最旧开始丢弃，
 * 保留最新、最重要的对话。返回可直接发给模型的 messages 与丢弃条数。
 */
export function trimToContext(messages: ChatMessage[], maxTokens: number): TrimmedResult {
  const system = messages.filter((m) => m.role === 'system')
  const rest = messages.filter((m) => m.role !== 'system')
  const systemTokens = system.reduce((acc, m) => acc + estimateMessageTokens(m), 0)
  let budget = Math.max(0, maxTokens - systemTokens)
  const kept: ChatMessage[] = []
  let dropped = 0

  for (let i = rest.length - 1; i >= 0; i--) {
    const msg = rest[i]
    const t = estimateMessageTokens(msg)
    if (t <= budget) {
      kept.unshift(msg)
      budget -= t
    } else {
      dropped++
    }
  }

  return {
    messages: [...system, ...kept].map((m) => ({ role: m.role, content: buildMessageContent(m) })),
    dropped
  }
}
