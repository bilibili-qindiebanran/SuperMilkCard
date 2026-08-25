import { EventEmitter } from 'events'
import { createConnection, type Socket } from 'net'
import { createSocket, type Socket as UdpSocket } from 'dgram'
import type {
  Esp32Config,
  Esp32Device,
  Esp32Live2dState,
  Esp32MusicCommand,
  Esp32SendResult,
  Esp32Status,
  Esp32ConnectionState
} from '@shared/types'
import { getSettings } from './settings'
import { synthesize } from './tts'
import { transcribe } from './stt'
import { encodeFrame, encodeTextFrame, FrameDecoder, FrameType } from './framing'

/** ESP32 局域网发现的 UDP 端口（广播/组播监听） */
const DISCOVERY_PORT = 4210

interface AudioMeta {
  format: string
  sampleRate: number
  channels: number
  bits: number
}

const emitter = new EventEmitter()

const devices = new Map<string, Esp32Device>()
let udpSocket: UdpSocket | null = null

let socket: Socket | null = null
const decoder = new FrameDecoder()
let status: Esp32Status = { connected: false, state: 'idle', message: '' }
let reconnectTimer: NodeJS.Timeout | null = null
let reconnectAttempt = 0
let manuallyDisconnected = false

let audioMeta: AudioMeta | null = null
let audioChunks: Buffer[] = []

/* ---------------- 设备发现 ---------------- */

function ensureDiscovery(): void {
  if (udpSocket) return
  udpSocket = createSocket('udp4')
  udpSocket.on('error', () => {
    /* 忽略 UDP 监听错误 */
  })
  udpSocket.on('message', (msg, rinfo) => {
    try {
      const data = JSON.parse(msg.toString('utf-8')) as {
        id?: string
        name?: string
        tcpPort?: number
        wsPort?: number
      }
      if (!data.id) return
      devices.set(data.id, {
        id: data.id,
        name: data.name ?? '',
        host: rinfo.address,
        tcpPort: data.tcpPort ?? 0,
        wsPort: data.wsPort ?? 0,
        lastSeen: Date.now()
      })
      emitter.emit('devices', listDevices())
    } catch {
      /* 忽略非 JSON 广播 */
    }
  })
  udpSocket.bind(DISCOVERY_PORT, () => {
    udpSocket?.setBroadcast(true)
  })
}

function listDevices(): Esp32Device[] {
  return Array.from(devices.values()).sort((a, b) => a.id.localeCompare(b.id))
}

/** 解析目标设备连接参数：优先按识别码取发现结果，否则回退手动配置 */
function resolveTarget(): { host: string; tcpPort: number; wsPort: number } {
  const cfg = getSettings().esp32
  if (cfg.deviceId && devices.has(cfg.deviceId)) {
    const d = devices.get(cfg.deviceId) as Esp32Device
    return {
      host: d.host,
      tcpPort: d.tcpPort || cfg.tcpPort,
      wsPort: d.wsPort || cfg.wsPort
    }
  }
  return { host: cfg.host, tcpPort: cfg.tcpPort, wsPort: cfg.wsPort }
}

/* ---------------- 状态 ---------------- */

function setStatus(state: Esp32ConnectionState, message: string, connected: boolean): void {
  status = { connected, state, message }
  emitter.emit('status', status)
}

/* ---------------- TCP 连接 ---------------- */

function connect(): Esp32Status {
  manuallyDisconnected = false
  const cfg = getSettings().esp32
  if (!cfg.enabled) {
    setStatus('idle', 'ESP32 连接未启用', false)
    return status
  }
  const target = resolveTarget()
  if (!target.host) {
    setStatus('error', '未配置 ESP32 主机地址或识别码', false)
    return status
  }
  openTcp(target.host, target.tcpPort, cfg)
  return status
}

function openTcp(host: string, port: number, cfg: Esp32Config): void {
  closeTcp()
  setStatus('connecting', `正在连接 ${host}:${port}`, false)

  const sock = createConnection({ host, port })
  socket = sock
  sock.setNoDelay(true)

  sock.on('connect', () => {
    reconnectAttempt = 0
    setStatus('connected', `已连接 ${host}:${port}`, true)
  })
  sock.on('data', (chunk: Buffer) => handleData(chunk, cfg))
  sock.on('error', (err) => {
    setStatus('error', err.message, false)
  })
  sock.on('close', () => {
    if (socket === sock) socket = null
    decoder.reset()
    audioMeta = null
    audioChunks = []
    if (!manuallyDisconnected && getSettings().esp32.enabled) {
      scheduleReconnect(cfg)
    }
  })
}

function closeTcp(): void {
  if (socket) {
    socket.destroy()
    socket = null
  }
  decoder.reset()
  audioMeta = null
  audioChunks = []
}

