import { ipcMain, BrowserWindow } from 'electron'
import type {
  ApiKeySection,
  ApiKeyTestRequest,
  ApiKeyTestResult,
  AppSettings,
  ChatStreamRequest,
  EmotionClassifyItem,
  Esp32Live2dState,
  SettingsPatch,
  SttTranscribeRequest,
  TtsSynthesizeRequest
} from '@shared/types'
import { toPublicSettings } from '@shared/types'
import { getSettings, setSettings, resetSettings } from './services/settings'
import { streamChat, classifyEmotions } from './services/llm'
import { synthesize } from './services/tts'
import { transcribe } from './services/stt'
import { listModels, importModel } from './services/live2dModels'
import {
  esp32Emitter,
  discover,
  connect,
  disconnect,
  getStatus as getEsp32Status,
  listDevices,
  sendChat,
  sendLive2dState,
  sendTts
} from './services/esp32'
import {
  perfEmitter,
  start as perfStart,
  stop as perfStop,
  getLatest as getPerfLatest
} from './services/perfMonitor'

const active = new Map<string, AbortController>()

/** 丢弃渲染层补丁中的 apiKey / hasApiKey，避免把状态性/敏感字段写入主进程配置 */
function normalizePatch(partial: SettingsPatch): Partial<AppSettings> {
  const next = { ...partial } as Record<string, unknown>
  for (const sec of ['llm', 'tts', 'stt']) {
    const cfg = next[sec]
    if (cfg && typeof cfg === 'object') {
      const c = { ...(cfg as Record<string, unknown>) }
      delete c.apiKey
      delete c.hasApiKey
      next[sec] = c
    }
  }
  return next as Partial<AppSettings>
}

/** 构造仅修改某一部分 apiKey 的补丁（其余字段沿用主进程当前值） */
function keyPatch(section: ApiKeySection, value: string): Partial<AppSettings> {
  switch (section) {
    case 'llm':
      return { llm: { ...getSettings().llm, apiKey: value } }
    case 'tts':
      return { tts: { ...getSettings().tts, apiKey: value } }
    case 'stt':
      return { stt: { ...getSettings().stt, apiKey: value } }
  }
}

/** 解析测试时实际生效的密钥与 Base URL（输入的优先，空则回退到当前配置） */
function resolveTestCreds(req: ApiKeyTestRequest): { key: string; base: string } {
  const s = getSettings()
  const configKey = req.section === 'llm' ? s.llm.apiKey : s[req.section].apiKey
  const configBase = req.section === 'llm' ? s.llm.baseUrl : s[req.section].baseUrl || s.llm.baseUrl
  return {
    key: req.apiKey.trim() || configKey,
    base: req.baseUrl.trim() || configBase
  }
}

/** 从 Base URL（可能是根路径，也可能是已含 /v1 或完整 /chat/completions 端点）推导 /models 列表端点 */
function resolveModelsUrl(baseUrl: string): string {
  let root = baseUrl.trim().replace(/\/+$/, '')
  // 兼容「把完整 /chat/completions 当作 Base」的情况：回退到根路径再拼 /models
  if (/\/chat\/completions$/i.test(root)) {
    root = root.replace(/\/chat\/completions$/i, '').replace(/\/+$/, '')
  }
  return `${root}/models`
}

/** 以 GET {base}/models 作为 OpenAI 兼容口径的可达性与鉴权校验（8 秒超时） */
async function testApiKey(req: ApiKeyTestRequest): Promise<ApiKeyTestResult> {
  const { key, base } = resolveTestCreds(req)
  if (!key) return { ok: false, message: '尚未填写 API Key' }
  if (!base) return { ok: false, message: '尚未填写 Base URL' }

  const url = resolveModelsUrl(base)
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), 8000)
  try {
    const res = await fetch(url, {
      headers: { Authorization: `Bearer ${key}` },
      signal: controller.signal
    })
    if (res.ok) return { ok: true, message: '连接成功，密钥有效' }
    return { ok: false, message: `连接失败（HTTP ${res.status}）` }
  } catch (err) {
    const aborted = err instanceof Error && err.name === 'AbortError'
    const message = aborted
      ? '连接超时'
      : `无法连接：${err instanceof Error ? err.message : String(err)}`
    return { ok: false, message }
  } finally {
    clearTimeout(timer)
  }
}

