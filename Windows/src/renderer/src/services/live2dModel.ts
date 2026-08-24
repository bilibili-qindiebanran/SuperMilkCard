import * as PIXI from 'pixi.js'
// 从 cubism4 入口导入，仅注册 Cubism 4 运行时（需在调用前已加载 live2dcubismcore.min.js）
import { Live2DModel } from 'pixi-live2d-display/cubism4'
import { install } from '@pixi/unsafe-eval'

// CSP 禁止 unsafe-eval 时，用静态 uniform 同步替代 new Function 生成（必须在创建渲染器前调用）
install({ ShaderSystem: PIXI.ShaderSystem })

Live2DModel.registerTicker(PIXI.Ticker)

/**
 * 表情语义特征识别（面向大多数用户上传模型）。
 * 不同模型的表达式文件名五花八门（mao 用 exp_01，murasame 用 exp1.exp3），
 * 「序号 = 语义」的假设跨模型不成立。这里用**双层推理**：
 *   1) 表情名关键词：设计师通常语义化命名（happy/sad/angry…或开心/难过/生气…），跨模型最可靠，命中即返回。
 *   2) 官方标准参数评分：把 Cubism Standard Parameter List 的面部参数全部纳入，按每个语义累计得分、取最高，
 *      避免硬顺序误判。
 * 两者都不确定时返回 null（不强行套表情，还原模型本体默认脸）。
 */
export type ExpressionSemantic = 'neutral' | 'happy' | 'sad' | 'angry' | 'surprised' | 'blush'

/** 供 UI 遍历的语义顺序 */
export const EXPRESSION_SEMANTICS: ExpressionSemantic[] = [
  'neutral',
  'happy',
  'sad',
  'angry',
  'surprised',
  'blush'
]

/** 语义中文标签（设置页展示用） */
export const EXPRESSION_SEMANTIC_LABELS: Record<ExpressionSemantic, string> = {
  neutral: '正常',
  happy: '开心',
  sad: '难过',
  angry: '生气',
  surprised: '惊讶',
  blush: '害羞'
}

/**
 * 强度等级：>=1。1 = 基础档（不带后缀），2/3/… = 更强（键形式 语义@等级）。
 * 例如 开心(happy) 是基础档，更开心(happy@2)、非常开心(happy@3)、超开心(happy@4)。
 */
export type ExpressionLevel = number

/** 语义+等级 → 唯一 tierKey。基础档不带后缀，兼容旧覆盖键（如 'happy'）。 */
export function tierKey(semantic: ExpressionSemantic, level: ExpressionLevel): string {
  return level <= 1 ? semantic : `${semantic}@${level}`
}

/** tierKey → {semantic, level}；解析失败返回 null（非本语义集合或等级非法）。 */
export function parseTierKey(
  key: string
): { semantic: ExpressionSemantic; level: ExpressionLevel } | null {
  if (!key) return null
  const idx = key.indexOf('@')
  if (idx < 0) {
    return EXPRESSION_SEMANTICS.includes(key as ExpressionSemantic)
      ? { semantic: key as ExpressionSemantic, level: 1 }
      : null
  }
  const sem = key.slice(0, idx) as ExpressionSemantic
  const lvl = Number(key.slice(idx + 1))
  if (!EXPRESSION_SEMANTICS.includes(sem)) return null
  if (!Number.isInteger(lvl) || lvl < 2) return null
  return { semantic: sem, level: lvl }
}

/** 等级中文前缀：2=更、3=非常、4=超… 用于生成「更开心 / 非常开心」这类标签。 */
export const LEVEL_PREFIX: Record<number, string> = {
  2: '更',
  3: '非常',
  4: '超'
}

/** 档位中文标签：如 开心 / 更开心 / 非常开心。 */
export function tierLabel(semantic: ExpressionSemantic, level: ExpressionLevel): string {
  const base = EXPRESSION_SEMANTIC_LABELS[semantic]
  if (level <= 1) return base
  return `${LEVEL_PREFIX[level] ?? `第${level}档`}${base}`
}

/** 表情名强度词 → 等级（命中匹配到的最大等级；无则默认 1）。 */
const LEVEL_KEYWORDS: Array<{ level: ExpressionLevel; words: string[] }> = [
  { level: 2, words: ['more', 'morer', 'quite', 'really', 'ver', '较', '很', '更', '特别', '越发'] },
  { level: 3, words: ['very', 'super', 'extrem', 'highly', '太', '超', '极其', '特别特别', '狂', '暴', '极了'] },
  { level: 4, words: ['max', 'ultimate', 'ultra', 'full', 'extreme', '最', '爆', '超超', '满', '极大'] }
]

