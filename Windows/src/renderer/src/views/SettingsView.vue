<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import {
  NAlert,
  NButton,
  NCard,
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
import { EXPRESSION_SEMANTICS, EXPRESSION_SEMANTIC_LABELS } from '../services/live2dModel'
import type { ExpressionSemantic } from '../services/live2dModel'
import type { ApiKeySection, Persona, ThemeMode } from '@shared/types'

const settings = useSettingsStore()
const live2d = useLive2dStore()
const savedTip = ref(false)
const live2dTip = ref('')
const importing = ref(false)
const dragging = ref(false)
/** 密钥输入仅存于本地 ref，不回填真实值；保存时通过 setKey 写入主进程 */
const llmKeyInput = ref('')
const ttsKeyInput = ref('')
const sttKeyInput = ref('')

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

/** 读取某语义的手动覆盖值：空串表示「自动（跟随推理）」，与占位符一致 */
function emotionOverrideValue(semantic: ExpressionSemantic): string {
  const name = live2d.currentModelName()
  return settings.live2d.emotionOverrides[name]?.[semantic] ?? ''
}

function onEmotionOverrideChange(semantic: ExpressionSemantic, value: unknown): void {
  // clearable 时会收到 null；统一转成空串表示「自动」
  void live2d.setEmotionOverride(semantic, (value as string) ?? '')
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
  await live2d.switchModel(modelUrl)
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
    await live2d.switchModel(modelUrl)
    live2dTip.value = `已导入并启用模型：${modelUrl}`
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

async function save(): Promise<void> {
  await settings.save(JSON.parse(JSON.stringify(settings.data)))
  // 仅当用户本次输入了非空密钥时才覆盖写入，写入后清空本地输入
  if (llmKeyInput.value) {
    await settings.setKey('llm', llmKeyInput.value)
    llmKeyInput.value = ''
  }
  if (ttsKeyInput.value) {
    await settings.setKey('tts', ttsKeyInput.value)
    ttsKeyInput.value = ''
  }
  if (sttKeyInput.value) {
    await settings.setKey('stt', sttKeyInput.value)
    sttKeyInput.value = ''
  }
  savedTip.value = true
  window.setTimeout(() => (savedTip.value = false), 2000)
}

async function clearKey(section: ApiKeySection): Promise<void> {
  await settings.clearKey(section)
  if (section === 'llm') llmKeyInput.value = ''
  if (section === 'tts') ttsKeyInput.value = ''
  if (section === 'stt') sttKeyInput.value = ''
  savedTip.value = true
  window.setTimeout(() => (savedTip.value = false), 2000)
}

async function reset(): Promise<void> {
  await settings.reset()
  llmKeyInput.value = ''
  ttsKeyInput.value = ''
  sttKeyInput.value = ''
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
            <div class="key-row">
              <n-input
                v-model:value="llmKeyInput"
                type="password"
                show-password-on="click"
                :placeholder="settings.llm.hasApiKey ? '已配置（输入新值可更换）' : 'sk-...'"
              />
              <n-button
                v-if="settings.llm.hasApiKey"
                size="small"
                type="error"
                quaternary
                @click="clearKey('llm')"
              >
                清除
              </n-button>
            </div>
            <span v-if="settings.llm.hasApiKey" class="key-status">已配置，密钥不回显。</span>
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
              <div class="key-row">
                <n-input
                  v-model:value="ttsKeyInput"
                  type="password"
                  show-password-on="click"
                  :placeholder="settings.tts.hasApiKey ? '已配置（输入新值可更换）' : 'sk-...'"
                />
                <n-button
                  v-if="settings.tts.hasApiKey"
                  size="small"
                  type="error"
                  quaternary
                  @click="clearKey('tts')"
                >
                  清除
                </n-button>
              </div>
              <span v-if="settings.tts.hasApiKey" class="key-status">已配置，密钥不回显。</span>
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
              <div class="key-row">
                <n-input
                  v-model:value="sttKeyInput"
                  type="password"
                  show-password-on="click"
                  :placeholder="settings.stt.hasApiKey ? '已配置（输入新值可更换）' : 'sk-...'"
                />
                <n-button
                  v-if="settings.stt.hasApiKey"
                  size="small"
                  type="error"
                  quaternary
                  @click="clearKey('stt')"
                >
                  清除
                </n-button>
              </div>
              <span v-if="settings.stt.hasApiKey" class="key-status">已配置，密钥不回显。</span>
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
            <n-switch v-model:value="settings.live2d.enabled" />
          </n-form-item>
          <n-form-item label="模型">
            <n-select
              :value="settings.live2d.modelUrl"
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
          <n-alert v-if="live2dTip" :type="live2dTip.startsWith('已导入') ? 'success' : 'warning'" size="small">
            {{ live2dTip }}
          </n-alert>
          <n-form-item label="模型地址">
            <n-input
              v-model:value="settings.live2d.modelUrl"
              placeholder="live2d://natori/Natori.model3.json"
            />
          </n-form-item>
          <template v-if="live2d.expressionIds.length > 0">
            <n-divider title-placement="left">表情映射（可选）</n-divider>
            <p class="emotion-map-hint">
              自动识别模型的面部表情；若识别不准，可手动为每种情绪指定一个表情。当前生效：
              <template v-for="s in EXPRESSION_SEMANTICS" :key="s">
                <span class="emotion-map-chip">{{ EXPRESSION_SEMANTIC_LABELS[s] }}:{{ live2d.emotionToId[s] ?? '无' }}</span>
              </template>
            </p>
            <n-form-item v-for="s in EXPRESSION_SEMANTICS" :key="s" :label="EXPRESSION_SEMANTIC_LABELS[s]">
              <n-select
                :value="emotionOverrideValue(s)"
                :options="expressionOptions"
                clearable
                placeholder="自动（跟随推理）"
                style="width: 100%"
                @update:value="(v) => onEmotionOverrideChange(s, v)"
              />
            </n-form-item>
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
.key-row {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
}

.key-row .n-input {
  flex: 1;
}

.key-status {
  font-size: 12px;
  color: var(--text-3);
  line-height: 1.4;
}

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
</style>
