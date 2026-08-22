<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import {
  NAlert,
  NButton,
  NCard,
  NCheckbox,
  NCheckboxGroup,
  NDivider,
  NForm,
  NFormItem,
  NInput,
  NInputNumber,
  NSelect,
  NSwitch
} from 'naive-ui'
import { useSettingsStore } from '../stores/settings'
import { useLive2dStore } from '../stores/live2d'
import { useEsp32Store } from '../stores/esp32'
import { usePerfStore } from '../stores/perf'
import {
  EXPRESSION_SEMANTICS,
  EXPRESSION_SEMANTIC_LABELS,
  mergeTiers,
  tierLabel,
  tierKey,
  parseTierKey
} from '../services/live2dModel'
import type { ExpressionSemantic, TierEntry } from '../services/live2dModel'
import ApiKeySetting from '../components/ApiKeySetting.vue'
import { DEFAULT_SETTINGS } from '@shared/types'
import type { Esp32Device, Live2dConfig, Persona, PublicAppSettings, ThemeMode } from '@shared/types'

const settings = useSettingsStore()
const live2d = useLive2dStore()
const esp32 = useEsp32Store()
const perf = usePerfStore()
const savedTip = ref(false)
const live2dTip = ref('')
const importing = ref(false)
const dragging = ref(false)
const reidentifying = ref(false)
const aiIdentifying = ref(false)
const selectedDevice = ref<string | null>(null)

const deviceOptions = computed(() =>
  esp32.devices.map((d: Esp32Device) => ({
    label: `${d.id}${d.name ? ` · ${d.name}` : ''}`,
    value: d.id
  }))
)

const esp32StateLabel = computed(() => {
  const map: Record<string, string> = {
    idle: '未连接',
    connecting: '连接中',
    connected: '已连接',
    reconnecting: '重连中',
    error: '错误'
  }
  return map[esp32.status.state] ?? esp32.status.state
})

const perfMetricOptions = [
  { label: 'CPU 使用率', value: 'cpu' },
  { label: 'GPU 使用率', value: 'gpu' },
  { label: '内存使用率', value: 'memory' }
]

function fmtPercent(v: number | null | undefined): string {
  return v == null ? '—' : `${v}%`
}

async function discoverDevices(): Promise<void> {
  await esp32.discover()
}

function onSelectDevice(id: string | null): void {
  if (!id) return
  const d = esp32.devices.find((x) => x.id === id)
  if (!d) return
  settings.esp32.deviceId = d.id
  settings.esp32.host = d.host
  settings.esp32.tcpPort = d.tcpPort || settings.esp32.tcpPort
  settings.esp32.wsPort = d.wsPort || settings.esp32.wsPort
  selectedDevice.value = null
}

async function onToggleConnect(): Promise<void> {
  await settings.save({ esp32: JSON.parse(JSON.stringify(settings.esp32)) })
  if (esp32.connected) await esp32.disconnect()
  else await esp32.connect()
}

async function onPerfToggle(v: boolean): Promise<void> {
  settings.perf.enabled = v
  await settings.save({ perf: JSON.parse(JSON.stringify(settings.perf)) })
  if (v) await perf.start()
  else await perf.stop()
}

/** 深拷贝 Live2D 配置，避免草稿与 store 共享同一个响应式对象 */
function cloneLive2d(cfg: Live2dConfig): Live2dConfig {
  return JSON.parse(JSON.stringify(cfg))
}

/** Live2D 模块草稿：编辑后需点「保存设置」才持久化，与全局保存解耦 */
const live2dDraft = ref<Live2dConfig>(cloneLive2d(settings.live2d))

// 设置异步加载完成后同步一次草稿，避免首次进入时草稿仍停留在默认值
watch(
  () => settings.loaded,
  (loaded) => {
    if (loaded) live2dDraft.value = cloneLive2d(settings.live2d)
  },
  { immediate: true }
)

