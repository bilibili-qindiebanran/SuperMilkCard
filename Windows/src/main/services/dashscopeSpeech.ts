// DashScope（通义千问）语音服务：
//   - ASR: qwen-audio-3.0-asr-flash-streaming（WebSocket run-task 流式，16kHz/16bit PCM）
//   - TTS: qwen-audio-3.0-tts-flash（WebSocket run-task，MP3 输出）
//
// ASR run-task 协议（已实测验证）：
//   1) wss://dashscope.aliyuncs.com/api-ws/v1/inference，Authorization: Bearer <key>
//   2) JSON: {header:{action:"run-task",streaming:"duplex",task_id:"..."},
//             payload:{model,parameters:{format:"pcm",sample_rate:16000},input:{},
//                      task:"asr",task_group:"audio",function:"recognition"}}
//   3) 二进制帧：16kHz/16bit/单声道 PCM 分块
//   4) JSON: {header:{action:"finish-task",task_id},payload:{input:{}}}
//   5) 事件 result-generated → payload.output.sentence.text；task-finished/task-failed 结束

import WebSocket from 'ws'

const DASHSCOPE_WS_URL = 'wss://dashscope.aliyuncs.com/api-ws/v1/inference'

export interface DashScopeAsrOptions {
  apiKey: string
  model: string
  sampleRate?: number
  format?: string
}

export interface DashScopeTtsOptions {
  apiKey: string
  model: string
  voice?: string
  format?: string
  sampleRate?: number
}

/** 用 WebSocket 合成文本为音频（默认 MP3），返回完整音频 Buffer。 */
export function synthesizeText(
  text: string,
  opts: DashScopeTtsOptions
): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(DASHSCOPE_WS_URL, {
      headers: { Authorization: `Bearer ${opts.apiKey}` }
    })
    const format = opts.format ?? 'mp3'
    const sampleRate = opts.sampleRate ?? 24000
    const taskId = `tts_${Date.now()}_${Math.floor(Math.random() * 1000)}`
    const chunks: Buffer[] = []
    let finished = false
    let failedMsg = ''
    const timer = setTimeout(() => {
      ws.close()
      reject(new Error('TTS 超时（15s）'))
    }, 15000)

    ws.on('open', () => {
      // run-task 开始（TTS 协议：task=tts, function=SpeechSynthesizer）
      const start = {
        header: { action: 'run-task', streaming: 'duplex', task_id: taskId },
        payload: {
          model: opts.model,
          task_group: 'audio',
          task: 'tts',
          function: 'SpeechSynthesizer',
          input: {},
          parameters: {
            voice: opts.voice ?? 'longanfengyue',
            format,
            sample_rate: sampleRate,
            text_type: 'PlainText',
            volume: 50,
            rate: 1.0
          }
        }
      }
      ws.send(JSON.stringify(start))
      // 发送文本（input）
      ws.send(JSON.stringify({
        header: { action: 'continue-task', task_id: taskId },
        payload: { input: { text } }
      }))
      // finish-task
      ws.send(JSON.stringify({
        header: { action: 'finish-task', task_id: taskId },
        payload: { input: {} }
      }))
    })

    ws.on('message', (data) => {
      // 二进制帧 = 音频数据
      if (data instanceof Buffer || data instanceof ArrayBuffer) {
        chunks.push(Buffer.from(data as ArrayBuffer))
        return
      }
      const msg = JSON.parse(data.toString('utf-8'))
      const event = msg.header?.event
      if (event === 'task-finished') {
        finished = true
        clearTimeout(timer)
        ws.close()
        resolve(Buffer.concat(chunks))
      } else if (event === 'task-failed') {
        failedMsg = msg.header?.error_message ?? 'TTS task failed'
      }
    })

    ws.on('error', (err) => {
      clearTimeout(timer)
      reject(new Error(`TTS WebSocket 错误：${err.message}`))
    })

    ws.on('close', () => {
      clearTimeout(timer)
      if (!finished) reject(new Error(`TTS 连接关闭：${failedMsg}`))
    })
  })
}

/** 用 WebSocket 流式识别 PCM（16kHz/16bit/单声道小端），返回最终文本。 */
export function transcribePcm(
  pcm: Buffer,
  opts: DashScopeAsrOptions
): Promise<string> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(DASHSCOPE_WS_URL, {
      headers: { Authorization: `Bearer ${opts.apiKey}` }
    })
    const sampleRate = opts.sampleRate ?? 16000
    const format = opts.format ?? 'pcm'
    const taskId = `task_${Date.now()}_${Math.floor(Math.random() * 1000)}`
    let finalText = ''
    let finished = false
    let started = false
    let settled = false

    const fail = (message: string): void => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      try {
        ws.close()
      } catch {
        /* ignore close errors */
      }
      reject(new Error(message))
    }

    const timer = setTimeout(() => {
      fail('ASR 超时：通义服务未在 60 秒内完成识别')
    }, 60000)

    const sendAudio = (): void => {
      if (started === false || settled) return
      // DashScope 要求收到 task-started 后才能发送音频，否则会丢弃音频或直接结束任务。
      const chunkSize = 3200
      for (let offset = 0; offset < pcm.length; offset += chunkSize) {
        ws.send(pcm.subarray(offset, offset + chunkSize))
      }
      ws.send(JSON.stringify({
        header: { action: 'finish-task', task_id: taskId },
        payload: { input: {} }
      }))
      console.log('[stt] DashScope audio sent', { taskId, bytes: pcm.length, format })
    }

    ws.on('open', () => {
      const startMessage = {
        header: { action: 'run-task', streaming: 'duplex', task_id: taskId },
        payload: {
          model: opts.model,
          parameters: { format, sample_rate: sampleRate },
          input: {},
          task: 'asr',
          task_group: 'audio',
          function: 'recognition'
        }
      }
      console.log('[stt] DashScope task start', { taskId, model: opts.model, format, sampleRate })
      ws.send(JSON.stringify(startMessage))
    })

    ws.on('message', (data) => {
      let msg: {
        header?: { event?: string; error_message?: string }
        payload?: { output?: { sentence?: { text?: string } } }
      }
      try {
        msg = JSON.parse(data.toString('utf-8')) as typeof msg
      } catch {
        fail('ASR 返回了无法解析的消息')
        return
      }

      const event = msg.header?.event
      if (event === 'task-started') {
        started = true
        sendAudio()
      } else if (event === 'result-generated') {
        const text = msg.payload?.output?.sentence?.text
        if (text) {
          finalText = text
          console.log('[stt] DashScope partial result', text)
        }
      } else if (event === 'task-finished') {
        finished = true
        settled = true
        clearTimeout(timer)
        ws.close()
        resolve(finalText.trim())
      } else if (event === 'task-failed') {
        fail(`ASR 失败：${msg.header?.error_message ?? 'task failed'}`)
      }
    })

    ws.on('error', (err) => {
      fail(`ASR WebSocket 错误：${err.message}`)
    })

    ws.on('close', () => {
      clearTimeout(timer)
      if (!finished && !settled) {
        settled = true
        reject(new Error('ASR 连接提前关闭'))
      }
    })
  })
}