export function registerIpc(): void {
  ipcMain.handle('settings:get', () => toPublicSettings(getSettings()))
  ipcMain.handle('settings:set', (_e, partial: SettingsPatch) =>
    toPublicSettings(setSettings(normalizePatch(partial)))
  )
  ipcMain.handle('settings:reset', () => toPublicSettings(resetSettings()))
  ipcMain.handle('settings:set-key', (_e, section: ApiKeySection, apiKey: string) =>
    toPublicSettings(setSettings(keyPatch(section, apiKey.trim())))
  )
  ipcMain.handle('settings:clear-key', (_e, section: ApiKeySection) =>
    toPublicSettings(setSettings(keyPatch(section, '')))
  )
  ipcMain.handle('settings:test-key', (_e, req: ApiKeyTestRequest) => testApiKey(req))

  ipcMain.on('llm:chat-stream', (e, req: ChatStreamRequest) => {
    const controller = new AbortController()
    active.set(req.chatId, controller)
    const win = BrowserWindow.fromWebContents(e.sender)
    console.log('[ipc] llm:chat-stream chatId=%s', req.chatId)

    void streamChat(
      req,
      (chunk) => {
        win?.webContents.send('llm:chunk', chunk)
      },
      controller.signal
    )
      .then((fullText) => {
        win?.webContents.send('llm:done', { chatId: req.chatId, fullText })
      })
      .catch((err: unknown) => {
        if (controller.signal.aborted) {
          win?.webContents.send('llm:aborted', { chatId: req.chatId })
        } else {
          const message = err instanceof Error ? err.message : String(err)
          win?.webContents.send('llm:error', { chatId: req.chatId, message })
        }
      })
      .finally(() => {
        active.delete(req.chatId)
      })
  })

  ipcMain.on('llm:abort', (_e, chatId: string) => {
    console.log('[ipc] llm:abort chatId=%s has=%s', chatId, active.has(chatId))
    active.get(chatId)?.abort()
  })

  ipcMain.handle('llm:classify', (_e, items: EmotionClassifyItem[]) => classifyEmotions(items))

  ipcMain.handle('tts:synthesize', (_e, req: TtsSynthesizeRequest) => synthesize(req))
  ipcMain.handle('stt:transcribe', (_e, req: SttTranscribeRequest) => transcribe(req))

  ipcMain.handle('live2d:list', () => listModels())
  ipcMain.handle('live2d:import', async (_e, sourcePath: string) => {
    const { models, modelUrl } = await importModel(sourcePath)
    setSettings({ live2d: { ...getSettings().live2d, modelUrl, enabled: true } })
    return { models, modelUrl }
  })

  // ESP32 连接 / 发现
  discover()
  ipcMain.handle('esp32:connect', () => connect())
  ipcMain.handle('esp32:disconnect', () => disconnect())
  ipcMain.handle('esp32:get-status', () => getEsp32Status())
  ipcMain.handle('esp32:discover', () => discover())
  ipcMain.handle('esp32:list-devices', () => listDevices())
  ipcMain.handle('esp32:send-chat', (_e, role: 'user' | 'assistant', content: string) =>
    sendChat(role, content)
  )
  ipcMain.handle('esp32:send-tts', (_e, text: string) => sendTts(text))
  ipcMain.handle('esp32:send-live2d-state', (_e, state: Esp32Live2dState) =>
    sendLive2dState(state)
  )

  // 性能监测
  ipcMain.handle('perf:start', () => perfStart())
  ipcMain.handle('perf:stop', () => perfStop())
  ipcMain.handle('perf:get-latest', () => getPerfLatest())

  // 主进程 → 渲染进程 事件广播
  const broadcast = (channel: string, data: unknown): void => {
    for (const win of BrowserWindow.getAllWindows()) win.webContents.send(channel, data)
  }
  esp32Emitter.on('status', (s) => broadcast('esp32:status', s))
  esp32Emitter.on('devices', (d) => broadcast('esp32:devices', d))
  esp32Emitter.on('text', (m) => broadcast('esp32:text', m))
  esp32Emitter.on('voice-text', (m) => broadcast('esp32:voice-text', m))
  esp32Emitter.on('live2d-command', (c) => broadcast('esp32:live2d-command', c))
  esp32Emitter.on('error', (m) => broadcast('esp32:error', m))
  perfEmitter.on('sample', (s) => broadcast('perf:sample', s))
}
