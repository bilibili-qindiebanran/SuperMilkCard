<script setup lang="ts">
import { computed, ref } from 'vue'
import type {
  ApiKeySection,
  ApiKeyTestResult,
  PublicLlmConfig,
  PublicSttConfig,
  PublicTtsConfig
} from '@shared/types'
import { useSettingsStore } from '../stores/settings'

const props = withDefaults(
  defineProps<{
    section: ApiKeySection
    label?: string
    hint?: string
  }>(),
  {
    label: 'API Key',
    hint: '请输入 API Key；格式通常以 sk- 开头，建议 ≥ 16 位，不含空格。'
  }
)

const settings = useSettingsStore()
const show = ref(false)
const tempKey = ref('')
const testLoading = ref(false)
const testResult = ref<ApiKeyTestResult | null>(null)

/** 各模块的脱敏公共配置（不含真实密钥） */
const cfg = computed<PublicLlmConfig | PublicTtsConfig | PublicSttConfig>(
  () => settings.data[props.section]
)
const hasApiKey = computed(() => cfg.value.hasApiKey)
const maskedKey = computed(() => cfg.value.maskedKey)

/** 在线测试时生效的 Base URL：本模块优先，空则回退 LLM */
const effectiveBaseUrl = computed(() => {
  const cfgBase = settings.data[props.section].baseUrl
  return (cfgBase || settings.llm.baseUrl).trim()
})

/** 弹窗内格式校验：非空、长度 ≥ 8、不含空白字符 */
const validationError = computed(() => {
  const v = tempKey.value.trim()
  if (!v) return ''
  if (v.length < 8) return 'API Key 长度过短（至少 8 位）'
  if (/\s/.test(v)) return 'API Key 不能包含空白字符'
  return ''
})
const canConfirm = computed(() => !validationError.value && tempKey.value.trim() !== '')
const canTest = computed(() => tempKey.value.trim() !== '')

function open(): void {
  tempKey.value = ''
  testResult.value = null
  show.value = true
}

function close(): void {
  show.value = false
  tempKey.value = ''
  testResult.value = null
}

async function onConfirm(): Promise<void> {
  if (!canConfirm.value) return
  await settings.setKey(props.section, tempKey.value.trim())
  close()
}

async function onTest(): Promise<void> {
  if (!canTest.value) return
  testLoading.value = true
  testResult.value = null
  try {
    testResult.value = await settings.testKey(
      props.section,
      tempKey.value.trim(),
      effectiveBaseUrl.value
    )
  } catch (err) {
    testResult.value = { ok: false, message: err instanceof Error ? err.message : String(err) }
  } finally {
    testLoading.value = false
  }
}

async function onClear(): Promise<void> {
  await settings.clearKey(props.section)
}
</script>

<template>
  <div class="api-key-setting">
    <div class="api-key-status-row">
      <span class="api-key-label">{{ label }}</span>
      <n-tag :type="hasApiKey ? 'success' : 'warning'" size="small" round>
        {{ hasApiKey ? '已配置' : '未配置' }}
      </n-tag>
      <span v-if="hasApiKey" class="api-key-masked">{{ maskedKey }}</span>
      <span class="api-key-actions">
        <n-button size="small" type="primary" strong ghost @click="open">设置 API Key</n-button>
        <n-button v-if="hasApiKey" size="small" type="error" quaternary @click="onClear"
          >清除</n-button
        >
      </span>
    </div>

    <n-modal v-model:show="show" preset="card" :title="`设置 ${label}`" class="api-key-modal">
      <p class="api-key-hint">{{ hint }}</p>
      <n-input
        v-model:value="tempKey"
        type="password"
        show-password-on="click"
        placeholder="sk-..."
        @input="testResult = null"
      />
      <p v-if="validationError" class="api-key-feedback is-error">{{ validationError }}</p>
      <p
        v-else-if="testResult"
        class="api-key-feedback"
        :class="testResult.ok ? 'is-ok' : 'is-error'"
      >
        {{ testResult.message }}
      </p>
      <template #footer>
        <div class="api-key-modal-footer">
          <n-button size="small" :loading="testLoading" :disabled="!canTest" @click="onTest">
            测试连接
          </n-button>
          <div class="api-key-modal-actions">
            <n-button size="small" @click="close">取消</n-button>
            <n-button size="small" type="primary" :disabled="!canConfirm" @click="onConfirm">
              确认
            </n-button>
          </div>
        </div>
      </template>
    </n-modal>
  </div>
</template>

<style scoped>
.api-key-status-row {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  flex-wrap: wrap;
}

.api-key-label {
  font-size: 13px;
  color: var(--text-2);
  font-weight: 500;
}

.api-key-masked {
  font-size: 12px;
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  color: var(--text-3);
  letter-spacing: 0.5px;
}

.api-key-hint {
  margin: 0 0 12px;
  font-size: 12px;
  color: var(--text-3);
  line-height: 1.5;
}

.api-key-feedback {
  margin: 8px 0 0;
  font-size: 12px;
  line-height: 1.5;
}

.api-key-feedback.is-error {
  color: #ff6b81;
}

.api-key-feedback.is-ok {
  color: #3fb68b;
}

.api-key-modal-footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
}

.api-key-modal-actions {
  display: flex;
  gap: 8px;
}
</style>
