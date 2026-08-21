import { defineStore } from 'pinia'
import { useSettingsStore } from './settings'
import {
  createRecognizer,
  isSttSupported,
  playAudioBase64,
  speakSystem,
  startRecording
} from '../services/speech'

type FinalCb = (text: string) => void

let recognizer: { stop: () => void } | null = null
let recording: { stop: () => Promise<{ base64: string; mimeType: string }> } | null = null
let pendingFinal: FinalCb | null = null

export const useAudioStore = defineStore('audio', {
  state: () => ({
    speaking: false,
    listening: false,
    transcript: '',
    error: ''
  }),
  actions: {
    /** 朗读文本：系统语音或云端 TTS。 */
    async speak(text: string): Promise<void> {
      const settings = useSettingsStore()
      const clean = text.trim()
      if (!clean) return
      this.error = ''
      try {
        this.speaking = true
        if (settings.tts.engine === 'system') {
          speakSystem(clean, settings.tts)
        } else {
          const { audioBase64 } = await window.api.tts.synthesize({ text: clean })
          await playAudioBase64(audioBase64)
        }
      } catch (e) {
        this.error = e instanceof Error ? e.message : String(e)
      } finally {
        this.speaking = false
      }
    },

    /** 开始语音输入：系统识别或云端录音。 */
    async startListening(onFinal: FinalCb): Promise<void> {
      const settings = useSettingsStore()
      if (this.listening) return
      this.error = ''
      this.transcript = ''
      pendingFinal = onFinal

      if (settings.stt.engine === 'system' && isSttSupported()) {
        const rec = createRecognizer(settings.stt, {
          onStart: () => {
            this.listening = true
          },
          onResult: (t) => {
            this.transcript = t
          },
          onFinal: (t) => {
            if (t) pendingFinal?.(t)
          },
          onEnd: () => {
            this.listening = false
            recognizer = null
            pendingFinal = null
          },
          onError: (m) => {
            this.error = m
            this.listening = false
            pendingFinal = null
          }
        })
        if (rec) {
          recognizer = rec
          return
        }
      }

      // 云端 STT：录音后转写
      try {
        this.listening = true
        recording = await startRecording()
      } catch (e) {
        this.listening = false
        pendingFinal = null
        this.error = e instanceof Error ? e.message : '无法访问麦克风'
      }
    },

    /** 结束语音输入并返回识别结果。 */
    async stopListening(): Promise<void> {
      if (recognizer) {
        recognizer.stop()
        recognizer = null
        return
      }
      if (recording) {
        const rec = recording
        recording = null
        try {
          const { base64, mimeType } = await rec.stop()
          const { text } = await window.api.stt.transcribe({
            audioBase64: base64,
            mimeType
          })
          if (text.trim()) pendingFinal?.(text.trim())
        } catch (e) {
          this.error = e instanceof Error ? e.message : String(e)
        } finally {
          this.listening = false
          pendingFinal = null
        }
      }
    }
  }
})