function scheduleReconnect(cfg: Esp32Config): void {
  if (reconnectTimer) return
  reconnectAttempt += 1
  const base = Math.max(100, cfg.reconnectIntervalMs || 1000)
  const delay = Math.min(base * 2 ** (reconnectAttempt - 1), 30000)
  setStatus('reconnecting', `连接断开，${delay}ms 后重连`, false)
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null
    connect()
  }, delay)
}

function disconnect(): Esp32Status {
  manuallyDisconnected = true
  if (reconnectTimer) {
    clearTimeout(reconnectTimer)
    reconnectTimer = null
  }
  closeTcp()
  setStatus('idle', '已断开连接', false)
  return status
}

/* ---------------- 帧分发 ---------------- */

function handleData(chunk: Buffer, cfg: Esp32Config): void {
  for (const frame of decoder.push(chunk)) {
    if (frame.type === FrameType.HELLO) handleHello(frame.payload, cfg)
    else if (frame.type === FrameType.TEXT) handleText(frame.payload)
    else if (frame.type === FrameType.AUDIO) handleAudio(frame.payload)
  }
}

function handleHello(payload: Buffer, cfg: Esp32Config): void {
  try {
    const msg = JSON.parse(payload.toString('utf-8')) as { id?: string }
    if (cfg.deviceId && msg.id && msg.id !== cfg.deviceId) {
      setStatus('error', `设备识别码不匹配：期望 ${cfg.deviceId}，实际 ${msg.id}`, false)
      closeTcp()
    }
  } catch {
    /* 忽略非法握手帧 */
  }
}

function handleText(payload: Buffer): void {
  let msg: {
    type?: string
    content?: string
    text?: string
    message?: string
    title?: string
    url?: string
    format?: string
    sampleRate?: number
    channels?: number
    bits?: number
  }
  try {
    msg = JSON.parse(payload.toString('utf-8')) as typeof msg
  } catch {
    emitter.emit('text', { content: payload.toString('utf-8') })
    return
  }

  switch (msg.type) {
    case 'text':
      emitter.emit('text', { content: msg.content ?? '' })
      break
    case 'audio_start':
    case 'voice_start':
      /* voice_start 为 ESP32 语音上行（16k/1ch/16bit PCM），语义同 audio_start */
      audioMeta = {
        format: msg.format ?? 'pcm',
        sampleRate: msg.sampleRate ?? 16000,
        channels: msg.channels ?? 1,
        bits: msg.bits ?? 16
      }
      audioChunks = []
      break
    case 'audio_end':
    case 'voice_end':
      void finalizeVoice()
      break
    case 'chat':
      emitter.emit('text', { content: msg.content ?? '' })
      break
    case 'voice_text':
      emitter.emit('voice-text', { text: msg.text ?? '' })
      break
    case 'voice_error':
      emitter.emit('error', { message: msg.message ?? 'ESP32 STT 失败' })
      break
    case 'live2d_command': {
      const command = (msg as { command?: string }).command
      if (command === 'enter' || command === 'return_home' || command === 'reconnect') {
        emitter.emit('live2d-command', { command })
      }
      break
    }
    case 'music_play': {
      const title = typeof msg.title === 'string' ? msg.title : ''
      const url = typeof msg.url === 'string' ? msg.url : ''
      if (title && url) emitter.emit('music-play', { title, url } satisfies Esp32MusicCommand)
      break
    }
    default:
      emitter.emit('text', { content: msg.content ?? payload.toString('utf-8') })
  }
}

function handleAudio(payload: Buffer): void {
  if (audioMeta) audioChunks.push(payload)
}

/* ---------------- 语音输入（ESP32 → STT） ---------------- */

function pcmToWav(pcm: Buffer, sampleRate: number, channels: number, bits: number): Buffer {
  const byteRate = (sampleRate * channels * bits) / 8
  const blockAlign = (channels * bits) / 8
  const dataSize = pcm.length
  const buf = Buffer.alloc(44 + dataSize)
  buf.write('RIFF', 0)
  buf.writeUInt32LE(36 + dataSize, 4)
  buf.write('WAVE', 8)
  buf.write('fmt ', 12)
  buf.writeUInt32LE(16, 16)
  buf.writeUInt16LE(1, 20)
  buf.writeUInt16LE(channels, 22)
  buf.writeUInt32LE(sampleRate, 24)
  buf.writeUInt32LE(byteRate, 28)
  buf.writeUInt16LE(blockAlign, 32)
  buf.writeUInt16LE(bits, 34)
  buf.write('data', 36)
  buf.writeUInt32LE(dataSize, 40)
  pcm.copy(buf, 44)
  return buf
}

