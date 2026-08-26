import type { TtsSynthesizeRequest, TtsSynthesizeResult } from '@shared/types'
import { getSettings } from './settings'
import { resolveEndpoint } from './urls'
import { synthesizeWavBase64 } from './systemSpeech'

/** 文本转语音（TTS）。engine=system 走 Windows 本地语音合成（离线）。 */
export async function synthesize(req: TtsSynthesizeRequest): Promise<TtsSynthesizeResult> {
  const s = getSettings()
  const cfg = s.tts

  if (cfg.engine === 'system') {
    // 系统 TTS：合成 WAV（PCM），供 ESP32 播放
    const audioBase64 = await synthesizeWavBase64(req.text, cfg.voice, cfg.speed)
    return { audioBase64 }
  }

  const baseUrl = cfg.baseUrl || s.llm.baseUrl
  const apiKey = cfg.apiKey || s.llm.apiKey
  const { model, voice, speed } = cfg
  if (!apiKey) throw new Error('请先在设置中填写 API Key')
  if (!baseUrl) throw new Error('请先在设置中填写 Base URL')

  const url = resolveEndpoint(baseUrl, 'speech')
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
