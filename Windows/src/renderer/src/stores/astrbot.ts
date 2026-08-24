import { defineStore } from 'pinia'
import type { AstrbotSendRequest, AstrbotStatus } from '@shared/types'

export const useAstrbotStore = defineStore('astrbot', {
  state: () => ({
    status: { connected: false, state: 'idle', message: '' } as AstrbotStatus
  }),
  getters: {
    connected: (s): boolean => s.status.connected
  },
  actions: {
    setupListeners(): void {
      window.api.astrbot.onStatus((s) => {
        this.status = s
      })
    },

    async refresh(): Promise<void> {
      this.status = await window.api.astrbot.getStatus()
    },

    async connect(): Promise<void> {
      this.status = await window.api.astrbot.connect()
    },

    async disconnect(): Promise<void> {
      this.status = await window.api.astrbot.disconnect()
    },

    async sendMessage(req: AstrbotSendRequest): Promise<void> {
      if (!this.connected) {
        console.log('[astrbot-store] sendMessage skipped: not connected')
        return
      }
      const result = await window.api.astrbot.sendMessage(req)
      if (!result.ok) {
        console.log(`[astrbot-store] sendMessage failed: ${result.message ?? 'unknown'}`)
      }
    },

    stop(): void {
      window.api.astrbot.stop()
    }
  }
})
