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

type Recording = { stop: () => Promise<{ base64: string; mimeType: string }> }

let recognizer: { stop: () => void } | null = null
let interimRecognizer: { stop: () => void } | null = null
let recording: Recording | null = null
let recordingReady: Promise<Recording> | null = null
let stopRequested = false
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
    async startListening(onFinal: FinalCb, onInterim?: (text: string) => void): Promise<void> {
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

      // 通义云端识别最终结果；若 Electron 支持 Web Speech，则并行显示实时中间文本。
      // 中间文本只用于 UI 反馈，最终回填仍以 qwen 结果为准。
      if (settings.stt.model.includes('qwen-audio') && isSttSupported()) {
        interimRecognizer = createRecognizer(settings.stt, {
          onStart: () => console.info('[audio] interim recognizer started'),
          onResult: (text) => {
            this.transcript = text
            onInterim?.(text)
          },
          onFinal: () => undefined,
          onEnd: () => {
            interimRecognizer = null
          },
          onError: (message) => {
            console.warn('[audio] interim recognizer unavailable', message)
            interimRecognizer = null
          }
        })
      }
      // 云端 STT：录音后转写
      try {
        this.listening = true
        stopRequested = false
        console.info('[audio] start cloud recording')
        const pendingRecording = startRecording()
        recordingReady = pendingRecording
        recording = await pendingRecording
        recordingReady = null
        console.info('[audio] cloud recording ready')
        if (stopRequested) {
          stopRequested = false
          await this.stopListening()
        }
      } catch (e) {
        recordingReady = null
        recording = null
        this.listening = false
        pendingFinal = null
        this.error = e instanceof Error ? e.message : '无法访问麦克风'
        console.error('[audio] recording start failed', e)
      }
    },

    /** 结束语音输入并返回识别结果。 */
    async stopListening(): Promise<void> {
      if (recordingReady && !recording) {
        stopRequested = true
        console.info('[audio] stop requested while microphone is initializing')
        return
      }
      if (recognizer) {
        recognizer.stop()
        recognizer = null
        return
      }
      if (interimRecognizer) {
        interimRecognizer.stop()
        interimRecognizer = null
      }
      if (recording) {
        const rec = recording
        recording = null
        try {
          const { base64, mimeType } = await rec.stop()
          console.info('[audio] recording stopped', { mimeType, base64Length: base64.length })
          const { text } = await window.api.stt.transcribe({
            audioBase64: base64,
            mimeType
          })
          if (text.trim()) pendingFinal?.(text.trim())
        } catch (e) {
          this.error = e instanceof Error ? e.message : String(e)
          console.error('[audio] transcription failed', e)
        } finally {
          this.listening = false
          pendingFinal = null
        }
      }
    }
  }
})
