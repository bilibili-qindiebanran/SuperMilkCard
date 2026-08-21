export interface EmotionExtract {
  /** 去掉情绪/动作标签后的纯文本 */
  text: string
  /** 最后出现的情绪标签，如 happy */
  emotion: string | null
  /** 出现的所有动作标签，如 ['wave'] */
  actions: string[]
}

/**
 * 从 AI 回复中提取并剥离约定标签：`[emotion: xxx]` / `[action: xxx]`。
 * 标签不会出现在聊天文本中，而是交给 Live2D 触发动作/表情。
 */
export function extractEmotionTags(input: string): EmotionExtract {
  let text = input
  let emotion: string | null = null
  const actions: string[] = []

  const re = /\[(emotion|action):\s*([^\]]+)\]/gi
  text = text.replace(re, (_match, kind: string, value: string) => {
    const v = value.trim()
    if (!v) return ''
    if (kind.toLowerCase() === 'emotion') emotion = v
    else actions.push(v)
    return ''
  })

  return {
    text: text.replace(/\n{3,}/g, '\n\n').trim(),
    emotion,
    actions
  }
}

/** 判断某句是否属于「情感标签指令」句（形如：请在回复中通过标签表达情绪：… [emotion: xxx] …） */
function isEmotionInstructionSentence(sentence: string): boolean {
  const t = sentence.trim()
  return t.includes('[emotion:') && (t.includes('表达情绪') || t.includes('标签'))
}

/**
 * 从人设提示词中剔除约定好的情感标签指令句。
 * 情绪指令现已改由 Live2D 模块根据当前模型自动生成并注入，
 * 避免与人设里手工写死的句子重复。用「按句号切分 + 匹配指令句」的方式，尽量不误伤其它内容。
 */
export function stripEmotionInstruction(text: string): string {
  if (!text.includes('[emotion:')) return text
  const kept = text
    .split('。')
    .map((s) => s.trim())
    .filter((s) => s && !isEmotionInstructionSentence(s))
  return kept.length ? `${kept.join('。')}。` : ''
}
