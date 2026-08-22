import { defineStore } from 'pinia'
import { useSettingsStore } from './settings'
import {
  Live2DController,
  mapActionToMotion,
  reidentifyModelExpressions,
  mergeTiers,
  tierLabel,
  parseTierKey
} from '../services/live2dModel'
import type { ExpressionSemantic, TierEntry } from '../services/live2dModel'
import type { Live2dModelInfo } from '@shared/types'

let controller: Live2DController | null = null

/**
 * 解析模型地址：统一保留 live2d:// 自定义协议，由主进程从候选目录
 * （用户导入目录 → 内置目录）读取本地文件，支持跨域。
 * - dev / prod 行为一致，导入到用户目录的模型也能正常加载。
 */
function resolveModelUrl(raw: string): string {
  return raw
}

/** 从模型地址解析模型名（目录名）：live2d://murasame/Murasame.model3.json -> murasame */
function modelNameFromUrl(url: string): string {
  return url.replace(/^live2d:\/\//, '').split('/')[0] ?? ''
}

export const useLive2dStore = defineStore('live2d', {
  state: () => ({
    status: 'idle' as 'idle' | 'loading' | 'ready' | 'error',
    error: '',
    emotion: '',
    action: '',
    models: [] as Live2dModelInfo[],
    modelsLoaded: false,
    /** 当前模型实际声明的表达式 id 列表（设置页下拉用） */
    expressionIds: [] as string[],
    /** 当前模型的自动/手动「语义 → 表达式 id」映射（设置页预览用） */
    emotionToId: {} as Record<string, string>,
    /** 自动推理的「tierKey → 表达式 id」映射（不含手动覆盖，设置页下拉回退用） */
    autoEmotionToId: {} as Record<string, string>,
    /** 当前模型自动识别出的档位列表（语义 + 等级 + tierKey，设置页/指令用） */
    autoTiers: [] as TierEntry[],
    /** 每个表达式推断出的语义（设置页下拉标注用） */
    expressionSemantics: {} as Record<string, string>,
    /** 每个表达式推断出的档位（语义 + 等级，设置页下拉标注用） */
    expressionTiers: {} as Record<string, { semantic: string; level: number }>,
    /** 每个表达式的作者内置参数描述（设置页下拉标注用） */
    expressionDescriptions: {} as Record<string, string>
  }),
  getters: {
    /**
     * 根据当前模型实际生效的「档位 → 表达式 id」映射，自动生成情绪标签指令。
     * 会注入到系统提示词，也会展示在 Live2D 模块最下方。neutral 为默认脸，无需注入。
     * 档位 = 自动识别档位 + 用户新增 - 用户移除；标签含强度等级，如 [emotion: happy@2]。
     */
    emotionInstruction(state): string {
      const settings = useSettingsStore()
      const name = modelNameFromUrl(settings.live2d.modelUrl)
      const additions = settings.live2d.tierAdditions[name] ?? []
      const removals = settings.live2d.tierRemovals[name] ?? []
      const tiers = mergeTiers(state.autoTiers, additions, removals)
      const parts: string[] = []
      for (const t of tiers) {
        if (t.semantic === 'neutral') continue
        if (!state.emotionToId[t.key]) continue
        parts.push(`${tierLabel(t.semantic as ExpressionSemantic, t.level)}用 [emotion: ${t.key}]`)
      }
      if (parts.length === 0) return ''
      return `请在回复中通过标签表达情绪：${parts.join('、')}。`
    }
  },
  actions: {
    mount(canvas: HTMLCanvasElement, width: number, height: number): void {
      if (controller) controller.destroy()
      controller = new Live2DController()
      try {
        controller.mount(canvas, width, height)
      } catch (e) {
        controller = null
        this.status = 'error'
        this.error = e instanceof Error ? e.message : String(e)
      }
    },

    async loadModels(): Promise<void> {
      try {
        this.models = await window.api.live2d.listModels()
        this.modelsLoaded = true
      } catch (e) {
        this.modelsLoaded = false
        console.warn('[live2d] 加载模型列表失败', e)
      }
    },

    async switchModel(modelUrl: string): Promise<void> {
      const settings = useSettingsStore()
      // 持久化模型选择，重启后仍生效
      await settings.save({ live2d: { ...settings.live2d, modelUrl, enabled: true } })
      await this.loadModel(modelUrl)
    },

    async loadModel(url?: string): Promise<void> {
      if (!controller) return
      const settings = useSettingsStore()
      const modelUrl = resolveModelUrl(url ?? settings.live2d.modelUrl)
      this.status = 'loading'
      this.error = ''
      try {
        // 手动覆盖：该模型已保存的「tierKey → 表达式 id」
        const name = modelNameFromUrl(modelUrl)
        const overrides = settings.live2d.emotionOverrides[name] ?? {}
        // LLM 预填/人工修正的「表达式 id → tierKey」：以此为准覆盖启发式语义
        const perEmotion = settings.live2d.expressionEmotions[name] ?? {}
        await controller.loadModel(modelUrl, overrides, perEmotion)
        this.status = 'ready'
        this.expressionIds = controller.getExpressionIds()
        this.emotionToId = controller.getEmotionToId()
        this.autoEmotionToId = controller.getAutoEmotionToId()
        this.autoTiers = controller.getTiers()
        this.expressionSemantics = controller.getExpressionSemantics()
        this.expressionTiers = controller.getExpressionTiers()
        this.expressionDescriptions = controller.getExpressionDescriptions()
        await controller.idle()
        // 默认表情：人格级默认 id 能精确命中则用之；否则回落到该模型的中性表情（保证默认脸正确）
        const applied = await controller.setExpression(settings.persona.defaultExpression)
        if (!applied) await controller.setEmotion('neutral')
      } catch (e) {
        this.status = 'error'
        this.error = e instanceof Error ? e.message : String(e)
      }
    },

    /** 当前正在使用的模型名（目录名） */
    currentModelName(): string {
      const settings = useSettingsStore()
      return modelNameFromUrl(settings.live2d.modelUrl)
    },

    /** 重新识别模型表情：复用加载模型时的分析逻辑，刷新「档位 → 表达式 id」预览。
     *  不重载模型、仅刷新设置页下拉与提示；会应用已持久化的 LLM/人工每表达式标注。 */
    async reidentifyExpressions(modelUrl: string): Promise<void> {
      const settings = useSettingsStore()
      const name = modelNameFromUrl(modelUrl)
      const perEmotion = settings.live2d.expressionEmotions[name] ?? {}
      const { expressionIds, autoEmotionToId, tiers, semantics, tiersById, descriptions } =
        await reidentifyModelExpressions(modelUrl, perEmotion)
      this.expressionIds = expressionIds
      this.autoEmotionToId = autoEmotionToId
      this.autoTiers = tiers
      this.expressionSemantics = semantics
      this.expressionTiers = tiersById
      this.expressionDescriptions = descriptions
      this.emotionToId = { ...autoEmotionToId, ...(settings.live2d.emotionOverrides[name] ?? {}) }
    },

    /** 用 LLM 为当前模型预填「表达式 → 情绪@等级」并持久化，随后刷新映射。
     *  LLM 未配置或失败时抛错，由调用方回退到本地规则识别。 */
    async analyzeWithLlm(): Promise<void> {
      const settings = useSettingsStore()
      const name = this.currentModelName()
      const modelUrl = settings.live2d.modelUrl
      const items = this.expressionIds
        .filter((id): id is string => typeof id === 'string' && id.length > 0)
        .map((id) => ({ id, name: id, description: this.expressionDescriptions[id] ?? '' }))
      if (items.length === 0) throw new Error('当前模型没有可识别的表情')

      const result = await window.api.llm.classify(items)
      // 仅保留真实存在于本模型、且 tierKey 合法的标注
      const valid: Record<string, string> = {}
      for (const id of this.expressionIds) {
        const tk = result[id]
        if (tk && parseTierKey(tk)) valid[id] = tk
      }
      const expressionEmotions = {
        ...(settings.live2d.expressionEmotions ?? {}),
        [name]: valid
      }
      await settings.save({ live2d: { ...settings.live2d, expressionEmotions } })
      await this.reidentifyExpressions(modelUrl)
    },

    /** 为当前模型写入/清除某档位（tierKey）的手动表情覆盖，并原地生效 */
    async setEmotionOverride(tierKey: string, expressionId: string): Promise<void> {
      const settings = useSettingsStore()
      const name = this.currentModelName()
      const existing = { ...(settings.live2d.emotionOverrides[name] ?? {}) }
      if (expressionId) existing[tierKey] = expressionId
      else delete existing[tierKey]
      const emotionOverrides = { ...settings.live2d.emotionOverrides, [name]: existing }
      await settings.save({ live2d: { ...settings.live2d, emotionOverrides } })
      // 原地应用，避免重载模型闪烁
      controller?.applyOverrides(existing)
      this.emotionToId = controller?.getEmotionToId() ?? {}
    },

    async triggerEmotion(emotion: string): Promise<void> {
      if (!controller || !emotion) return
      this.emotion = emotion
      await controller.setEmotion(emotion)
    },

    async triggerAction(action: string): Promise<void> {
      if (!controller || !action) return
      this.action = action
      await controller.playMotion(mapActionToMotion(action))
    },

    async tap(): Promise<void> {
      await controller?.playMotion('TapBody')
    },

    zoom(factor: number, clientX: number, clientY: number): void {
      controller?.zoomAt(factor, clientX, clientY)
    },

    pan(dx: number, dy: number): void {
      controller?.addPan(dx, dy)
    },

    resize(width: number, height: number): void {
      controller?.resize(width, height)
    },

    destroy(): void {
      controller?.destroy()
      controller = null
    }
  }
})
