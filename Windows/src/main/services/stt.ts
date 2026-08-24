import type { SttTranscribeRequest, SttTranscribeResult } from '@shared/types'
import { getSettings } from './settings'
import { resolveEndpoint } from './urls'

/** 调用 OpenAI 兼容的 /audio/transcriptions（Whisper），返回文本。 */
export async function transcribe(req: SttTranscribeRequest): Promise<SttTranscribeResult> {
  const s = getSettings()
  const cfg = s.stt
  const baseUrl = cfg.baseUrl || s.llm.baseUrl
  const apiKey = cfg.apiKey || s.llm.apiKey
  const { model, language } = cfg
  if (!apiKey) throw new Error('请先在设置中填写 API Key')
  if (!baseUrl) throw new Error('请先在设置中填写 Base URL')

  const url = resolveEndpoint(baseUrl, 'transcriptions')
  const audioBuffer = Buffer.from(req.audioBase64, 'base64')
  const mimeType = req.mimeType || 'audio/webm'

  const form = new FormData()
  form.append('file', new Blob([audioBuffer], { type: mimeType }), 'audio.webm')
  form.append('model', model)
  if (language) form.append('language', language)

  const res = await fetch(url, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${apiKey}`
    },
    body: form
  })

  if (!res.ok) {
    const text = await res.text().catch(() => '')
    throw new Error(`STT 请求失败 (HTTP ${res.status})${text ? `：${text.slice(0, 200)}` : ''}`)
  }

  const json = (await res.json()) as { text?: string }
  return { text: json.text ?? '' }
}
