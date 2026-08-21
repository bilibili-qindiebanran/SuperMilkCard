import type { SttConfig, TtsConfig } from '@shared/types'

/* ---------------- 系统 TTS（Web Speech API） ---------------- */

export function speakSystem(text: string, cfg: Pick<TtsConfig, 'voice' | 'speed'>): void {
  const synth = window.speechSynthesis
  if (!synth) return
  synth.cancel()
  const u = new SpeechSynthesisUtterance(text)
  u.lang = 'zh-CN'
  u.rate = cfg.speed || 1
  const voices = synth.getVoices()
  if (cfg.voice && voices.length > 0) {
    const found = voices.find((v) => v.name === cfg.voice || v.lang === cfg.voice)
    if (found) u.voice = found
  }
  synth.speak(u)
}

export function stopSystemSpeech(): void {
  window.speechSynthesis?.cancel()
}

/* ---------------- 云 TTS / STT 的音频播放与录制 ---------------- */

export async function playAudioBase64(base64: string): Promise<void> {
  const blob = base64ToBlob(base64, 'audio/mpeg')
  const url = URL.createObjectURL(blob)
  const audio = new Audio(url)
  audio.onended = () => URL.revokeObjectURL(url)
  await audio.play()
}

function base64ToBlob(base64: string, mime: string): Blob {
  const binary = atob(base64)
  const bytes = new Uint8Array(binary.length)
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i)
  return new Blob([bytes], { type: mime })
}

function blobToBase64(blob: Blob): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onloadend = () => {
      const result = reader.result as string
      resolve(result.split(',')[1] ?? '')
    }
    reader.onerror = () => reject(reader.error)
    reader.readAsDataURL(blob)
  })
}

export interface Recording {
  stop: () => Promise<{ base64: string; mimeType: string }>
}

export async function startRecording(): Promise<Recording> {
  const stream = await navigator.mediaDevices.getUserMedia({ audio: true })
  const preferred = ['audio/webm;codecs=opus', 'audio/webm', 'audio/ogg;codecs=opus']
  const mimeType = preferred.find((m) => MediaRecorder.isTypeSupported(m)) ?? ''
  const rec = new MediaRecorder(stream, mimeType ? { mimeType } : undefined)
  const chunks: Blob[] = []

  rec.ondataavailable = (e: BlobEvent) => {
    if (e.data.size > 0) chunks.push(e.data)
  }

  rec.start()

  return {
    stop: () =>
      new Promise((resolve, reject) => {
        rec.onstop = () => {
          stream.getTracks().forEach((t) => t.stop())
          const blob = new Blob(chunks, { type: mimeType || 'audio/webm' })
          void blobToBase64(blob).then(
            (base64) => resolve({ base64, mimeType: mimeType || 'audio/webm' }),
            reject
          )
        }
        rec.onerror = () => reject(new Error('录音失败'))
        rec.stop()
      })
  }
}

/* ---------------- 系统 STT（Web Speech API） ---------------- */

export function isSttSupported(): boolean {
  return Boolean(window.SpeechRecognition ?? window.webkitSpeechRecognition)
}

export interface SttCallbacks {
  onStart: () => void
  onResult: (text: string) => void
  onFinal: (text: string) => void
  onEnd: () => void
  onError: (message: string) => void
}

export function createRecognizer(
  cfg: Pick<SttConfig, 'language'>,
  cbs: SttCallbacks
): { stop: () => void } | null {
  const Ctor = window.SpeechRecognition ?? window.webkitSpeechRecognition
  if (!Ctor) return null

  const rec = new Ctor()
  rec.lang = cfg.language || 'zh-CN'
  rec.continuous = true
  rec.interimResults = true

  let finalText = ''

  rec.onstart = () => cbs.onStart()
  rec.onresult = (e: SpeechRecognitionEvent) => {
    let interim = ''
    for (let i = e.resultIndex; i < e.results.length; i++) {
      const r = e.results[i]
      if (r.isFinal) finalText += r[0].transcript
      else interim += r[0].transcript
    }
    cbs.onResult(finalText + interim)
  }
  rec.onerror = () => cbs.onError('语音识别出错，请重试')
  rec.onend = () => {
    cbs.onFinal(finalText.trim())
    cbs.onEnd()
  }

  rec.start()
  return { stop: () => rec.stop() }
}
