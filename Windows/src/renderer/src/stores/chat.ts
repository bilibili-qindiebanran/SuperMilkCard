import { defineStore } from 'pinia'
import type { ChatMessage, LlmAborted, LlmChunk, LlmDone, LlmError } from '@shared/types'
import { estimateTokens, trimToContext } from '@shared/context'
import { extractEmotionTags, stripEmotionInstruction } from '@shared/emotion'
import { useSettingsStore } from './settings'
import { useLive2dStore } from './live2d'

let seq = 0
function uid(): string {
  seq += 1
  return `${Date.now().toString(36)}-${seq.toString(36)}`
}

export const useChatStore = defineStore('chat', {
  state: () => ({
    history: [] as ChatMessage[],
    streaming: false,
    streamingChatId: '',
    streamingMessageId: '',
    error: '',
    lastEmotion: '',
    triggeredActions: [] as string[]
  }),
  getters: {
    tokenCount(state): number {
      return state.history.reduce((acc, m) => acc + estimateTokens(m.content), 0)
    }
  },
  actions: {
    setupListeners(): void {
      window.api.llm.onChunk((e: LlmChunk) => this.handleChunk(e))
      window.api.llm.onDone((e: LlmDone) => this.handleDone(e))
      window.api.llm.onError((e: LlmError) => this.handleError(e))
      window.api.llm.onAborted((e: LlmAborted) => this.handleAborted(e))
    },

    send(text: string): void {
      const content = text.trim()
      if (!content || this.streaming) return
      this.history.push({ id: uid(), role: 'user', content, createdAt: Date.now() })
      this.runRequest()
    },

    regenerate(): void {
      if (this.streaming) return
      let lastUserIdx = -1
      for (let i = this.history.length - 1; i >= 0; i--) {
        if (this.history[i].role === 'user') {
          lastUserIdx = i
          break
        }
      }
      if (lastUserIdx < 0) return
      this.history = this.history.slice(0, lastUserIdx + 1)
      this.runRequest()
    },

    stop(): void {
      if (!this.streaming) return
      window.api.llm.abort(this.streamingChatId)
      this.finishStream()
    },

    clear(): void {
      if (this.streaming) this.stop()
      this.history = []
      this.error = ''
    },

    runRequest(): void {
      const settings = useSettingsStore()
      const live2d = useLive2dStore()
      const assistant: ChatMessage = {
        id: uid(),
        role: 'assistant',
        content: '',
        createdAt: Date.now()
      }
      this.history.push(assistant)

      // 系统提示词 = 人设（剥离其中手工写死的情绪指令句） + 按模型自动生成的情绪标签指令
      const personaSystem = stripEmotionInstruction(settings.persona.systemPrompt)
      const emotionInjection = settings.live2d.enabled ? live2d.emotionInstruction : ''
      const system = {
        role: 'system' as const,
        content: [personaSystem, emotionInjection].filter(Boolean).join('\n')
      }
      const { messages } = trimToContext(this.history, settings.llm.maxContextTokens)

      this.streaming = true
      this.streamingChatId = uid()
      this.streamingMessageId = assistant.id
      this.error = ''
      this.lastEmotion = ''
      this.triggeredActions = []

      window.api.llm.chatStream({
        chatId: this.streamingChatId,
        messages: [system, ...messages]
      })
    },

    handleChunk(e: LlmChunk): void {
      if (e.chatId !== this.streamingChatId) return
      const msg = this.history.find((m) => m.id === this.streamingMessageId)
      if (msg) {
        msg.content += e.delta
        this.dispatchEmotion(msg.content)
      }
    },

    /** 从流式内容中解析情绪/动作标签并驱动 Live2D */
    dispatchEmotion(content: string): void {
      const { emotion, actions } = extractEmotionTags(content)
      const live2d = useLive2dStore()
      if (emotion && emotion !== this.lastEmotion) {
        this.lastEmotion = emotion
        void live2d.triggerEmotion(emotion)
      }
      for (const action of actions) {
        if (!this.triggeredActions.includes(action)) {
          this.triggeredActions.push(action)
          void live2d.triggerAction(action)
        }
      }
    },

    handleDone(e: LlmDone): void {
      if (e.chatId !== this.streamingChatId) return
      this.finishStream()
    },

    handleError(e: LlmError): void {
      if (e.chatId !== this.streamingChatId) return
      this.error = e.message
      const idx = this.history.findIndex((m) => m.id === this.streamingMessageId)
      if (idx >= 0 && this.history[idx].content === '') {
        this.history.splice(idx, 1)
      }
      this.finishStream()
    },

    handleAborted(e: LlmAborted): void {
      if (e.chatId !== this.streamingChatId) return
      this.finishStream()
    },

    finishStream(): void {
      this.streaming = false
      this.streamingChatId = ''
      this.streamingMessageId = ''
    }
  }
})
