<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import type { ChatMessage, ImageAttachment } from '@shared/types'
import { extractEmotionTags } from '@shared/emotion'
import { useChatStore } from '../stores/chat'
import { useSettingsStore } from '../stores/settings'
import { useAudioStore } from '../stores/audio'
import { useEsp32Store } from '../stores/esp32'
import Live2DStage from '../components/live2d/Live2DStage.vue'

const chat = useChatStore()
const settings = useSettingsStore()
const audio = useAudioStore()
const esp32 = useEsp32Store()
const draft = ref('')
const listEl = ref<HTMLElement | null>(null)
const attachments = ref<ImageAttachment[]>([])
const fileInput = ref<HTMLInputElement | null>(null)
const fileError = ref('')
const previewUrl = ref('')
const viewEl = ref<HTMLElement | null>(null)
const scale = ref(1)
const offsetX = ref(0)
const offsetY = ref(0)
const dragging = ref(false)
const MIN_SCALE = 1
const MAX_SCALE = 6
const MAX_IMAGE_SIZE = 10 * 1024 * 1024

/** 图片的画布变换（先缩放后平移，原点居中） */
const viewStyle = computed(() => ({
  transform: `translate(${offsetX.value}px, ${offsetY.value}px) scale(${scale.value})`
}))

const messages = computed(() => chat.history)
const canRegenerate = computed(() => !chat.streaming && chat.history.some((m) => m.role === 'user'))
const canSend = computed(
  () => !chat.streaming && (draft.value.trim() !== '' || attachments.value.length > 0)
)

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
  if ((!value.trim() && attachments.value.length === 0) || chat.streaming) return
  chat.send(value, attachments.value)
  draft.value = ''
  attachments.value = []
  fileError.value = ''
  scrollToBottom()
}

function onKeydown(e: KeyboardEvent): void {
  if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
    e.preventDefault()
    send()
  }
}

/** 读取文件为 data URL（用于展示预览与发送给多模态 LLM） */
function readAsDataUrl(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(reader.result as string)
    reader.onerror = () => reject(reader.error)
    reader.readAsDataURL(file)
  })
}

async function addFiles(files: File[]): Promise<void> {
  for (const file of files) {
    if (!file.type.startsWith('image/')) {
      fileError.value = `${file.name || '该文件'} 不是图片，已跳过`
      continue
    }
    if (file.size > MAX_IMAGE_SIZE) {
      fileError.value = `${file.name || '图片'} 超过 10MB，已跳过`
      continue
    }
    const dataUrl = await readAsDataUrl(file)
    attachments.value.push({ dataUrl, mimeType: file.type, name: file.name })
    fileError.value = ''
  }
}

function onFileChange(e: Event): void {
  const input = e.target as HTMLInputElement
  if (input.files?.length) void addFiles(Array.from(input.files))
  input.value = ''
}

function removeAttachment(index: number): void {
  attachments.value.splice(index, 1)
}

/** 把剪贴板里的图片直接粘贴为带图消息 */
function onPaste(e: ClipboardEvent): void {
  const items = e.clipboardData?.items
  if (!items) return
  const imgs: File[] = []
  for (const item of items) {
    if (item.kind === 'file' && item.type.startsWith('image/')) {
      const f = item.getAsFile()
      if (f) imgs.push(f)
    }
  }
  if (imgs.length) {
    e.preventDefault()
    void addFiles(imgs)
  }
}

/** 打开图片放大预览（重置缩放与平移） */
function openPreview(url: string): void {
  previewUrl.value = url
  scale.value = MIN_SCALE
  offsetX.value = 0
  offsetY.value = 0
  dragging.value = false
}

function closePreview(): void {
  previewUrl.value = ''
  dragging.value = false
}

/** 限制平移范围，避免图片被拖出视野 */
function clampOffset(): void {
  const el = viewEl.value
  if (!el) return
  const maxX = Math.max(0, (scale.value - 1) * (el.clientWidth / 2))
  const maxY = Math.max(0, (scale.value - 1) * (el.clientHeight / 2))
  offsetX.value = Math.min(maxX, Math.max(-maxX, offsetX.value))
  offsetY.value = Math.min(maxY, Math.max(-maxY, offsetY.value))
}

/** 鼠标滚轮：以光标为中心缩放 */
function onWheel(e: WheelEvent): void {
  e.preventDefault()
  const el = viewEl.value
  if (!el) return
  const rect = el.getBoundingClientRect()
  // 以视图中心为原点，计算光标相对位置
  const mx = e.clientX - rect.left - rect.width / 2
  const my = e.clientY - rect.top - rect.height / 2
  const factor = e.deltaY > 0 ? 0.9 : 1.1
  const next = Math.min(MAX_SCALE, Math.max(MIN_SCALE, scale.value * factor))
  // 保持光标下的图片点在缩放后仍位于光标下
  const worldX = (mx - offsetX.value) / scale.value
  const worldY = (my - offsetY.value) / scale.value
  scale.value = next
  offsetX.value = mx - worldX * next
  offsetY.value = my - worldY * next
  if (scale.value <= MIN_SCALE) {
    offsetX.value = 0
    offsetY.value = 0
  }
  clampOffset()
}