/** 由表情名推断强度等级（取最大命中，默认 1）。 */
function inferTierLevel(name?: string): ExpressionLevel {
  if (!name) return 1
  const key = normalizeName(name)
  if (!key) return 1
  let level: ExpressionLevel = 1
  for (const { level: lvl, words } of LEVEL_KEYWORDS) {
    if (lvl <= level) continue
    if (words.some((w) => key.includes(normalizeName(w)))) level = lvl
  }
  return level
}

/** 一个档位条目：语义 + 等级 + 唯一键。 */
export interface TierEntry {
  semantic: ExpressionSemantic
  level: ExpressionLevel
  key: string
}

/**
 * 合并自动识别档位与用户新增/移除的档位，得到当前生效的档位列表。
 * - auto：模型自动识别出的档位；
 * - additions：用户手动新增的 tierKey（如 happy@2）；
 * - removals：用户手动隐藏的自动档位 tierKey。
 * 结果按「语义顺序 + 等级升序」排序、去重（移除了的自动档位跳过；被新增又移除的也跳过）。
 */
export function mergeTiers(
  auto: TierEntry[],
  additions: string[],
  removals: string[]
): TierEntry[] {
  const result: TierEntry[] = []
  const seen = new Set<string>()
  const push = (t: TierEntry): void => {
    if (!seen.has(t.key)) {
      seen.add(t.key)
      result.push(t)
    }
  }
  for (const t of auto ?? []) {
    if (removals?.includes(t.key)) continue
    push({ ...t })
  }
  for (const key of additions ?? []) {
    if (removals?.includes(key)) continue
    const parsed = parseTierKey(key)
    if (parsed) push({ semantic: parsed.semantic, level: parsed.level, key: tierKey(parsed.semantic, parsed.level) })
  }
  const order = (s: ExpressionSemantic): number => EXPRESSION_SEMANTICS.indexOf(s)
  result.sort((a, b) => order(a.semantic) - order(b.semantic) || a.level - b.level)
  return result
}

/** 表情参数相对默认值的偏移幅度（绝对值平均；Multiply 取 |Value-1|），供同语义多档按强弱排序。 */
function magnitudeScore(params: ExpressionParam[]): number {
  if (!params || params.length === 0) return 0
  let sum = 0
  let count = 0
  for (const p of params) {
    const dev = p.Blend === 'Multiply' ? Math.abs(p.Value - 1) : Math.abs(p.Value)
    if (dev > 0.01) {
      sum += dev
      count++
    }
  }
  return count > 0 ? sum / count : 0
}

interface ExpressionParam {
  Id: string
  Value: number
  Blend?: string
}

/** 读取参数的当前值（未定义记为 0） */
function pickParam(params: ExpressionParam[], id: string): number {
  const p = params.find((x) => x.Id === id)
  return p ? p.Value : 0
}

/** 表情名关键词 → 语义（越靠前优先），命中返回语义，否则 null */
const NAME_KEYWORDS: Array<{ semantic: ExpressionSemantic; words: string[] }> = [
  {
    semantic: 'angry',
    words: ['angry', 'anger', 'mad', 'furious', '生气', '愤怒', '恼火', '黑化', '压力', '线条', '阴暗']
  },
  {
    semantic: 'blush',
    words: ['blush', 'shy', 'embarrassed', 'ashamed', '害羞', '脸红', '尴尬', '娇羞']
  },
  {
    semantic: 'surprised',
    words: ['surprised', 'surprise', 'shocked', 'shock', 'amazed', '惊讶', '震惊', '吃惊', '吓']
  },
  {
    semantic: 'sad',
    words: ['sad', 'sadness', 'cry', 'crying', 'unhappy', 'grief', '难过', '伤心', '悲伤', '哭', '沮丧', '委屈']
  },
  {
    semantic: 'happy',
    words: ['happy', 'joy', 'joyful', 'smile', 'smiling', 'laugh', 'delight', 'glad', '开心', '高兴', '笑', '微笑', '愉悦', '得意']
  },
  {
    semantic: 'neutral',
    words: ['neutral', 'normal', 'calm', 'default', 'idle', 'serious', '平静', '正常', '默认', '淡定', '认真']
  }
]

/** 归一化表情名：小写，仅保留字母/中文，便于关键词匹配 */
function normalizeName(name: string): string {
  return name.toLowerCase().replace(/[^a-z\u4e00-\u9fa5]/g, '')
}

function inferByName(name?: string): ExpressionSemantic | null {
  if (!name) return null
  const key = normalizeName(name)
  if (!key) return null
  for (const { semantic, words } of NAME_KEYWORDS) {
    if (words.some((w) => key.includes(normalizeName(w)))) return semantic
  }
  return null
}