const engineOptions = [
  { label: '系统语音（免费）', value: 'system' },
  { label: 'OpenAI 兼容（云端）', value: 'openai' }
]
const themeOptions: Array<{ label: string; value: ThemeMode }> = [
  { label: '浅色', value: 'light' },
  { label: '深色', value: 'dark' }
]

const personaOptions = computed(() =>
  settings.personas.map((p) => ({ label: `${p.avatar} ${p.name}`, value: p.id }))
)

const modelOptions = computed(() =>
  live2d.models.map((m) => ({ label: m.name, value: m.modelUrl }))
)

/** 表情选项标签：id · 语义 (作者内置参数描述)，缺位时优雅降级 */
function expressionLabel(id: string): string {
  const semantic = live2d.expressionSemantics[id]
  const desc = live2d.expressionDescriptions[id]
  let label = id
  const semLabel = semantic ? EXPRESSION_SEMANTIC_LABELS[semantic as ExpressionSemantic] : ''
  if (semLabel) label += ` · ${semLabel}`
  if (desc) label += `（${desc}）`
  return label
}

/** 当前模型的表达式 id 下拉选项（手动表情映射用） */
const expressionOptions = computed(() =>
  live2d.expressionIds.map((id) => ({ label: expressionLabel(id), value: id }))
)

