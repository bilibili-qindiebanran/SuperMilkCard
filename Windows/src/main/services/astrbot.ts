import { EventEmitter } from 'events'
import { randomUUID } from 'crypto'
import WebSocket from 'ws'
import type {
  AstrbotConfig,
  AstrbotConnectionState,
  AstrbotSendRequest,
  AstrbotSendResult,
  AstrbotStatus
} from '@shared/types'
import { getSettings, setSettings } from './settings'

const emitter = new EventEmitter()

let ws: WebSocket | null = null
let status: AstrbotStatus = { connected: false, state: 'idle', message: '' }
let reconnectTimer: NodeJS.Timeout | null = null
let reconnectAttempt = 0
let manuallyDisconnected = false

let activeChatId = ''
let fullText = ''

/* ---------------- 状态 ---------------- */

function setStatus(state: AstrbotConnectionState, message: string, connected: boolean): void {
  status = { connected, state, message }
  emitter.emit('status', status)
}

/* ---------------- 会话 ID ---------------- */

function ensureSessionId(): string {
  const cfg = getSettings().astrbot
  if (cfg.sessionId.trim()) return cfg.sessionId
  const sessionId = `smk-${randomUUID()}`
  setSettings({ astrbot: { ...cfg, sessionId } })
  return sessionId
}

/* ---------------- 连接 ---------------- */

function connect(): AstrbotStatus {
  manuallyDisconnected = false
  const cfg = getSettings().astrbot
  if (!cfg.enabled) {
    setStatus('idle', 'AstrBot 连接未启用', false)
    return status
  }
  if (!cfg.host) {
    setStatus('error', '未配置 AstrBot 主机地址', false)
    return status
  }
  openSocket(cfg)
  return status
}

function openSocket(cfg: AstrbotConfig): void {
  closeSocket()
  setStatus('connecting', `正在连接 ${cfg.host}:${cfg.port}`, false)

  let socket: WebSocket
  try {
    socket = new WebSocket(`ws://${cfg.host}:${cfg.port}`)
  } catch (err) {
    setStatus('error', err instanceof Error ? err.message : String(err), false)
    return
  }
  ws = socket

  socket.on('open', () => {
    reconnectAttempt = 0
    setStatus('connected', `已连接 ${cfg.host}:${cfg.port}`, true)
  })
  socket.on('message', (data) => {
    const text = (Array.isArray(data) ? Buffer.concat(data) : Buffer.from(data)).toString('utf-8')
    handleMessage(text)
  })
  socket.on('error', (err) => {
    setStatus('error', err.message, false)
  })
  socket.on('close', () => {
    if (ws === socket) ws = null
    if (!manuallyDisconnected && getSettings().astrbot.enabled) {
      scheduleReconnect()
    }
  })
}

function closeSocket(): void {
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

function scheduleReconnect(): void {
  if (reconnectTimer) return
  reconnectAttempt += 1
  const delay = Math.min(1000 * 2 ** (reconnectAttempt - 1), 30000)
  setStatus('reconnecting', `连接断开，${delay}ms 后重连`, false)
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null
    connect()
  }, delay)
}

function disconnect(): AstrbotStatus {
  manuallyDisconnected = true
  if (reconnectTimer) {
    clearTimeout(reconnectTimer)
    reconnectTimer = null
  }
  closeSocket()
  setStatus('idle', '已断开连接', false)
  return status
}

/* ---------------- 消息处理 ---------------- */

function handleMessage(raw: string): void {
  let msg: { type?: string; content?: string; message?: string; session_id?: string }
  try {
    msg = JSON.parse(raw) as typeof msg
  } catch {
    return
  }
  if (msg.type === 'token' || msg.type === 'done' || msg.type === 'error') {
    console.log(`[astrbot] recv type=${msg.type} activeChatId=${activeChatId}`)
  }
  switch (msg.type) {
    case 'token':
      if (msg.content) {
        fullText += msg.content
        emitter.emit('chunk', { chatId: activeChatId, delta: msg.content })
      }
      break
    case 'done':
      emitter.emit('done', { chatId: activeChatId, fullText })
      activeChatId = ''
      fullText = ''
      break
    case 'error':
      emitter.emit('error', {
        chatId: activeChatId,
        message: msg.message ?? msg.content ?? 'AstrBot 返回错误'
      })
      activeChatId = ''
      fullText = ''
      break
  }
}

/* ---------------- 发送 ---------------- */

function sendMessage(req: AstrbotSendRequest): AstrbotSendResult {
  if (!ws || status.state !== 'connected') {
    console.log(`[astrbot] sendMessage rejected: ws=${!!ws} state=${status.state}`)
    return { ok: false, message: 'AstrBot 未连接' }
  }
  activeChatId = req.chatId
  fullText = ''
  const payload: Record<string, unknown> = {
    type: 'message',
    content: req.content,
    session_id: ensureSessionId()
  }
  const cfg = getSettings().astrbot
  if (cfg.personaId.trim()) payload.persona_id = cfg.personaId.trim()
  if (req.images && req.images.length > 0) {
    const kb = req.images.reduce((acc, img) => acc + (img.dataUrl?.length ?? 0), 0)
    console.log(
      `[astrbot] sendMessage chatId=${req.chatId} images=${req.images.length} ` +
        `imageDataKB=${Math.round(kb / 1024)}`
    )
    payload.images = req.images.map((img) => ({ base64: img.dataUrl, mime_type: img.mimeType }))
  }
  if (req.systemPromptExtra && req.systemPromptExtra.trim()) {
    payload.system_prompt_extra = req.systemPromptExtra.trim()
  }
  ws.send(JSON.stringify(payload))
  console.log(`[astrbot] sendMessage sent chatId=${req.chatId}`)
  return { ok: true }
}

function stop(): void {
  if (ws && status.state === 'connected') {
    ws.send(JSON.stringify({ type: 'stop' }))
  }
}

function getStatus(): AstrbotStatus {
  return status
}

export { emitter as astrbotEmitter, connect, disconnect, getStatus, sendMessage, stop }
