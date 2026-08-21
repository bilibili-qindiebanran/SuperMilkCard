<script setup lang="ts">
import { computed, nextTick, onMounted, ref, watch } from 'vue'
import type { ChatMessage } from '@shared/types'
import { extractEmotionTags } from '@shared/emotion'
import { useChatStore } from '../stores/chat'
import { useSettingsStore } from '../stores/settings'
import { useAudioStore } from '../stores/audio'
import Live2DStage from '../components/live2d/Live2DStage.vue'

const chat = useChatStore()
const settings = useSettingsStore()
const audio = useAudioStore()
const draft = ref('')
const listEl = ref<HTMLElement | null>(null)

const messages = computed(() => chat.history)
const canRegenerate = computed(() => !chat.streaming && chat.history.some((m) => m.role === 'user'))

function cleanContent(msg: ChatMessage): string {
  if (msg.role !== 'assistant') return msg.content
  return extractEmotionTags(msg.content).text
}

function scrollToBottom(): void {
  void nextTick(() => {
    const el = listEl.value
    if (el) el.scrollTop = el.scrollHeight
  })
}

function send(): void {
  const value = draft.value
  if (!value.trim() || chat.streaming) return
  chat.send(value)
  draft.value = ''
  scrollToBottom()
}

function onKeydown(e: KeyboardEvent): void {
  if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
    e.preventDefault()
    send()
  }
}

function formatTime(ts: number): string {
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d
    .getMinutes()
    .toString()
    .padStart(2, '0')}`
}

function speak(msg: ChatMessage): void {
  void audio.speak(cleanContent(msg))
}

function onSttFinal(text: string): void {
  draft.value = text
}

function toggleListening(): void {
  if (audio.listening) void audio.stopListening()
  else void audio.startListening(onSttFinal)
}

watch(
  () => chat.history.length,
  () => scrollToBottom()
)
watch(
  () => chat.history[chat.history.length - 1]?.content.length ?? 0,
  () => scrollToBottom()
)
watch(
  () => chat.streaming,
  (now, prev) => {
    if (prev && !now && settings.tts.autoSpeak) {
      const last = chat.history[chat.history.length - 1]
      if (last && last.role === 'assistant' && last.content.trim()) {
        void audio.speak(cleanContent(last))
      }
    }
  }
)

onMounted(scrollToBottom)
</script>

<template>
  <div class="chat-view">
    <section class="stage-panel">
      <div class="stage-card">
        <Live2DStage />
        <div class="stage-name">{{ settings.persona.name }}</div>
        <div class="stage-hint">AI 会根据情绪控制形象动作</div>
      </div>
    </section>

    <section class="chat-panel">
      <div ref="listEl" class="chat-list">
        <div v-if="messages.length === 0" class="chat-empty">
          <div class="chat-empty-avatar">{{ settings.persona.avatar }}</div>
          <p>和 {{ settings.persona.name }} 打个招呼吧～</p>
          <p class="chat-empty-sub">回复将以流式逐字显示，还能控制 Live2D 做动作</p>
        </div>

        <div v-for="msg in messages" :key="msg.id" class="msg-row" :class="msg.role">
          <div class="bubble">
            <div class="bubble-text">{{ cleanContent(msg) }}</div>
            <div
              v-if="msg.role === 'assistant' && msg.content === '' && chat.streaming"
              class="typing"
            >
              <span></span><span></span><span></span>
            </div>
            <div class="bubble-foot">
              <button
                v-if="msg.role === 'assistant' && msg.content"
                class="bubble-speak"
                :title="audio.speaking ? '朗读中…' : '朗读'"
                @click="speak(msg)"
              >
                {{ audio.speaking ? '🔊' : '🔉' }}
              </button>
              <span class="bubble-time">{{ formatTime(msg.createdAt) }}</span>
            </div>
          </div>
        </div>
      </div>

      <div v-if="chat.error || audio.error" class="chat-error">
        <span class="chat-error-icon">⚠️</span>
        <span>{{ chat.error || audio.error }}</span>
      </div>

      <div v-if="audio.listening" class="listening-bar">
        <span class="pulse"></span>
        <span>聆听中… {{ audio.transcript }}</span>
      </div>

      <div class="chat-toolbar">
        <span class="tokens">{{ chat.tokenCount }} tokens · 上下文 {{ settings.llm.maxContextTokens }}</span>
        <button class="tool-btn" :disabled="!canRegenerate" @click="chat.regenerate()">
          重新生成
        </button>
        <button class="tool-btn" :disabled="messages.length === 0" @click="chat.clear()">清空</button>
      </div>

      <div class="chat-input">
        <button
          class="mic-btn"
          :class="{ active: audio.listening }"
          :title="audio.listening ? '停止' : '语音输入'"
          @click="toggleListening()"
        >
          🎤
        </button>
        <textarea
          v-model="draft"
          class="chat-textarea"
          rows="1"
          :placeholder="audio.listening ? '正在聆听…' : '输入消息，Enter 发送，Shift+Enter 换行'"
          @keydown="onKeydown"
        ></textarea>
        <button v-if="!chat.streaming" class="send-btn" :disabled="!draft.trim()" @click="send()">
          发送
        </button>
        <button v-else class="stop-btn" @click="chat.stop()">停止</button>
      </div>
    </section>
  </div>
</template>
