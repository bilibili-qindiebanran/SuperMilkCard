import { defineStore } from 'pinia'
import type {
  Esp32Device,
  Esp32Live2dCommand,
  Esp32Live2dState,
  Esp32Status
} from '@shared/types'

export const useEsp32Store = defineStore('esp32', {
  state: () => ({
    status: { connected: false, state: 'idle', message: '' } as Esp32Status,
    devices: [] as Esp32Device[],
    lastText: '',
    voiceText: '',
    voiceTextVersion: 0,
    error: '',
    /** 最近一条来自 ESP32 的互动页命令（enter / return_home / reconnect） */
    lastLive2dCommand: null as Esp32Live2dCommand | null
  }),
  getters: {
    connected: (s): boolean => s.status.connected
  },
  actions: {
    setupListeners(): void {
      window.api.esp32.onStatus((s) => {
        this.status = s
      })
      window.api.esp32.onDevices((d) => {
        this.devices = d
      })
      window.api.esp32.onText((m) => {
        this.lastText = m.content
      })
      window.api.esp32.onVoiceText((m) => {
        this.voiceText = m.text
        this.voiceTextVersion += 1
      })
      window.api.esp32.onLive2dCommand((c) => {
        this.lastLive2dCommand = c
      })
      window.api.esp32.onError((m) => {
        this.error = m.message
      })
    },

    async refresh(): Promise<void> {
      this.status = await window.api.esp32.getStatus()
      this.devices = await window.api.esp32.listDevices()
    },

    async connect(): Promise<void> {
      this.status = await window.api.esp32.connect()
    },

    async disconnect(): Promise<void> {
      this.status = await window.api.esp32.disconnect()
    },

    async discover(): Promise<void> {
      this.devices = await window.api.esp32.discover()
    },

    async sendChat(role: 'user' | 'assistant', content: string): Promise<void> {
      if (!this.connected) return
      const res = await window.api.esp32.sendChat(role, content)
      if (!res.ok && res.message) this.error = res.message
    },

    async sendTts(text: string): Promise<void> {
      if (!this.connected) return
      const res = await window.api.esp32.sendTts(text)
      if (!res.ok && res.message) this.error = res.message
    },

    /** 发送 Live2D 表情/动作/摘要状态；未连接时静默失败 */
    async sendLive2dState(state: Esp32Live2dState): Promise<void> {
      if (!this.connected) return
      const res = await window.api.esp32.sendLive2dState(state)
      if (!res.ok && res.message) this.error = res.message
    }
  }
})
