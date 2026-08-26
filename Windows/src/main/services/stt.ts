import type { SttTranscribeRequest, SttTranscribeResult } from '@shared/types'
import { getSettings } from './settings'
import { resolveEndpoint } from './urls'
import { recognizeWav, writeWavTemp } from './systemSpeech'
import { transcribePcm } from './dashscopeSpeech'
import { unlink } from 'fs/promises'

/** 从 WAV 提取 PCM 数据（跳过 RIFF 头）。 */
function wavToPcm(wav: Buffer): { pcm: Buffer; sampleRate: number } {
  // WAV 头 44 字节：sampleRate 在偏移 24
  const sampleRate = wav.readUInt32LE(24)
  let dataStart = 44
  // 处理扩展头（如 fmt 块长度 >16）
  const fmtSize = wav.readUInt32LE(16)
  dataStart = 44 + (fmtSize > 16 ? fmtSize - 16 : 0)
  // 找 data 块
  let offset = 12
  while (offset + 8 < wav.length) {
    const id = wav.toString('ascii', offset, offset + 4)
    const size = wav.readUInt32LE(offset + 4)
    if (id === 'data') {
      return { pcm: wav.subarray(offset + 8, offset + 8 + size), sampleRate }
    }
    offset += 8 + size
  }
  return { pcm: wav.subarray(dataStart), sampleRate }
}

/** 转写音频（STT）。engine=system 走本地；qwen-audio 走 DashScope WebSocket ASR。 */
export async function transcribe(req: SttTranscribeRequest): Promise<SttTranscribeResult> {
  const s = getSettings()
  const cfg = s.stt

  if (cfg.engine === 'system') {
    return transcribeSystem(req)
  }

  const baseUrl = cfg.baseUrl || s.llm.baseUrl
  const apiKey = cfg.apiKey || s.llm.apiKey
  const { model, language } = cfg
  if (!apiKey) throw new Error('请先在设置中填写 API Key')
  if (!baseUrl) throw new Error('请先在设置中填写 Base URL')

  // DashScope qwen-audio 系列：走 WebSocket 流式 ASR
  if (model.includes('qwen-audio')) {
    const audio = Buffer.from(req.audioBase64, 'base64')
    const mimeType = (req.mimeType || '').toLowerCase()
    // WAV → 提取 PCM（16k/16bit 单声道）；WebM/OPUS → 直接按 opus 上传（DashScope 支持）
    if (mimeType.includes('wav') || mimeType.includes('pcm')) {
      const { pcm } = wavToPcm(audio)
      const text = await transcribePcm(pcm, { apiKey, model, sampleRate: 16000, format: 'pcm' })
      return { text }
    }
    // WebM/OPUS：DashScope ASR 的 opus 格式接受原始字节流
    const text = await transcribePcm(audio, { apiKey, model, sampleRate: 16000, format: 'opus' })
    return { text }
  }

  const url = resolveEndpoint(baseUrl, 'transcriptions')
  console.log('[stt] POST', url, 'model=', model, 'bytes=', req.audioBase64.length)
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

/** Windows 系统离线识别（System.Speech）：ESP32 PCM→WAV 直接喂给本机识别引擎。 */
async function transcribeSystem(req: SttTranscribeRequest): Promise<SttTranscribeResult> {
  const wavPath = await writeWavTemp(req.audioBase64)
  try {
    const text = await recognizeWav(wavPath)
    return { text }
  } finally {
    await unlink(wavPath).catch(() => {})
  }
}
