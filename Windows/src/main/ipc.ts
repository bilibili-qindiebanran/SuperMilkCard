import { ipcMain, BrowserWindow } from 'electron'
import type {
  ApiKeySection,
  AppSettings,
  ChatStreamRequest,
  SettingsPatch,
  SttTranscribeRequest,
  TtsSynthesizeRequest
} from '@shared/types'
import { toPublicSettings } from '@shared/types'
import { getSettings, setSettings, resetSettings } from './services/settings'
import { streamChat } from './services/llm'
import { synthesize } from './services/tts'
import { transcribe } from './services/stt'
import { listModels, importModel } from './services/live2dModels'

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

  ipcMain.handle('tts:synthesize', (_e, req: TtsSynthesizeRequest) => synthesize(req))
  ipcMain.handle('stt:transcribe', (_e, req: SttTranscribeRequest) => transcribe(req))

  ipcMain.handle('live2d:list', () => listModels())
  ipcMain.handle('live2d:import', async (_e, sourcePath: string) => {
    const { models, modelUrl } = await importModel(sourcePath)
    setSettings({ live2d: { ...getSettings().live2d, modelUrl, enabled: true } })
    return { models, modelUrl }
  })
}