// 拖拽起点缓存（非响应式，避免频繁触发更新）
let dragStartX = 0
let dragStartY = 0
let dragStartOffsetX = 0
let dragStartOffsetY = 0

function onImageMouseDown(e: MouseEvent): void {
  if (scale.value <= MIN_SCALE) return
  dragging.value = true
  dragStartX = e.clientX
  dragStartY = e.clientY
  dragStartOffsetX = offsetX.value
  dragStartOffsetY = offsetY.value
  e.preventDefault()
}

function onWindowMouseMove(e: MouseEvent): void {
  if (!dragging.value) return
  offsetX.value = dragStartOffsetX + (e.clientX - dragStartX)
  offsetY.value = dragStartOffsetY + (e.clientY - dragStartY)
  clampOffset()
}

function onWindowMouseUp(): void {
  dragging.value = false
}

/** 复位缩放与平移 */
function resetView(): void {
  scale.value = MIN_SCALE
  offsetX.value = 0
  offsetY.value = 0
}

function onGlobalKeydown(e: KeyboardEvent): void {
  if (e.key === 'Escape' && previewUrl.value) closePreview()
}

function formatTime(ts: number): string {
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

function speak(msg: ChatMessage): void {
  const text = cleanContent(msg)
  if (esp32.connected) void esp32.sendTts(text)
  else void audio.speak(text)
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
        const text = cleanContent(last)
        if (esp32.connected) void esp32.sendTts(text)
        else void audio.speak(text)
      }
    }
  }
)

// ESP32 语音转写文本回填输入框
watch(
  () => esp32.voiceTextVersion,
  () => {
    if (esp32.voiceText) draft.value = esp32.voiceText
  }
)

onMounted(() => {
  scrollToBottom()
  window.addEventListener('keydown', onGlobalKeydown)
  window.addEventListener('mousemove', onWindowMouseMove)
  window.addEventListener('mouseup', onWindowMouseUp)
})

onUnmounted(() => {
  window.removeEventListener('keydown', onGlobalKeydown)
  window.removeEventListener('mousemove', onWindowMouseMove)
  window.removeEventListener('mouseup', onWindowMouseUp)
})
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
            <div v-if="msg.images?.length" class="bubble-images">
              <img
                v-for="(img, i) in msg.images"
                :key="i"
                :src="img.dataUrl"
                class="bubble-image"
                alt="图片"
                title="点击查看大图"
                @click="openPreview(img.dataUrl)"
              />
            </div>
            <div v-if="cleanContent(msg)" class="bubble-text">{{ cleanContent(msg) }}</div>
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
        <span class="tokens"
          >{{ chat.tokenCount }} tokens · 上下文 {{ settings.llm.maxContextTokens }}</span
        >
        <button class="tool-btn" :disabled="!canRegenerate" @click="chat.regenerate()">
          重新生成
        </button>
        <button class="tool-btn" :disabled="messages.length === 0" @click="chat.clear()">
          清空
        </button>
      </div>

      <div class="chat-input-area">
        <div v-if="attachments.length" class="attachment-bar">
          <div v-for="(img, i) in attachments" :key="i" class="attachment-item">
            <img :src="img.dataUrl" class="attachment-thumb" alt="预览" />
            <button class="attachment-remove" title="移除" @click="removeAttachment(i)">×</button>
          </div>
        </div>
        <div v-if="fileError" class="chat-file-error">{{ fileError }}</div>
        <div class="chat-input">
          <input
            ref="fileInput"
            type="file"
            accept="image/*"
            multiple
            class="file-input"
            @change="onFileChange"
          />
          <button class="img-btn" title="上传图片" @click="fileInput?.click()">🖼️</button>
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
            :placeholder="
              audio.listening
                ? '正在聆听…'
                : '输入消息，Enter 发送，Shift+Enter 换行，可直接粘贴图片'
            "
            @keydown="onKeydown"
            @paste="onPaste"
          ></textarea>
          <button v-if="!chat.streaming" class="send-btn" :disabled="!canSend" @click="send()">
            发送
          </button>
          <button v-else class="stop-btn" @click="chat.stop()">停止</button>
        </div>
      </div>
    </section>

    <Teleport to="body">
      <div
        v-if="previewUrl"
        ref="viewEl"
        class="image-viewer"
        :class="{ 'is-dragging': dragging, 'is-zoomed': scale > 1 }"
        @click.self="closePreview"
        @wheel="onWheel"
      >
        <img
          :src="previewUrl"
          class="image-viewer-img"
          :style="viewStyle"
          alt="放大预览"
          draggable="false"
          @mousedown="onImageMouseDown"
        />
        <button class="image-viewer-close" title="关闭" @click="closePreview">×</button>
        <button v-if="scale > 1" class="image-viewer-reset" title="复位" @click="resetView">
          复位
        </button>
        <div class="image-viewer-hint">滚轮缩放 · 拖拽移动</div>
      </div>
    </Teleport>
  </div>
</template>