/**
 * 官方标准参数评分。对每个语义累加证据得分，取最高。
 * 依据 Cubism Standard Parameter List 的语义约定：
 *  - ParamMouthForm：+ 笑嘴 / - 怒嘴
 *  - ParamBrowLAngle / ParamBrowRForm / ParamBrowRAngle / ParamBrowLX / ParamBrowRX：- 表怒
 *  - ParamBrowLY / ParamBrowRY：+ 上扬（惊/喜），- 垂眉（难过）
 *  - ParamEyeLSmile / ParamEyeRSmile：+ 笑眼
 *  - ParamEyeLOpen / ParamEyeROpen：默认 1，>1 瞪眼（惊）
 *  - ParamMouthOpenY：+ 张嘴
 *  - ParamCheek：+ 脸红
 */
function scoreByParams(params: ExpressionParam[]): Partial<Record<ExpressionSemantic, number>> {
  const score: Partial<Record<ExpressionSemantic, number>> = {}
  const add = (s: ExpressionSemantic, n: number): void => {
    score[s] = (score[s] ?? 0) + n
  }
  const pick = (id: string): number => pickParam(params, id)

  // 黑化 / 压力 / 特殊表情 → 生气（murasame 类自定义参数）
  const stress =
    pick('ParamHeiHuaShadow') +
    pick('ParamXianTiaoChuXian') +
    pick('ParamTeShuEyeChuXian') +
    pick('ParamTeShuZuiCX') +
    pick('ParamMouthAngry') +
    pick('ParamMouthAngryLine')
  if (stress > 0.2) add('angry', stress)

  // 脸红
  if (pick('ParamCheek') > 0.2) add('blush', pick('ParamCheek'))

  // 嘴形：兼容标准 ParamMouthForm 与 mao 样例的 ParamMouthUp / ParamMouthDown / ParamMouthAngry(AngryLine)
  const mouthForm = pick('ParamMouthForm')
  const mouthUp = pick('ParamMouthUp')
  const mouthDown = pick('ParamMouthDown')
  const mouthAngry = pick('ParamMouthAngry') + pick('ParamMouthAngryLine')
  const smileMouth = Math.max(mouthForm, mouthUp) // 嘴角上挑
  const frownMouth = Math.max(mouthDown, -mouthForm, -mouthUp) // 嘴角下垂/下弯
  const angryMouth = Math.max(mouthAngry, -mouthForm) // 怒嘴线/负嘴形
  if (smileMouth > 0.2) add('happy', smileMouth)
  if (frownMouth > 0.2) add('sad', frownMouth)
  if (angryMouth > 0.2) add('angry', angryMouth)
  if (pick('ParamMouthOpenY') > 0.5) add('surprised', pick('ParamMouthOpenY'))

  // 眉
  const browLY = pick('ParamBrowLY')
  const browRY = pick('ParamBrowRY')
  if (browLY > 0.2 || browRY > 0.2) {
    add('surprised', (browLY + browRY) / 2)
    add('happy', 0.5)
  }
  if (browLY < -0.2 && browRY < -0.2) add('sad', (-browLY - browRY) / 2)
  const browAngry =
    -pick('ParamBrowLAngle') -
    pick('ParamBrowRAngle') -
    pick('ParamBrowLForm') -
    pick('ParamBrowRForm') -
    pick('ParamBrowLX') -
    pick('ParamBrowRX')
  if (browAngry > 0.4) add('angry', browAngry)

  // 眼
  if (pick('ParamEyeLSmile') > 0.2 || pick('ParamEyeRSmile') > 0.2) add('happy', 1)
  if (pick('ParamEyeLOpen') > 1.05 || pick('ParamEyeROpen') > 1.05) add('surprised', 1)

  return score
}

/** 由参数得分选出最高且超过阈值的语义 */
function classifyByParams(params: ExpressionParam[]): ExpressionSemantic | null {
  if (!params || params.length === 0) return null
  const score = scoreByParams(params)
  let best: ExpressionSemantic | null = null
  let bestVal = 0.25 // 低于该阈值视为不确定
  for (const s of EXPRESSION_SEMANTICS) {
    const v = score[s] ?? 0
    if (v > bestVal) {
      bestVal = v
      best = s
    }
  }
  if (best) return best
  // 无显著面部运动 → 正常脸
  const meaningful = params.filter((p) => p.Blend !== 'Multiply' && Math.abs(p.Value) > 0.01)
  return meaningful.length === 0 ? 'neutral' : null
}

/** 双层推理入口：表情名关键词优先，其次参数评分 */
function inferExpressionSemantic(
  params: ExpressionParam[],
  name?: string
): ExpressionSemantic | null {
  const byName = inferByName(name)
  if (byName) return byName
  return classifyByParams(params)
}

