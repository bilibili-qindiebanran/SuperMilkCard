import { EventEmitter } from 'events'
import si from 'systeminformation'
import WebSocket from 'ws'
import type { PerfSample } from '@shared/types'
import { getSettings } from './settings'
import { resolveTarget } from './esp32'

const emitter = new EventEmitter()

let timer: NodeJS.Timeout | null = null
let latest: PerfSample | null = null

let ws: WebSocket | null = null
let wsTimer: NodeJS.Timeout | null = null

function resolveWsUrl(): string | null {
  const target = resolveTarget()
  if (!target.host) return null
  const port = target.wsPort || getSettings().esp32.wsPort
  return `ws://${target.host}:${port}`
}

function connectWs(): void {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return
  const url = resolveWsUrl()
  if (!url) return
  try {
    ws = new WebSocket(url)
  } catch {
    ws = null
    return
  }
  ws.on('open', () => {
    /* 连接成功，等待采样推送 */
  })
  ws.on('error', () => {
    /* 由 close 统一触发重连 */
  })
  ws.on('close', () => {
    ws = null
    scheduleWsReconnect()
  })
}

function scheduleWsReconnect(): void {
  if (wsTimer) return
  if (!getSettings().perf.enabled) return
  wsTimer = setTimeout(() => {
    wsTimer = null
    connectWs()
  }, 5000)
}

function closeWs(): void {
  if (wsTimer) {
    clearTimeout(wsTimer)
    wsTimer = null
  }
  if (ws) {
    ws.removeAllListeners()
    try {
      ws.close()
    } catch {
      /* ignore */
    }
    ws = null
  }
}

async function sample(): Promise<PerfSample> {
  const metrics = getSettings().perf.metrics ?? []
  const needCpu = metrics.includes('cpu')
  const needMemory = metrics.includes('memory')
  const needGpu = metrics.includes('gpu')

  const [cpuLoad, mem] = await Promise.all([
    needCpu ? si.currentLoad().catch(() => null) : Promise.resolve(null),
    needMemory ? si.mem().catch(() => null) : Promise.resolve(null)
  ])

  const result: PerfSample = { ts: Date.now() }
  if (needCpu && cpuLoad) result.cpu = Math.round(cpuLoad.currentLoad * 10) / 10
  if (needMemory && mem) {
    result.memory = Math.round(((mem.total - mem.available) / mem.total) * 1000) / 10
  }

  if (needGpu) {
    const gpu = await si.graphics().catch(() => null)
    const ctrl = gpu?.controllers?.[0] as
      | { memoryTotal?: number; memoryUsed?: number }
      | undefined
    if (ctrl && ctrl.memoryTotal && ctrl.memoryUsed != null) {
      result.gpu = Math.round((ctrl.memoryUsed / ctrl.memoryTotal) * 1000) / 10
    } else {
      result.gpu = null
    }
  }

  return result
}

async function tick(): Promise<void> {
  if (!getSettings().perf.enabled) return
  try {
    latest = await sample()
    emitter.emit('sample', latest)
  } catch {
    /* 忽略单次采样异常 */
  }
  schedule()
}

function schedule(): void {
  if (timer) clearTimeout(timer)
  if (!getSettings().perf.enabled) return
  const interval = Math.max(200, getSettings().perf.intervalMs || 1000)
  timer = setTimeout(() => {
    void tick()
  }, interval)
}

function pushSample(s: PerfSample): void {
  const cfg = getSettings().perf
  if (!cfg.pushOverWs) return
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    connectWs()
    return
  }
  ws.send(JSON.stringify({ type: 'perf', ...s }))
}

function start(): void {
  stop()
  connectWs()
  void tick()
}

function stop(): void {
  if (timer) {
    clearTimeout(timer)
    timer = null
  }
  closeWs()
}

function getLatest(): PerfSample | null {
  return latest
}

emitter.on('sample', pushSample)

export { emitter as perfEmitter, start, stop, getLatest }
