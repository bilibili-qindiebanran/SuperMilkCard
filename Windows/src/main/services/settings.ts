import { app } from 'electron'
import { readFileSync, writeFileSync, mkdirSync } from 'fs'
import { join, dirname } from 'path'
import type { AppSettings } from '@shared/types'
import { DEFAULT_SETTINGS } from '@shared/types'

let cache: AppSettings | null = null

function filePath(): string {
  return join(app.getPath('userData'), 'settings.json')
}

function isPlainObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v)
}

function deepMerge(base: unknown, override: unknown): unknown {
  if (!isPlainObject(base) || !isPlainObject(override)) {
    return override === undefined ? base : override
  }
  const out: Record<string, unknown> = { ...base }
  for (const [key, value] of Object.entries(override)) {
    out[key] = deepMerge(base[key], value)
  }
  return out
}

function clone<T>(v: T): T {
  return JSON.parse(JSON.stringify(v)) as T
}

export function getSettings(): AppSettings {
  if (cache) return cache
  try {
    const raw = readFileSync(filePath(), 'utf-8')
    const parsed = JSON.parse(raw) as unknown
    cache = deepMerge(DEFAULT_SETTINGS, parsed) as AppSettings
  } catch {
    cache = clone(DEFAULT_SETTINGS)
  }
  return cache
}

export function setSettings(partial: Partial<AppSettings>): AppSettings {
  const current = getSettings()
  const next = deepMerge(current, partial) as AppSettings
  return persist(next)
}

export function resetSettings(): AppSettings {
  return persist(clone(DEFAULT_SETTINGS))
}

function persist(next: AppSettings): AppSettings {
  const file = filePath()
  mkdirSync(dirname(file), { recursive: true })
  writeFileSync(file, JSON.stringify(next, null, 2), 'utf-8')
  cache = next
  return next
}
