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
    const timer = setTimeout(() => {
      ws.close()
      reject(new Error('ASR 超时（15s）'))
    }, 15000)

    ws.on('open', () => {
      // run-task 开始（format/sample_rate 在 parameters，input 空对象）
      const start = {
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
      ws.send(JSON.stringify(start))
      // 分块发 PCM（每块 3200 字节 ≈ 100ms）
      const CHUNK = 3200
      for (let i = 0; i < pcm.length; i += CHUNK) {
        ws.send(pcm.subarray(i, i + CHUNK))
      }
      // finish-task
      const finish = { header: { action: 'finish-task', task_id: taskId }, payload: { input: {} } }
      ws.send(JSON.stringify(finish))
    })

    ws.on('message', (data) => {
      const msg = JSON.parse(data.toString('utf-8'))
      const event = msg.header?.event
      if (event === 'result-generated') {
        const text = msg.payload?.output?.sentence?.text
        if (text) finalText += text
      } else if (event === 'task-finished') {
        finished = true
        clearTimeout(timer)
        ws.close()
        resolve(finalText.trim())
      } else if (event === 'task-failed') {
        clearTimeout(timer)
        const err = msg.header?.error_message ?? 'ASR task failed'
        ws.close()
        reject(new Error(`ASR 失败：${err}`))
      }
    })

    ws.on('error', (err) => {
      clearTimeout(timer)
      reject(new Error(`ASR WebSocket 错误：${err.message}`))
    })

    ws.on('close', () => {
      clearTimeout(timer)
      if (!finished) reject(new Error('ASR 连接提前关闭'))
    })
  })
}