/** 读取模型的 .cdi3.json（DisplayInfo），构建「参数 Id → 作者内置显示名」映射。 */
async function loadParamNames(base: string, modelFileName: string): Promise<Record<string, string>> {
  const names: Record<string, string> = {}
  try {
    const res = await fetch(base + modelFileName)
    if (!res.ok) return names
    const json = (await res.json()) as { FileReferences?: { DisplayInfo?: string } }
    const rel = json.FileReferences?.DisplayInfo
    if (!rel) return names
    const cdiRes = await fetch(base + rel)
    if (!cdiRes.ok) return names
    const cdi = (await cdiRes.json()) as { Parameters?: Array<{ Id?: string; Name?: string }> }
    for (const p of cdi.Parameters ?? []) {
      if (p.Id && p.Name) names[p.Id] = p.Name
    }
  } catch {
    // 无 cdi3 或读取失败时忽略，不阻断整体
  }
  return names
}

/** 把表达式实际改动的参数映射为作者内置显示名，生成简洁描述（合并左右、截断）。 */
function describeExpression(paramNames: Record<string, string>, params: ExpressionParam[]): string {
  if (!params || params.length === 0) return ''
  const items: string[] = []
  for (const p of params) {
    const v = p.Value
    const changed = (p.Blend ?? 'Add') === 'Multiply' ? Math.abs(v - 1) > 0.01 : Math.abs(v) > 0.01
    if (!changed) continue
    const name = paramNames[p.Id]
    if (name) items.push(name.trim())
  }
  if (items.length === 0) return ''

  // 归一化空白后合并「左…/右…」同名参数：左目笑顔 + 右目笑顔 -> 目笑顔
  const norm = items.map((s) => s.replace(/\s+/g, ''))
  const merged: string[] = []
  const used = new Set<number>()
  for (let i = 0; i < norm.length; i++) {
    if (used.has(i)) continue
    let best = norm[i]
    for (let j = i + 1; j < norm.length; j++) {
      if (used.has(j)) continue
      const a = norm[i]
      const b = norm[j]
      if (
        (a.startsWith('左') && b.startsWith('右') && a.slice(1) === b.slice(1)) ||
        (a.startsWith('右') && b.startsWith('左') && a.slice(1) === b.slice(1))
      ) {
        best = b.slice(1)
        used.add(j)
      }
    }
    merged.push(best)
    used.add(i)
  }

  const cleaned = [...new Set(merged)].filter((s) => s.length > 0)
  if (cleaned.length === 0) return ''
  return cleaned.slice(0, 4).join('+') + (cleaned.length > 4 ? '…' : '')
}

/** 逐表情分析：构建「语义 → id」映射 + 每个表情的语义与作者内置参数描述。
 *  通过 live2d:// 协议读取表达式与 .cdi3.json；手动覆盖优先。
 */
