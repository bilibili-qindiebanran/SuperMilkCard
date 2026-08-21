import { defineStore } from 'pinia'
import type {
  ApiKeySection,
  Live2dConfig,
  Persona,
  PublicAppSettings,
  PublicLlmConfig,
  PublicSttConfig,
  PublicTtsConfig,
  SettingsPatch,
  ThemeMode
} from '@shared/types'
import { DEFAULT_SETTINGS, toPublicSettings } from '@shared/types'
import { stripEmotionInstruction } from '@shared/emotion'

function cloneDefaults(): PublicAppSettings {
  return toPublicSettings(JSON.parse(JSON.stringify(DEFAULT_SETTINGS)))
}

/** 剥离人格提示词中手工写死的情绪指令句（现由 Live2D 模块自动注入），避免重复 */
function cleanPersona(p: Persona): Persona {
  return { ...p, systemPrompt: stripEmotionInstruction(p.systemPrompt) }
}

/** 发送前的补丁消毒：丢弃密钥相关字段（密钥修改走 setKey / clearKey） */
function sanitizePatch(partial: Partial<PublicAppSettings>): SettingsPatch {
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
  return next as SettingsPatch
}

export const useSettingsStore = defineStore('settings', {
  state: () => ({
    loaded: false,
    data: cloneDefaults(),
    saving: false
  }),
  getters: {
    llm: (s): PublicLlmConfig => s.data.llm,
    personas: (s): Persona[] => s.data.personas,
    activePersonaId: (s): string => s.data.activePersonaId,
    persona(s): Persona {
      return s.data.personas.find((p) => p.id === s.data.activePersonaId) ?? s.data.personas[0]
    },
    tts: (s): PublicTtsConfig => s.data.tts,
    stt: (s): PublicSttConfig => s.data.stt,
    live2d: (s): Live2dConfig => s.data.live2d,
    theme: (s): ThemeMode => s.data.theme
  },
  actions: {
    async load(): Promise<void> {
      const data = await window.api.settings.get()
      this.data = { ...data, personas: data.personas.map(cleanPersona) }
      this.loaded = true
    },
    async save(partial: Partial<PublicAppSettings>): Promise<void> {
      this.saving = true
      try {
        const next =
          partial.personas !== undefined
            ? { ...partial, personas: partial.personas.map(cleanPersona) }
            : partial
        this.data = await window.api.settings.set(sanitizePatch(next))
      } finally {
        this.saving = false
      }
    },
    async reset(): Promise<void> {
      this.data = await window.api.settings.reset()
    },
    async setKey(section: ApiKeySection, apiKey: string): Promise<void> {
      this.data = await window.api.settings.setKey(section, apiKey)
    },
    async clearKey(section: ApiKeySection): Promise<void> {
      this.data = await window.api.settings.clearKey(section)
    },

    async setActivePersona(id: string): Promise<void> {
      await this.save({ activePersonaId: id })
    },

    async setTheme(theme: ThemeMode): Promise<void> {
      this.data.theme = theme
      await this.save({ theme })
    },

    async updatePersona(persona: Persona): Promise<void> {
      const personas = this.data.personas.map((p) => (p.id === persona.id ? persona : p))
      await this.save({ personas })
    },

    async addPersona(persona: Persona): Promise<void> {
      await this.save({ personas: [...this.data.personas, persona] })
    },

    async deletePersona(id: string): Promise<void> {
      if (this.data.personas.length <= 1) return
      const personas = this.data.personas.filter((p) => p.id !== id)
      const activePersonaId =
        this.data.activePersonaId === id ? personas[0].id : this.data.activePersonaId
      await this.save({ personas, activePersonaId })
    }
  }
})
