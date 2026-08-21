import { app } from 'electron'
import { join, basename, relative } from 'path'
import { mkdir, stat, cp, rm, readdir } from 'fs/promises'
import { is } from '@electron-toolkit/utils'
import type { Live2dModelInfo } from '@shared/types'

/** 用户模型目录：复制导入的模型到此，持久化且可写 */
export function modelsDir(): string {
  return join(app.getPath('userData'), 'live2d-models')
}

/** 内置模型目录（打包内只读） */
export function builtinDir(): string {
  return is.dev
    ? join(__dirname, '../../src/renderer/public/live2d')
    : join(__dirname, '../renderer/live2d')
}

/** 模型文件后缀（Cubism 4 / Cubism 2） */
const MODEL_SUFFIXES = ['.model3.json', '.model.json']

function isModelFile(name: string): boolean {
  return MODEL_SUFFIXES.some((s) => name.toLowerCase().endsWith(s))
}

/** 递归扫描目录，找出模型根目录名 → 模型文件相对路径的映射 */
async function findModelFiles(
  dir: string,
  root: string
): Promise<Array<{ name: string; rel: string }>> {
  let entries: string[]
  try {
    entries = await readdir(dir)
  } catch {
    return []
  }
  const found: Array<{ name: string; rel: string }> = []
  for (const entry of entries) {
    const full = join(dir, entry)
    let st
    try {
      st = await stat(full)
    } catch {
      continue
    }
    if (st.isDirectory()) {
      found.push(...(await findModelFiles(full, root)))
    } else if (st.isFile() && isModelFile(entry)) {
      const rel = relative(root, full).split('\\').join('/')
      // 首段目录名即模型名
      const name = rel.split('/')[0]
      found.push({ name, rel })
    }
  }
  return found
}

/** 扫描内置 + 用户目录，按模型名去重（用户目录优先），返回模型列表 */
export async function listModels(): Promise<Live2dModelInfo[]> {
  const byName = new Map<string, Live2dModelInfo>()

  const builtinRoot = builtinDir()
  for (const { name, rel } of await findModelFiles(builtinRoot, builtinRoot)) {
    byName.set(name, { id: name, name, modelUrl: `live2d://${rel}` })
  }

  const userRoot = modelsDir()
  for (const { name, rel } of await findModelFiles(userRoot, userRoot)) {
    // 用户目录优先，覆盖同名内置
    byName.set(name, { id: name, name, modelUrl: `live2d://${rel}` })
  }

  return [...byName.values()]
}

/** 从给定目录中找第一个模型文件（用于导入后确定启用哪个） */
async function firstModel(dir: string): Promise<{ name: string; rel: string } | null> {
  const found = await findModelFiles(dir, dir)
  return found[0] ?? null
}

/** 复制模型文件夹到用户目录并返回更新后的模型列表 + 首选 modelUrl */
export async function importModel(
  sourcePath: string
): Promise<{ models: Live2dModelInfo[]; modelUrl: string }> {
  const srcStat = await stat(sourcePath).catch(() => null)
  if (!srcStat || !srcStat.isDirectory()) {
    throw new Error('拖入的路径不是有效的模型文件夹')
  }

  const baseName = basename(sourcePath)
  const userRoot = modelsDir()
  await mkdir(userRoot, { recursive: true })

  // 目标目录去重：重名时追加 -1 / -2 …
  let dest = join(userRoot, baseName)
  let suffix = 1
  while (await stat(dest).then(() => true).catch(() => false)) {
    dest = join(userRoot, `${baseName}-${suffix}`)
    suffix += 1
  }

  await cp(sourcePath, dest, { recursive: true })

  const model = await firstModel(dest)
  if (!model) {
    // 无可用模型则回滚已复制目录
    await rm(dest, { recursive: true, force: true }).catch(() => {})
    throw new Error('文件夹中未找到 Live2D 模型文件（*.model3.json / *.model.json）')
  }

  const models = await listModels()
  const modelUrl = `live2d://${basename(dest)}/${model.rel}`
  return { models, modelUrl }
}

// 确保模型目录存在（供协议读取时调用）
export async function ensureModelsDir(): Promise<void> {
  await mkdir(modelsDir(), { recursive: true })
}
