<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useLive2dStore } from '../../stores/live2d'
import { useSettingsStore } from '../../stores/settings'

const live2d = useLive2dStore()
const settings = useSettingsStore()
const wrapRef = ref<HTMLElement | null>(null)
const canvasRef = ref<HTMLCanvasElement | null>(null)
let observer: ResizeObserver | null = null
let modelStarted = false
const mounted = ref(false)

// 拖拽平移状态
let dragging = false
let lastX = 0
let lastY = 0

onMounted(() => {
  const wrap = wrapRef.value
  const canvas = canvasRef.value
  if (!wrap || !canvas) return

  // 挂载控制器（本身不依赖配置），尺寸适配交给 ResizeObserver 与设置加载完成后的 resize
  live2d.mount(canvas, wrap.clientWidth, wrap.clientHeight)
  mounted.value = true

  observer = new ResizeObserver(() => {
    live2d.resize(wrap.clientWidth, wrap.clientHeight)
  })
  observer.observe(wrap)
})

// 等「设置加载完成」且「舞台已挂载」后再加载模型：
// - 首次启动 settings 加载较慢，loaded 变 true 时 mount 已完成
// - 路由切回时 mounted 由 false→true 重新触发（settings.loaded 已是 true），
//   避免 watch 的 immediate 在 onMounted 前误触发导致 controller 为空而无法加载
watch(
  [() => settings.loaded, mounted],
  async ([loaded, isMounted]) => {
    if (!loaded || !isMounted || modelStarted) return
    modelStarted = true
    if (settings.live2d.enabled) {
      await live2d.loadModel()
    }
    // 设置加载完成后重新适配一次舞台尺寸
    if (wrapRef.value) live2d.resize(wrapRef.value.clientWidth, wrapRef.value.clientHeight)
  },
  { immediate: true }
)

function getLocalPoint(e: MouseEvent | WheelEvent): { x: number; y: number } {
  const wrap = wrapRef.value
  if (!wrap) return { x: 0, y: 0 }
  const rect = wrap.getBoundingClientRect()
  return { x: e.clientX - rect.left, y: e.clientY - rect.top }
}

function onWheel(e: WheelEvent): void {
  // 仅当模型加载完成才可缩放
  if (live2d.status !== 'ready') return
  e.preventDefault()
  const { x, y } = getLocalPoint(e)
  const factor = e.deltaY < 0 ? 1.08 : 1 / 1.08
  live2d.zoom(factor, x, y)
}

function onPointerDown(e: PointerEvent): void {
  if (live2d.status !== 'ready') return
  dragging = true
  lastX = e.clientX
  lastY = e.clientY
  // 捕获指针，让拖拽在移出舞台后仍能继续
  ;(e.target as HTMLElement)?.setPointerCapture?.(e.pointerId)
}

function onPointerMove(e: PointerEvent): void {
  if (!dragging) return
  const dx = e.clientX - lastX
  const dy = e.clientY - lastY
  if (dx === 0 && dy === 0) return
  lastX = e.clientX
  lastY = e.clientY
  live2d.pan(dx, dy)
}

function onPointerUp(e: PointerEvent): void {
  if (!dragging) return
  dragging = false
  ;(e.target as HTMLElement)?.releasePointerCapture?.(e.pointerId)
}

onBeforeUnmount(() => {
  observer?.disconnect()
  observer = null
  dragging = false
  live2d.destroy()
})
</script>

<template>
  <div
    ref="wrapRef"
    class="live2d-stage"
    @wheel="onWheel"
    @pointerdown="onPointerDown"
    @pointermove="onPointerMove"
    @pointerup="onPointerUp"
    @pointercancel="onPointerUp"
    @pointerleave="onPointerUp"
  >
    <canvas ref="canvasRef" class="live2d-canvas"></canvas>
    <div v-if="live2d.status === 'loading'" class="live2d-overlay">
      <span class="stage-avatar">🥛</span>
      <span class="live2d-note">形象加载中…</span>
    </div>
    <div v-else-if="live2d.status === 'error'" class="live2d-overlay">
      <span class="stage-avatar">🥛</span>
      <span class="live2d-note">Live2D 加载失败</span>
      <span class="live2d-error">{{ live2d.error }}</span>
    </div>
    <div v-else-if="!settings.live2d.enabled" class="live2d-overlay">
      <span class="stage-avatar">🥛</span>
    </div>
    <div class="live2d-hint">滚轮缩放 · 拖拽移动</div>
  </div>
</template>