async function finalizeVoice(): Promise<void> {
  const meta = audioMeta
  audioMeta = null
  if (!meta || audioChunks.length === 0) return

  const pcm = Buffer.concat(audioChunks)
  audioChunks = []
  const wav = pcmToWav(pcm, meta.sampleRate, meta.channels, meta.bits)
  // 调试：保存 ESP32 录音 WAV 供本地 STT 调试验证
  try {
    const { writeFile } = await import('fs/promises')
    const { join } = await import('path')
    const { tmpdir } = await import('os')
    const p = join(tmpdir(), `esp32_mic_${Date.now()}.wav`)
    await writeFile(p, wav)
    console.log('[esp32] saved mic wav:', p, wav.length, 'bytes')
  } catch {
    /* 忽略保存失败 */
  }
  try {
    const { text } = await transcribe({ audioBase64: wav.toString('base64'), mimeType: 'audio/wav' })
    if (text.trim()) emitter.emit('voice-text', { text: text.trim() })
  } catch (err) {
    emitter.emit('error', { message: err instanceof Error ? err.message : String(err) })
  }
}

/* ---------------- 发送 ---------------- */

function sendChat(role: 'user' | 'assistant', content: string): Esp32SendResult {
  if (!socket || status.state !== 'connected') return { ok: false, message: 'ESP32 未连接' }
  socket.write(encodeTextFrame({ type: 'chat', role, content }))
  return { ok: true }
}

function writeSocketBuffer(data: Buffer): Promise<void> {
  if (!socket || status.state !== 'connected') return Promise.reject(new Error('ESP32 未连接'))
  if (socket.write(data)) return Promise.resolve()
  return new Promise((resolve, reject) => {
    const onDrain = (): void => {
      socket?.off('error', onError)
      resolve()
    }
    const onError = (err: Error): void => {
      socket?.off('drain', onDrain)
      reject(err)
    }
    socket.once('drain', onDrain)
    socket.once('error', onError)
  })
}

async function playMusicOnEsp32(songUrl: string): Promise<Esp32SendResult> {
  if (!socket || status.state !== 'connected') return { ok: false, message: 'ESP32 未连接' }

  try {
    const song = new URL(songUrl)
    const songId = song.searchParams.get('id')
    if (song.hostname !== 'music.163.com' || song.pathname !== '/song' || !songId) {
      return { ok: false, message: '不支持的网易云歌曲地址' }
    }

    const mediaUrl = `https://music.163.com/song/media/outer/url?id=${encodeURIComponent(songId)}.mp3`
    const response = await fetch(mediaUrl, {
      headers: {
        Accept: 'audio/mpeg',
        Referer: 'https://music.163.com/'
      },
      redirect: 'follow'
    })
    if (!response.ok || !response.body) {
      return { ok: false, message: `网易云音频获取失败（HTTP ${response.status}）` }
    }

    await writeSocketBuffer(encodeTextFrame({ type: 'audio_start', format: 'mp3' }))
    for await (const chunk of response.body) {
      await writeSocketBuffer(encodeFrame(FrameType.AUDIO, Buffer.from(chunk)))
    }
    await writeSocketBuffer(encodeTextFrame({ type: 'audio_end' }))
    return { ok: true }
  } catch (err) {
    return { ok: false, message: err instanceof Error ? err.message : String(err) }
  }
}

async function sendTts(text: string): Promise<Esp32SendResult> {
  if (!socket || status.state !== 'connected') return { ok: false, message: 'ESP32 未连接' }
  try {
    const { audioBase64 } = await synthesize({ text })
    const audio = Buffer.from(audioBase64, 'base64')
    socket.write(encodeTextFrame({ type: 'audio_start', format: 'mp3' }))
    const chunkSize = 32 * 1024
    for (let i = 0; i < audio.length; i += chunkSize) {
      socket.write(encodeFrame(FrameType.AUDIO, audio.subarray(i, i + chunkSize)))
    }
    socket.write(encodeTextFrame({ type: 'audio_end' }))
    return { ok: true }
  } catch (err) {
    return { ok: false, message: err instanceof Error ? err.message : String(err) }
  }
}

function getStatus(): Esp32Status {
  return status
}

/** 触发一次设备发现并返回当前已发现设备列表 */
function discover(): Esp32Device[] {
  ensureDiscovery()
  return listDevices()
}

/**
 * 发送 Live2D 状态到 ESP32（live2d_state 帧）。
 * 字段白名单：expression/motion 未知值由 ESP32 回退 neutral/idle；
 * messagePreview 由 ESP32 截断 96 UTF-8 字节。
 */
function sendLive2dState(state: Esp32Live2dState): Esp32SendResult {
  if (!socket || status.state !== 'connected') return { ok: false, message: 'ESP32 未连接' }
  socket.write(
    encodeTextFrame({
      type: 'live2d_state',
      expression: state.expression,
      motion: state.motion,
      messagePreview: state.messagePreview
    })
  )
  return { ok: true }
}

/* ---------------- 导出 ---------------- */

export {
  emitter as esp32Emitter,
  discover,
  connect,
  disconnect,
  getStatus,
  listDevices,
  resolveTarget,
  sendChat,
  sendTts,
  playMusicOnEsp32,
  sendLive2dState
}
