import { contextBridge, ipcRenderer, webUtils } from 'electron'
import { electronAPI } from '@electron-toolkit/preload'
import type {
  ApiKeySection,
  ApiKeyTestRequest,
  ApiKeyTestResult,
  ChatStreamRequest,
  EmotionClassifyItem,
  LlmAborted,
  LlmChunk,
  LlmDone,
  LlmError,
  Live2dModelInfo,
  PublicAppSettings,
  RendererApi,
  SettingsPatch,
  SttTranscribeRequest,
  SttTranscribeResult,
  TtsSynthesizeRequest,
  TtsSynthesizeResult
} from '@shared/types'

type Unsubscribe = () => void

function subscribe<T>(channel: string, cb: (e: T) => void): Unsubscribe {
  const listener = (_event: Electron.IpcRendererEvent, data: T): void => cb(data)
  ipcRenderer.on(channel, listener)
  return () => {
    ipcRenderer.removeListener(channel, listener)
  }
}

// Custom APIs for renderer
const api: RendererApi = {
  llm: {
    chatStream: (req: ChatStreamRequest): void => ipcRenderer.send('llm:chat-stream', req),
    abort: (chatId: string): void => ipcRenderer.send('llm:abort', chatId),
    onChunk: (cb) => subscribe<LlmChunk>('llm:chunk', cb),
    onDone: (cb) => subscribe<LlmDone>('llm:done', cb),
    onError: (cb) => subscribe<LlmError>('llm:error', cb),
    onAborted: (cb) => subscribe<LlmAborted>('llm:aborted', cb),
    classify: (items: EmotionClassifyItem[]): Promise<Record<string, string>> =>
      ipcRenderer.invoke('llm:classify', items)
  },
  settings: {
    get: (): Promise<PublicAppSettings> => ipcRenderer.invoke('settings:get'),
    set: (partial: SettingsPatch): Promise<PublicAppSettings> =>
      ipcRenderer.invoke('settings:set', partial),
    reset: (): Promise<PublicAppSettings> => ipcRenderer.invoke('settings:reset'),
    setKey: (section: ApiKeySection, apiKey: string): Promise<PublicAppSettings> =>
      ipcRenderer.invoke('settings:set-key', section, apiKey),
    clearKey: (section: ApiKeySection): Promise<PublicAppSettings> =>
      ipcRenderer.invoke('settings:clear-key', section),
    testKey: (req: ApiKeyTestRequest): Promise<ApiKeyTestResult> =>
      ipcRenderer.invoke('settings:test-key', req)
  },
  tts: {
    synthesize: (req: TtsSynthesizeRequest): Promise<TtsSynthesizeResult> =>
      ipcRenderer.invoke('tts:synthesize', req)
  },
  stt: {
    transcribe: (req: SttTranscribeRequest): Promise<SttTranscribeResult> =>
      ipcRenderer.invoke('stt:transcribe', req)
  },
  live2d: {
    listModels: (): Promise<Live2dModelInfo[]> => ipcRenderer.invoke('live2d:list'),
    importModel: (sourcePath: string): Promise<{ models: Live2dModelInfo[]; modelUrl: string }> =>
      ipcRenderer.invoke('live2d:import', sourcePath),
    getPathForFile: (file: File): string => webUtils.getPathForFile(file)
  }
}

// Use `contextBridge` APIs to expose Electron APIs to
// renderer only if context isolation is enabled, otherwise
// just add to the DOM global.
if (process.contextIsolated) {
  try {
    contextBridge.exposeInMainWorld('electron', electronAPI)
    contextBridge.exposeInMainWorld('api', api)
  } catch (error) {
    console.error(error)
  }
} else {
  // @ts-ignore (define in dts)
  window.electron = electronAPI
  // @ts-ignore (define in dts)
  window.api = api
}