async function analyzeExpressions(
  modelUrl: string,
  defs: Array<{ Name?: string; name?: string; File?: string }>,
  perEmotion?: Record<string, string>
): Promise<{
  emotionToId: Record<string, string>
  tiers: Array<{ semantic: ExpressionSemantic; level: ExpressionLevel; key: string }>
  semantics: Record<string, ExpressionSemantic>
  tiersById: Record<string, { semantic: ExpressionSemantic; level: ExpressionLevel }>
  descriptions: Record<string, string>
}> {
  let emotionToId: Record<string, string> = {}
  const semantics: Record<string, ExpressionSemantic> = {}
  const tiersById: Record<string, { semantic: ExpressionSemantic; level: ExpressionLevel }> = {}
  const descriptions: Record<string, string> = {}

  // 模型 URL 的基目录：live2d://murasame/Murasame.model3.json -> live2d://murasame/
  let base = ''
  let modelFileName = ''
  try {
    const u = new URL(modelUrl)
    const seg = u.pathname.slice(0, u.pathname.lastIndexOf('/'))
    base = `${u.protocol}//${u.host}${seg}/`
    modelFileName = u.pathname.split('/').pop() ?? ''
  } catch {
    return { emotionToId, tiers: [], semantics, tiersById, descriptions }
  }

  const paramNames = await loadParamNames(base, modelFileName)

  // 先逐表情收集：语义 + 名称强度等级 + 参数幅度，作为「同语义多档排序」的依据。
  const raw: Array<{
    id: string
    semantic: ExpressionSemantic
    nameLevel: ExpressionLevel
    magnitude: number
    desc: string
  }> = []
  for (const d of defs) {
    const id = d.Name ?? d.name
    if (!id || !d.File) continue
    try {
      const res = await fetch(base + d.File)
      if (!res.ok) continue
      const json = (await res.json()) as { Parameters?: ExpressionParam[] }
      const params = json.Parameters ?? []
      const semantic = inferExpressionSemantic(params, id)
      if (!semantic) continue
      raw.push({
        id,
        semantic,
        nameLevel: inferTierLevel(id),
        magnitude: magnitudeScore(params),
        desc: describeExpression(paramNames, params)
      })
    } catch {
      // 读取失败（如浏览器无 live2d://）时跳过该表达式，不阻断整体
      continue
    }
  }

  // 按语义分组，组内先按名称强度（升序，弱→强），再按参数幅度（升序）排序，
  // 从而把最弱的一档定为等级1，最强的档定为最高等级；同强度重复的表达式并入同一档（仅记首个 id）。
  let tiers: Array<{ semantic: ExpressionSemantic; level: ExpressionLevel; key: string }> = []
  for (const sem of EXPRESSION_SEMANTICS) {
    const group = raw.filter((r) => r.semantic === sem)
    if (group.length === 0) continue
    group.sort((a, b) => a.nameLevel - b.nameLevel || a.magnitude - b.magnitude)
    const seen = new Map<string, ExpressionLevel>()
    let nextLevel = 1
    for (const r of group) {
      const bucket = `${r.nameLevel}|${Math.round(r.magnitude * 10)}`
      let lvl = seen.get(bucket)
      if (lvl === undefined) {
        lvl = nextLevel++
        seen.set(bucket, lvl)
      }
      const key = tierKey(sem, lvl)
      if (!emotionToId[key]) emotionToId[key] = r.id
      semantics[r.id] = sem
      tiersById[r.id] = { semantic: sem, level: lvl }
      if (r.desc) descriptions[r.id] = r.desc
    }
    for (const lvl of seen.values()) {
      tiers.push({ semantic: sem, level: lvl, key: tierKey(sem, lvl) })
    }
  }

  // 应用「LLM 预填 / 人工修正」的表达式 → tierKey 映射：若某表达式被指定了别的语义或等级，以它为准覆盖启发式结果。
  if (perEmotion) {
    let changed = false
    for (const [id, tk] of Object.entries(perEmotion)) {
      const parsed = parseTierKey(tk)
      if (!parsed) continue
      const cur = tiersById[id]
      if (!cur) continue
      if (cur.semantic === parsed.semantic && cur.level === parsed.level) continue
      semantics[id] = parsed.semantic
      tiersById[id] = { semantic: parsed.semantic, level: parsed.level }
      changed = true
    }
    if (changed) {
      // 依据修正后的 tiersById 重建 emotionToId 与 tiers（同 tierKey 只保留首个 id）
      emotionToId = {}
      const tierMap = new Map<string, { semantic: ExpressionSemantic; level: ExpressionLevel }>()
      for (const [id, t] of Object.entries(tiersById)) {
        const key = tierKey(t.semantic, t.level)
        if (!emotionToId[key]) emotionToId[key] = id
        tierMap.set(key, t)
      }
      const order = new Map(EXPRESSION_SEMANTICS.map((s, i) => [s, i]))
      tiers = [...tierMap.entries()]
        .map(([key, t]) => ({ semantic: t.semantic, level: t.level, key }))
        .sort(
          (a, b) => (order.get(a.semantic) ?? 99) - (order.get(b.semantic) ?? 99) || a.level - b.level
        )
    }
  }

  return { emotionToId, tiers, semantics, tiersById, descriptions }
}

/** 独立重新识别模型表情：读取模型3.json 的 Expressions 定义后逐表情分析。
 *  复用加载模型时的同一套识别逻辑（analyzeExpressions），供设置页「重新识别模型表情」使用。
 *  仅为预览/刷新映射，不写入任何设置。 */
export async function reidentifyModelExpressions(
  modelUrl: string,
  perEmotion?: Record<string, string>
): Promise<{
  expressionIds: string[]
  autoEmotionToId: Record<string, string>
  tiers: Array<{ semantic: ExpressionSemantic; level: ExpressionLevel; key: string }>
  semantics: Record<string, ExpressionSemantic>
  tiersById: Record<string, { semantic: ExpressionSemantic; level: ExpressionLevel }>
  descriptions: Record<string, string>
}> {
  const res = await fetch(modelUrl)
  if (!res.ok) throw new Error(`无法读取模型配置：${modelUrl}`)
  const json = (await res.json()) as {
    FileReferences?: { Expressions?: Array<{ Name?: string; File?: string }> }
  }
  const defs: Array<{ Name?: string; name?: string; File?: string }> =
    json.FileReferences?.Expressions ?? []
  const { emotionToId, tiers, semantics, tiersById, descriptions } = await analyzeExpressions(
    modelUrl,
    defs,
    perEmotion
  )
  const expressionIds = defs
    .map((d) => d.Name ?? d.name)
    .filter((x): x is string => typeof x === 'string' && x.length > 0)
  return {
    expressionIds,
    autoEmotionToId: emotionToId,
    tiers,
    semantics,
    tiersById,
    descriptions
  }
}

