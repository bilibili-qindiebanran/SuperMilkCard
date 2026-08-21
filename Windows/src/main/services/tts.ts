import type { TtsSynthesizeRequest, TtsSynthesizeResult } from '@shared/types'
import { getSettings } from './settings'

function trimSlash(url: string): string {
  return url.trim().replace(/\/+$/, '')
}

/** 调用 OpenAI 兼容的 /audio/speech，返回音频 base64。 */
export async function synthesize(req: TtsSynthesizeRequest): Promise<TtsSynthesizeResult> {
  const s = getSettings()
  const cfg = s.tts
  const baseUrl = cfg.baseUrl || s.llm.baseUrl
  const apiKey = cfg.apiKey || s.llm.apiKey
  const { model, voice, speed } = cfg
  if (!apiKey) throw new Error('请先在设置中填写 API Key')
  if (!baseUrl) throw new Error('请先在设置中填写 Base URL')

  const url = `${trimSlash(baseUrl)}/audio/speech`
  const res = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${apiKey}`
    },
    body: JSON.stringify({
      model,
      input: req.text,
      voice,
      speed,
      response_format: 'mp3'
    })
  })

  if (!res.ok) {
    const text = await res.text().catch(() => '')
    throw new Error(`TTS 请求失败 (HTTP ${res.status})${text ? `：${text.slice(0, 200)}` : ''}`)
  }

  const buf = Buffer.from(await res.arrayBuffer())
  return { audioBase64: buf.toString('base64') }
}
