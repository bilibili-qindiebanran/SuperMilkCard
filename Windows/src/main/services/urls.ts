// OpenAI 兼容端点 URL 规范化 + 安全校验（STT/TTS/LLM 共享）
//
// 安全约束：仅允许 http/https；拒绝 localhost、环回、私有和保留地址。
// 兼容三种配置形态：
//   https://host/v1              → https://host/v1/audio/transcriptions
//   https://host/v1/chat/completions（完整端点）→ 原样（仅 chat）
//   https://host               → https://host/audio/transcriptions

/** 校验并规范化 Base URL：仅 http/https，拒绝内网/环回/保留地址。 */
export function validateBaseUrl(baseUrl: string): string {
  const trimmed = baseUrl.trim().replace(/\/+$/, '')
  if (!trimmed) throw new Error('请先填写 Base URL')

  let url: URL
  try {
    url = new URL(trimmed)
  } catch {
    throw new Error('Base URL 格式无效')
  }

  if (url.protocol !== 'http:' && url.protocol !== 'https:') {
    throw new Error('仅支持 http/https')
  }

  const host = url.hostname.toLowerCase()
  // 拒绝 localhost / 环回 / 私有 / 保留地址
  if (
    host === 'localhost' ||
    host.endsWith('.localhost') ||
    host === '0.0.0.0' ||
    /^127\./.test(host) ||
    /^10\./.test(host) ||
    /^192\.168\./.test(host) ||
    /^169\.254\./.test(host) ||
    /^172\.(1[6-9]|2\d|3[01])\./.test(host) ||
    host === '[::1]' ||
    host.includes('::ffff:')
  ) {
    throw new Error('不允许连接本机或内网地址')
  }

  return url.toString().replace(/\/+$/, '')
}

/** 从 Base URL 推导某类端点：transcriptions / speech / chat/completions */
export function resolveEndpoint(baseUrl: string, kind: 'transcriptions' | 'speech' | 'chat'): string {
  const root = validateBaseUrl(baseUrl)
  if (kind === 'chat') {
    if (/\/chat\/completions$/i.test(root)) return root
    return `${root}/chat/completions`
  }
  return `${root}/${kind === 'transcriptions' ? 'audio/transcriptions' : 'audio/speech'}`
}