/** 情绪 → 表情语义（含中英文归一化） */
const EMOTION_TO_SEMANTIC: Record<string, ExpressionSemantic> = {
  happy: 'happy',
  joy: 'happy',
  smile: 'happy',
  laugh: 'happy',
  sad: 'sad',
  cry: 'sad',
  unhappy: 'sad',
  angry: 'angry',
  mad: 'angry',
  surprised: 'surprised',
  shock: 'surprised',
  amazed: 'surprised',
  blush: 'blush',
  embarrassed: 'blush',
  shy: 'blush',
  normal: 'neutral',
  neutral: 'neutral',
  calm: 'neutral',
  idle: 'neutral',
  开心: 'happy',
  高兴: 'happy',
  笑: 'happy',
  难过: 'sad',
  伤心: 'sad',
  悲伤: 'sad',
  哭: 'sad',
  生气: 'angry',
  愤怒: 'angry',
  惊讶: 'surprised',
  震惊: 'surprised',
  害羞: 'blush',
  脸红: 'blush',
  平静: 'neutral',
  正常: 'neutral'
}

/** 强度情绪词 → tierKey（优先于基础 EMOTION_TO_SEMANTIC）。 */
const EMOTION_TO_TIER: Record<string, string> = {
  更开心: 'happy@2',
  非常开心: 'happy@3',
  超开心: 'happy@4',
  大喜: 'happy@2',
  狂喜: 'happy@4',
  开心极了: 'happy@3',
  更高兴: 'happy@2',
  非常高兴: 'happy@3',
  更生气: 'angry@2',
  非常生气: 'angry@3',
  超生气: 'angry@4',
  愤怒: 'angry@2',
  大怒: 'angry@2',
  狂怒: 'angry@4',
  暴怒: 'angry@4',
  更难过: 'sad@2',
  非常难过: 'sad@3',
  大哭: 'sad@2',
  痛哭: 'sad@4',
  悲痛: 'sad@2',
  更惊讶: 'surprised@2',
  非常惊讶: 'surprised@3',
  更害羞: 'blush@2',
  非常害羞: 'blush@3'
}

/** 动作 → 动作组（内置模型仅有 Idle / TapBody，统一落到 TapBody） */
export function mapActionToMotion(_action: string): string {
  return 'TapBody'
}

/**
 * 把情绪文本/LLM 标签值解析为 tierKey。
 * 规则：1) 已带等级（如 happy@2）直接命中；2) 查强度词表（更开心→happy@2）；
 *       3) 回退到基础语义映射（开心→happy）；4) 全不中返回 null。
 */
function resolveTierKey(emotion: string): string | null {
  const key = emotion.trim().toLowerCase()
  if (!key) return null
  const parsed = parseTierKey(key)
  if (parsed) return tierKey(parsed.semantic, parsed.level)
  const tier = EMOTION_TO_TIER[key]
  if (tier) return tier
  const semantic = EMOTION_TO_SEMANTIC[key]
  if (semantic) return tierKey(semantic, 1)
  return null
}

/**
 * 将一个"偏好的表达式 id"解析为当前模型可用的真实表达式 id。
 * - 精确匹配当前模型的表达式 id（兼容旧配置如 exp_04）；
 * - 无法精确命中时返回 null（不应用表情，还原模型本体默认脸），
 *   绝不按数字序号猜测——不同模型的序号语义并不一致。
 */
function resolveExpression(preferred: string, expressionIds: string[]): string | null {
  if (!preferred || expressionIds.length === 0) return null
  const hit = expressionIds.find((id) => id === preferred)
  return hit ?? null
}

export class Live2DController {
  private app: PIXI.Application | null = null
  private model: Live2DModel | null = null
  /** 当前模型实际支持的表达式 id 列表（按声明顺序） */
  private expressionIds: string[] = []
  /** 按语义+等级自动识别的「tierKey → 表达式 id」映射（不含手动覆盖，随模型加载重建） */
  private autoEmotionToId: Record<string, string> = {}
  /** 用户手动覆盖的「tierKey → 表达式 id」映射（随覆盖操作整体替换，含清除） */
  private overrideEmotionToId: Record<string, string> = {}
  /** 当前模型推断出的档位列表（语义 + 等级 + tierKey，设置页/指令用） */
  private tiers: Array<{ semantic: ExpressionSemantic; level: ExpressionLevel; key: string }> = []
  /** 每个表达式推断出的基础语义（设置页下拉标注用） */
  private expressionSemantics: Record<string, ExpressionSemantic> = {}
  /** 每个表达式推断出的档位（语义 + 等级，设置页下拉标注用） */
  private expressionTiers: Record<string, { semantic: ExpressionSemantic; level: ExpressionLevel }> = {}
  /** 每个表达式的作者内置参数描述（设置页下拉标注用） */
  private expressionDescriptions: Record<string, string> = {}

