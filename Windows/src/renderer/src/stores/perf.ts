import { defineStore } from 'pinia'
import type { PerfSample } from '@shared/types'

export const usePerfStore = defineStore('perf', {
  state: () => ({
    latest: null as PerfSample | null,
    running: false
  }),
  actions: {
    setupListeners(): void {
      window.api.perf.onSample((s) => {
        this.latest = s
      })
    },

    async start(): Promise<void> {
      await window.api.perf.start()
      this.running = true
      this.latest = await window.api.perf.getLatest()
    },

    async stop(): Promise<void> {
      await window.api.perf.stop()
      this.running = false
    }
  }
})