/** 当前模型名（目录名）：live2d://murasame/Murasame.model3.json -> murasame（取自草稿，与保存解耦） */
function draftModelName(): string {
  const raw = live2dDraft.value.modelUrl.replace(/^live2d:\/\//, '')
  return raw.split('/')[0] ?? ''
}

/** 当前生效档位列表（自动识别 + 用户新增 - 用户移除），用于映射区渲染 */
const pendingTiers = computed<TierEntry[]>(() => {
  const name = draftModelName()
  const additions = live2dDraft.value.tierAdditions[name] ?? []
  const removals = live2dDraft.value.tierRemovals[name] ?? []
  return mergeTiers(live2d.autoTiers, additions, removals)
})

/** 档位显示标签：开心 / 更开心 / 非常开心 */
function tierEntryLabel(t: TierEntry): string {
  return tierLabel(t.semantic, t.level)
}

/** 读取某档位（tierKey）的手动覆盖值：空串表示「自动（跟随推理）」 */
function emotionOverrideValue(tier: string): string {
  return live2dDraft.value.emotionOverrides[draftModelName()]?.[tier] ?? ''
}

/** 占位符：无覆盖时显示自动识别生效的表情，既是「自动」又与「当前生效」保持一致 */
function emotionPlaceholder(tier: string): string {
  const autoId = live2d.autoEmotionToId[tier] ?? live2d.emotionToId[tier]
  return autoId ? `自动（跟随推理）：${expressionLabel(autoId)}` : '自动（跟随推理）'
}

/** 下拉显示值：优先手动覆盖，其次自动推理生效值，确保下拉始终展示「当前生效」的表情 */
function emotionDisplayValue(tier: string): string {
  return (
    emotionOverrideValue(tier) ||
    live2d.autoEmotionToId[tier] ||
    live2d.emotionToId[tier] ||
    ''
  )
}

function onEmotionOverrideChange(tier: string, value: unknown): void {
  // clearable 时会收到 null；统一转成空串表示「自动」，先写入草稿
  const name = draftModelName()
  const existing = { ...(live2dDraft.value.emotionOverrides[name] ?? {}) }
  const id = (value as string) ?? ''
  if (id) existing[tier] = id
  else delete existing[tier]
  live2dDraft.value.emotionOverrides = { ...live2dDraft.value.emotionOverrides, [name]: existing }
}

/** 添加档位：目标语义（排除 neutral 默认脸）与待选项 */
const addTierSemantic = ref<ExpressionSemantic>('happy')
const addableSemantics = computed(() =>
  EXPRESSION_SEMANTICS.filter((s) => s !== 'neutral').map((s) => ({
    label: EXPRESSION_SEMANTIC_LABELS[s],
    value: s
  }))
)

/** 手动为某语义新增一档（下一等级），写入草稿待保存 */
function addTier(semantic: ExpressionSemantic): void {
  const name = draftModelName()
  const maxLevel = pendingTiers.value
    .filter((t) => t.semantic === semantic)
    .reduce((m, t) => Math.max(m, t.level), 1)
  const next = tierKey(semantic, maxLevel + 1)
  const additions = [...(live2dDraft.value.tierAdditions[name] ?? []), next]
  live2dDraft.value.tierAdditions = {
    ...live2dDraft.value.tierAdditions,
    [name]: [...new Set(additions)]
  }
}

/** 手动删除一档：清除其覆盖；自动档并入移除列表，用户新增档从新增列表剔除 */
function removeTier(tier: string): void {
  const name = draftModelName()
  if (!parseTierKey(tier)) return
  const autoHas = live2d.autoTiers.some((t) => t.key === tier)
  const ov = { ...(live2dDraft.value.emotionOverrides[name] ?? {}) }
  delete ov[tier]
  const additions = (live2dDraft.value.tierAdditions[name] ?? []).filter((k) => k !== tier)
  let removals = live2dDraft.value.tierRemovals[name] ?? []
  if (autoHas) removals = [...new Set([...removals, tier])]
  live2dDraft.value.emotionOverrides = { ...live2dDraft.value.emotionOverrides, [name]: ov }
  live2dDraft.value.tierAdditions = { ...live2dDraft.value.tierAdditions, [name]: additions }
  live2dDraft.value.tierRemovals = { ...live2dDraft.value.tierRemovals, [name]: removals }
}

onMounted(() => {
  void live2d.loadModels()
})

function selectPersona(id: string): void {
  void settings.setActivePersona(id)
}

function onThemeChange(theme: ThemeMode): void {
  void settings.setTheme(theme)
}

async function onModelChange(modelUrl: string): Promise<void> {
  // 只写草稿，点「保存设置」后生效；选择模型时视为需启用
  live2dDraft.value.modelUrl = modelUrl
  live2dDraft.value.enabled = true
  // 切换模型后立即重新解析该模型的表情，刷新「表情映射」模块预览，无需等「保存设置」
  live2dTip.value = ''
  reidentifying.value = true
  try {
    await live2d.reidentifyExpressions(modelUrl)
  } catch (err) {
    live2dTip.value = err instanceof Error ? err.message : String(err)
  } finally {
    reidentifying.value = false
  }
}

async function onReidentifyExpressions(): Promise<void> {
  if (reidentifying.value) return
  reidentifying.value = true
  live2dTip.value = ''
  try {
    await live2d.reidentifyExpressions(live2dDraft.value.modelUrl)
    live2dTip.value = '已重新识别模型表情'
  } catch (err) {
    live2dTip.value = err instanceof Error ? err.message : String(err)
  } finally {
    reidentifying.value = false
  }
}

/** 用 LLM 预填当前模型的「表情 → 情绪@等级」标注并刷新映射 */
async function onAiAnalyze(): Promise<void> {
  if (aiIdentifying.value) return
  if (!settings.llm.hasApiKey) {
    live2dTip.value = '请先在「大模型」中配置 API Key，再使用 AI 识别'
    return
  }
  aiIdentifying.value = true
  live2dTip.value = ''
  try {
    await live2d.analyzeWithLlm()
    live2dTip.value = '已用 AI 重新识别表情'
  } catch (err) {
    live2dTip.value = err instanceof Error ? err.message : String(err)
  } finally {
    aiIdentifying.value = false
  }
}

async function onDropModel(e: DragEvent): Promise<void> {
  e.preventDefault()
  if (importing.value) return
  const file = e.dataTransfer?.files?.[0]
  if (!file) return
  importing.value = true
  live2dTip.value = ''
  try {
    const path = window.api.live2d.getPathForFile(file)
    const { models, modelUrl } = await window.api.live2d.importModel(path)
    live2d.models = models
    live2dDraft.value.modelUrl = modelUrl
    live2dDraft.value.enabled = true
    live2dTip.value = `已导入模型：${modelUrl}（点「保存设置」后启用）`
  } catch (err) {
    live2dTip.value = err instanceof Error ? err.message : String(err)
  } finally {
    importing.value = false
  }
}

function addPersona(): void {
  const id = `p-${Date.now().toString(36)}`
  const persona: Persona = {
    id,
    name: '新人格',
    avatar: '🤖',
    systemPrompt: '你是一个友好的虚拟助手。',
    defaultExpression: 'Normal'
  }
  void settings.addPersona(persona).then(() => settings.setActivePersona(id))
}

function removeActivePersona(): void {
  if (settings.personas.length <= 1) return
  void settings.deletePersona(settings.activePersonaId)
}

async function saveLive2d(): Promise<void> {
  await settings.save({ live2d: cloneLive2d(live2dDraft.value) })
  live2dTip.value = '已保存 Live2D 设置'
}

async function resetLive2d(): Promise<void> {
  live2dDraft.value = cloneLive2d(DEFAULT_SETTINGS.live2d)
  await settings.save({ live2d: cloneLive2d(DEFAULT_SETTINGS.live2d) })
  live2dTip.value = '已恢复 Live2D 默认设置'
}

async function save(): Promise<void> {
  // 全局保存时把 Live2D 草稿一并写入，保证两处保存口径一致
  const payload = JSON.parse(JSON.stringify(settings.data)) as PublicAppSettings
  payload.live2d = cloneLive2d(live2dDraft.value)
  await settings.save(payload)
  savedTip.value = true
  window.setTimeout(() => (savedTip.value = false), 2000)
}

async function reset(): Promise<void> {
  await settings.reset()
  live2dDraft.value = cloneLive2d(DEFAULT_SETTINGS.live2d)
  savedTip.value = true
  window.setTimeout(() => (savedTip.value = false), 2000)
}
</script>

<template>
  <div class="settings-view">
    <div class="settings-head">
      <h2>设置</h2>
      <p>在此集中配置大模型、语音与机器人人格，保存后即时生效。</p>
    </div>

    <n-alert v-if="savedTip" type="success" class="settings-tip">已保存</n-alert>

    <div class="settings-cards">
      <n-card title="大模型（LLM）" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="Base URL">
            <n-input v-model:value="settings.llm.baseUrl" placeholder="https://api.openai.com/v1" />
          </n-form-item>
          <n-form-item label="API Key">
            <ApiKeySetting section="llm" />
          </n-form-item>
          <n-form-item label="模型">
            <n-input v-model:value="settings.llm.model" placeholder="gpt-4o-mini" />
          </n-form-item>
          <div class="form-row">
            <n-form-item label="Temperature">
              <n-input-number
                v-model:value="settings.llm.temperature"
                :min="0"
                :max="2"
                :step="0.1"
              />
            </n-form-item>
            <n-form-item label="最大回复 Token">
              <n-input-number v-model:value="settings.llm.maxTokens" :min="1" :step="256" />
            </n-form-item>
          </div>
          <n-form-item label="上下文上限（估算 Token）">
            <n-input-number
              v-model:value="settings.llm.maxContextTokens"
              :min="512"
              :step="512"
            />
          </n-form-item>
        </n-form>
      </n-card>

      <n-card title="文转语音（TTS）" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="引擎">
            <n-select v-model:value="settings.tts.engine" :options="engineOptions" />
          </n-form-item>
          <template v-if="settings.tts.engine === 'openai'">
            <n-form-item label="Base URL">
              <n-input v-model:value="settings.tts.baseUrl" placeholder="https://api.openai.com/v1" />
            </n-form-item>
            <n-form-item label="API Key">
              <ApiKeySetting section="tts" />
            </n-form-item>
            <n-form-item label="模型">
              <n-input v-model:value="settings.tts.model" placeholder="tts-1" />
            </n-form-item>
            <n-form-item label="音色">
              <n-input v-model:value="settings.tts.voice" placeholder="alloy / echo / fable ..." />
            </n-form-item>
          </template>
          <div class="form-row">
            <n-form-item label="语速">
              <n-input-number
                v-model:value="settings.tts.speed"
                :min="0.25"
                :max="4"
                :step="0.05"
              />
            </n-form-item>
            <n-form-item label="回复后自动朗读">
              <n-switch v-model:value="settings.tts.autoSpeak" />
            </n-form-item>
          </div>
        </n-form>
      </n-card>

      <n-card title="语音转文字（STT）" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="引擎">
            <n-select v-model:value="settings.stt.engine" :options="engineOptions" />
          </n-form-item>
          <template v-if="settings.stt.engine === 'openai'">
            <n-form-item label="Base URL">
              <n-input v-model:value="settings.stt.baseUrl" placeholder="https://api.openai.com/v1" />
            </n-form-item>
            <n-form-item label="API Key">
              <ApiKeySetting section="stt" />
            </n-form-item>
            <n-form-item label="模型">
              <n-input v-model:value="settings.stt.model" placeholder="whisper-1" />
            </n-form-item>
          </template>
          <n-form-item label="语言">
            <n-input v-model:value="settings.stt.language" placeholder="zh-CN" />
          </n-form-item>
        </n-form>
      </n-card>

      <n-card title="机器人人格" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="当前人格">
            <div class="persona-select-row">
              <n-select
                v-model:value="settings.activePersonaId"
                :options="personaOptions"
                class="persona-select"
                @update:value="selectPersona"
              />
              <n-button size="small" @click="addPersona()">新增</n-button>
              <n-button
                size="small"
                :disabled="settings.personas.length <= 1"
                @click="removeActivePersona()"
              >
                删除
              </n-button>
            </div>
          </n-form-item>
          <div class="form-row">
            <n-form-item label="名称">
              <n-input v-model:value="settings.persona.name" />
            </n-form-item>
            <n-form-item label="头像（Emoji）">
              <n-input v-model:value="settings.persona.avatar" />
            </n-form-item>
          </div>
          <n-form-item label="默认表情">
            <n-input v-model:value="settings.persona.defaultExpression" placeholder="Normal / Smile" />
          </n-form-item>
          <n-form-item label="系统提示词（人设）">
            <n-input
              v-model:value="settings.persona.systemPrompt"
              type="textarea"
              :rows="6"
              placeholder="描述机器人的性格、语气与行为…"
            />
          </n-form-item>
        </n-form>
      </n-card>

      <n-card title="Live2D 形象" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="启用 Live2D">
            <n-switch v-model:value="live2dDraft.enabled" />
          </n-form-item>
          <n-form-item label="模型">
            <n-select
              :value="live2dDraft.modelUrl"
              :options="modelOptions"
              :loading="!live2d.modelsLoaded"
              placeholder="选择 Live2D 模型"
              @update:value="onModelChange"
            />
          </n-form-item>
          <n-form-item label="导入模型">
            <div
              class="dropzone"
              :class="{ 'is-dragging': dragging }"
              @dragover.prevent="dragging = true"
              @dragleave="dragging = false"
              @drop.prevent="onDropModel"
            >
              <span class="dropzone-icon">📂</span>
              <span class="dropzone-text">把 Live2D 模型文件夹拖到此处</span>
              <span class="dropzone-sub">将自动复制到用户目录并启用（*.model3.json）</span>
            </div>
          </n-form-item>
          <n-alert v-if="live2dTip" :type="live2dTip.startsWith('已') ? 'success' : 'warning'" size="small">
            {{ live2dTip }}
          </n-alert>
          <n-form-item label="模型地址">
            <n-input
              v-model:value="live2dDraft.modelUrl"
              placeholder="live2d://natori/Natori.model3.json"
            />
          </n-form-item>
          <div class="live2d-identify">
            <span class="identify-hint">
              按当前所选模型重新解析并推断每个表情的语义
              <template v-if="!settings.llm.hasApiKey">
                <br />「用 AI 重新识别」需先在大模型配置中填写 API Key
              </template>
            </span>
            <div class="live2d-identify-btns">
              <n-button size="small" :loading="reidentifying" @click="onReidentifyExpressions()">
                重新识别模型表情
              </n-button>
              <n-button
                size="small"
                type="primary"
                :loading="aiIdentifying"
                :disabled="!settings.llm.hasApiKey"
                @click="onAiAnalyze()"
              >
                用 AI 重新识别
              </n-button>
            </div>
          </div>
          <template v-if="live2d.expressionIds.length > 0">
            <n-divider title-placement="left">表情映射（可选）</n-divider>
            <p class="emotion-map-hint">
              自动识别模型的面部表情（含强度等级）；若识别不准，可手动为每个档位指定一个表情。当前生效：
              <template v-for="t in pendingTiers" :key="t.key">
                <span class="emotion-map-chip">{{ tierEntryLabel(t) }}:{{ live2d.emotionToId[t.key] ?? '无' }}</span>
              </template>
            </p>
            <div v-for="t in pendingTiers" :key="t.key" class="tier-row">
              <span class="tier-label" :title="tierEntryLabel(t)">{{ tierEntryLabel(t) }}</span>
              <n-select
                :value="emotionDisplayValue(t.key)"
                :options="expressionOptions"
                clearable
                :placeholder="emotionPlaceholder(t.key)"
                style="flex: 1 1 auto; min-width: 0"
                @update:value="(v) => onEmotionOverrideChange(t.key, v)"
              />
              <n-button size="small" quaternary type="error" class="tier-del" @click="removeTier(t.key)">
                删除
              </n-button>
            </div>
            <div class="tier-row tier-add-row">
              <span class="tier-label">新增档位</span>
              <n-select
                v-model:value="addTierSemantic"
                :options="addableSemantics"
                placeholder="选择情绪语义"
                style="flex: 1 1 auto; min-width: 0"
              />
              <n-button size="small" class="tier-del" @click="addTier(addTierSemantic)">添加</n-button>
            </div>
          </template>
          <n-alert
            v-if="live2d.emotionInstruction"
            type="info"
            title="表情提示词（自动注入）"
            size="small"
            style="margin-top: 12px"
          >
            {{ live2d.emotionInstruction }}
          </n-alert>
          <div class="live2d-actions">
            <n-button size="small" type="primary" :loading="settings.saving" @click="saveLive2d()">
              保存设置
            </n-button>
            <n-button size="small" @click="resetLive2d()">恢复默认</n-button>
          </div>
        </n-form>
      </n-card>

      <n-card title="ESP32 连接" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="启用 ESP32">
            <n-switch v-model:value="settings.esp32.enabled" />
          </n-form-item>
          <n-form-item label="识别码（硬件 ID）">
            <div class="esp32-discover">
              <n-input
                v-model:value="settings.esp32.deviceId"
                placeholder="如 esp32_xxxx / MAC 地址"
              />
              <n-button size="small" @click="discoverDevices()">发现设备</n-button>
            </div>
          </n-form-item>
          <n-form-item v-if="esp32.devices.length > 0" label="已发现设备">
            <n-select
              v-model:value="selectedDevice"
              :options="deviceOptions"
              placeholder="选择设备以自动填充连接参数"
              clearable
              @update:value="onSelectDevice"
            />
          </n-form-item>
          <n-form-item label="主机地址">
            <n-input
              v-model:value="settings.esp32.host"
              placeholder="192.168.1.x（未发现设备时手动填写）"
            />
          </n-form-item>
          <div class="form-row">
            <n-form-item label="TCP 端口">
              <n-input-number v-model:value="settings.esp32.tcpPort" :min="1" :max="65535" />
            </n-form-item>
            <n-form-item label="WebSocket 端口">
              <n-input-number v-model:value="settings.esp32.wsPort" :min="1" :max="65535" />
            </n-form-item>
          </div>
          <n-form-item label="重连基础间隔（毫秒）">
            <n-input-number
              v-model:value="settings.esp32.reconnectIntervalMs"
              :min="100"
              :step="100"
            />
          </n-form-item>
          <div class="esp32-status">
            <span class="esp32-state" :class="esp32.status.state">
              状态：{{ esp32StateLabel }}
            </span>
            <span v-if="esp32.status.message" class="esp32-msg">{{ esp32.status.message }}</span>
          </div>
          <n-button size="small" type="primary" @click="onToggleConnect()">
            {{ esp32.connected ? '断开' : '连接' }}
          </n-button>
          <n-alert
            v-if="esp32.error || esp32.lastText"
            type="info"
            size="small"
            style="margin-top: 12px"
          >
            <template v-if="esp32.error">{{ esp32.error }}</template>
            <template v-else>ESP32 消息：{{ esp32.lastText }}</template>
          </n-alert>
        </n-form>
      </n-card>

      <n-card title="性能监测" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="启用监测">
            <n-switch :value="settings.perf.enabled" @update:value="onPerfToggle" />
          </n-form-item>
          <n-form-item label="监测项目">
            <n-checkbox-group v-model:value="settings.perf.metrics">
              <n-checkbox
                v-for="m in perfMetricOptions"
                :key="m.value"
                :value="m.value"
                :label="m.label"
              />
            </n-checkbox-group>
          </n-form-item>
          <n-form-item label="采样间隔（毫秒）">
            <n-input-number v-model:value="settings.perf.intervalMs" :min="200" :step="100" />
          </n-form-item>
          <n-form-item label="通过 WebSocket 推送">
            <n-switch v-model:value="settings.perf.pushOverWs" />
          </n-form-item>
          <div class="perf-preview">
            <span>CPU：{{ fmtPercent(perf.latest?.cpu) }}</span>
            <span>GPU：{{ fmtPercent(perf.latest?.gpu) }}</span>
            <span>内存：{{ fmtPercent(perf.latest?.memory) }}</span>
          </div>
        </n-form>
      </n-card>

      <n-card title="外观" size="small">
        <n-form label-placement="top" :show-feedback="false">
          <n-form-item label="主题">
            <n-select
              :value="settings.theme"
              :options="themeOptions"
              @update:value="onThemeChange"
            />
          </n-form-item>
        </n-form>
      </n-card>
    </div>

    <n-divider />
    <div class="settings-actions">
      <n-button type="primary" :loading="settings.saving" @click="save()">保存设置</n-button>
      <n-button @click="reset()">恢复默认</n-button>
    </div>
  </div>
</template>

<style scoped>
.emotion-map-hint {
  margin: 0 0 8px;
  font-size: 12px;
  color: var(--text-3);
  line-height: 1.8;
}

.emotion-map-chip {
  display: inline-block;
  margin-right: 8px;
  padding: 1px 6px;
  border-radius: 8px;
  background: var(--glass-4);
  color: var(--text-2);
  white-space: nowrap;
}

.tier-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.tier-row:last-child {
  margin-bottom: 0;
}

.tier-label {
  flex: 0 0 72px;
  min-width: 0;
  font-size: 13px;
  color: var(--text-2);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.tier-del {
  flex: 0 0 auto;
}

.tier-add-row {
  margin-top: 4px;
}

.live2d-actions {
  display: flex;
  gap: 8px;
  margin-top: 12px;
}

.live2d-identify {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 12px;
}

.identify-hint {
  font-size: 12px;
  color: var(--text-3);
}

.live2d-identify-btns {
  display: flex;
  gap: 8px;
  flex-shrink: 0;
}

.esp32-discover {
  display: flex;
  gap: 8px;
}

.esp32-status {
  display: flex;
  flex-direction: column;
  gap: 4px;
  margin-bottom: 12px;
  font-size: 13px;
}

.esp32-state {
  color: var(--text-2);
}

.esp32-state.connected {
  color: #3fb68b;
}

.esp32-state.error {
  color: #ff6b81;
}

.esp32-msg {
  color: var(--text-3);
  font-size: 12px;
}

.perf-preview {
  display: flex;
  gap: 16px;
  margin-top: 8px;
  font-size: 13px;
  color: var(--text-2);
}
</style>