  /** 用户缩放系数（相对基准缩放 baseScale），初始 1 */
  private zoomLevel = 1
  /** 相对舞台中心的像素偏移（用户拖拽产生） */
  private panX = 0
  private panY = 0
  /** 基准缩放：加载后自动适配舞台，用户可在此基础上缩放 */
  private baseScale = 0.1
  private readonly MIN_ZOOM = 0.2
  private readonly MAX_ZOOM = 8

  get ready(): boolean {
    return this.model !== null
  }

  mount(canvas: HTMLCanvasElement, width: number, height: number): void {
    this.app = new PIXI.Application({
      view: canvas,
      width,
      height,
      backgroundAlpha: 0,
      antialias: true,
      autoDensity: true,
      resolution: window.devicePixelRatio || 1
    })
  }

  async loadModel(
    url: string,
    overrides?: Record<string, string>,
    perEmotion?: Record<string, string>
  ): Promise<void> {
    if (!this.app) throw new Error('Live2D 舞台尚未挂载')
    this.clearModel()
    const model = await Live2DModel.from(url, { autoInteract: false, autoUpdate: true })
    this.model = model
    this.app.stage.addChild(model)
    // 提取当前模型实际声明的表达式 id 列表 + 按参数特征识别语义
    const defs = ((model.internalModel?.settings as unknown as { expressions?: Array<{
      Name?: string
      name?: string
      File?: string
    }> })?.expressions ?? [])
    this.expressionIds = defs
      .map((d) => d.Name ?? d.name)
      .filter((x): x is string => typeof x === 'string' && x.length > 0)
    // 读取每个表情文件内容：构建「tierKey → 表达式 id」+ 每个表情的档位/语义与作者内置参数描述
    const { emotionToId, tiers, semantics, tiersById, descriptions } = await analyzeExpressions(
      url,
      defs,
      perEmotion
    )
    this.autoEmotionToId = emotionToId
    this.overrideEmotionToId = { ...(overrides ?? {}) }
    this.tiers = tiers
    this.expressionSemantics = semantics
    this.expressionTiers = tiersById
    this.expressionDescriptions = descriptions
    // 每次加载新模型后重置用户的缩放/平移
    this.zoomLevel = 1
    this.panX = 0
    this.panY = 0
    this.fit()
  }

  /** 当前模型实际声明的表达式 id 列表（设置页下拉用） */
  getExpressionIds(): string[] {
    return [...this.expressionIds]
  }

  /** 当前模型的「tierKey → 表达式 id」映射（自动推理 + 手动覆盖合并，设置页预览用） */
  getEmotionToId(): Record<string, string> {
    return { ...this.autoEmotionToId, ...this.overrideEmotionToId }
  }

  /** 当前模型自动推理的「tierKey → 表达式 id」映射（不含手动覆盖，设置页下拉回退用） */
  getAutoEmotionToId(): Record<string, string> {
    return { ...this.autoEmotionToId }
  }

  /** 当前模型推断出的档位列表（设置页/指令用） */
  getTiers(): Array<{ semantic: ExpressionSemantic; level: ExpressionLevel; key: string }> {
    return this.tiers.map((t) => ({ ...t }))
  }

  /** 每个表达式推断出的语义（设置页下拉标注用） */
  getExpressionSemantics(): Record<string, ExpressionSemantic> {
    return { ...this.expressionSemantics }
  }

  /** 每个表达式推断出的档位（设置页下拉标注用） */
  getExpressionTiers(): Record<string, { semantic: ExpressionSemantic; level: ExpressionLevel }> {
    return Object.fromEntries(
      Object.entries(this.expressionTiers).map(([k, v]) => [k, { ...v }])
    )
  }

  /** 每个表达式的作者内置参数描述（设置页下拉标注用） */
  getExpressionDescriptions(): Record<string, string> {
    return { ...this.expressionDescriptions }
  }

  /** 原地应用手动覆盖（不重载模型，避免闪烁）；空值即清除该语义的覆盖，回落到自动推理结果 */
  applyOverrides(overrides?: Record<string, string>): void {
    this.overrideEmotionToId = { ...(overrides ?? {}) }
  }

  /** 计算并应用模型的缩放与位置（统一入口） */
  private applyTransform(): void {
    if (!this.app || !this.model) return
    const model = this.model
    const w = this.app.screen.width
    const h = this.app.screen.height
    if (w <= 0 || h <= 0) return

    model.anchor.set(0.5, 0.5)
    model.scale.set(this.baseScale * this.zoomLevel)
    model.x = w / 2 + this.panX
    model.y = h / 2 + this.panY
  }

  private fit(): void {
    if (!this.app || !this.model) return
    const w = this.app.screen.width
    const h = this.app.screen.height
    if (w <= 0 || h <= 0) return

    // 模型载入后按舞台尺寸计算基准缩放，避免超出舞台
    const model = this.model
    model.anchor.set(0.5, 0.5)
    // 计算原始（scale=1）宽度/高度，取一个合适的基准缩放
    const naturalW = model.width
    const naturalH = model.height
    if (naturalW > 0) {
      this.baseScale = Math.min(Math.max(0.05, Math.min(w / naturalW, h / naturalH)), 0.5)
    } else {
      this.baseScale = 0.1
    }
    this.applyTransform()

    // 调试信息：请在 DevTools 控制台查看并反馈这几个值
    console.warn(
      '[live2d] stage=%sx%s model.width=%s model.height=%s baseScale=%s',
      Math.round(w),
      Math.round(h),
      model.width,
      model.height,
      this.baseScale
    )
  }

  resize(width: number, height: number): void {
    if (!this.app) return
    this.app.renderer.resize(width, height)
    this.applyTransform()
  }

  /** 以某个屏幕坐标（clientX/Y 相对舞台）为不动点缩放模型 */
  zoomAt(factor: number, clientX: number, clientY: number): void {
    if (!this.app || !this.model) return
    const oldZoom = this.zoomLevel
    const newZoom = Math.min(this.MAX_ZOOM, Math.max(this.MIN_ZOOM, oldZoom * factor))
    if (newZoom === oldZoom) return
    const w = this.app.screen.width
    const h = this.app.screen.height
    // 缩放前模型中心
    const oldCx = w / 2 + this.panX
    const oldCy = h / 2 + this.panY
    const ratio = newZoom / oldZoom
    // 保持光标下的点不动：新中心 = client - ratio * (client - 旧中心)
    this.panX = clientX - ratio * (clientX - oldCx) - w / 2
    this.panY = clientY - ratio * (clientY - oldCy) - h / 2
    this.zoomLevel = newZoom
    this.applyTransform()
  }

  /** 平移模型（像素） */
  addPan(dx: number, dy: number): void {
    if (!this.app || !this.model) return
    this.panX += dx
    this.panY += dy
    this.applyTransform()
  }

  /** 让模型注视舞台上的某个点（相对 canvas 左上角的坐标，与 PIXI 全局坐标一致）。
   *  复用 pixi-live2d-display 内置的平滑注视控制器：它会结合眼球 + 头部角度朝向目标，
   *  并通过 toModelPosition 正确换算缩放/平移后的模型坐标，远近只影响方向。 */
  focus(clientX: number, clientY: number): void {
    if (!this.model) return
    this.model.focus(clientX, clientY)
  }

  /** 让模型恢复正视前方（注视目标回中，带平滑过渡）。 */
  resetFocus(): void {
    const focusController = (this.model?.internalModel as unknown as {
      focusController?: { focus: (x: number, y: number, instant?: boolean) => void }
    })?.focusController
    focusController?.focus(0, 0)
  }

  async setEmotion(emotion: string): Promise<void> {
    if (!this.model) return
    // 情绪文本/标签 → tierKey → 该模型实际表达式 id（来自表情文件特征识别）
    const key = resolveTierKey(emotion)
    if (!key) return
    const id = this.overrideEmotionToId[key] ?? this.autoEmotionToId[key]
    if (id) await this.setExpression(id)
  }

  /** 直接设置表情：只做精确匹配（按当前模型声明的表达式 id），命中不了则还原本体默认脸。
   *  返回是否成功应用，供上层做默认表情回退判断。
   */
  async setExpression(preferred: string): Promise<boolean> {
    if (!this.model || !preferred) return false
    const id = resolveExpression(preferred, this.expressionIds)
    if (!id) return false
    try {
      await this.model.expression(id)
      return true
    } catch {
      // 模型无该表情时忽略
      return false
    }
  }

  async playMotion(group: string): Promise<void> {
    if (!this.model) return
    try {
      await this.model.motion(group)
    } catch {
      // 忽略不存在的动作组
    }
  }

  async idle(): Promise<void> {
    await this.playMotion('Idle')
  }

  private clearModel(): void {
    if (this.model && this.app) {
      this.app.stage.removeChild(this.model)
      this.model.destroy()
    }
    this.model = null
    this.expressionIds = []
    this.autoEmotionToId = {}
    this.overrideEmotionToId = {}
    this.tiers = []
    this.expressionSemantics = {}
    this.expressionTiers = {}
    this.expressionDescriptions = {}
  }

  destroy(): void {
    this.clearModel()
    if (this.app) {
      this.app.destroy(true, { children: true, texture: true, baseTexture: true })
      this.app = null
    }
  }
}
