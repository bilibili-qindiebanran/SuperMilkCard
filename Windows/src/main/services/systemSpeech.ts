// Windows 系统语音（System.Speech）：离线 STT（识别 WAV）+ TTS（合成 WAV）
//
// 通过 PowerShell 调用 .NET System.Speech：
//   - STT: SpeechRecognitionEngine.SetInputToWaveFile → Recognize（离线中文识别）
//   - TTS: SpeechSynthesizer → 合成 WAV → base64
// 注意：识别质量依赖系统安装的语言包（zh-CN/en-US/ja-JP），本机已装 zh-CN。

import { execFile } from 'child_process'
import { promisify } from 'util'
import { randomUUID } from 'crypto'
import { tmpdir } from 'os'
import { join } from 'path'
import { writeFile, readFile, unlink } from 'fs/promises'

const execFileAsync = promisify(execFile)

/** 执行 PowerShell 脚本，返回 stdout（UTF-8）。 */
async function runPowerShell(script: string): Promise<string> {
  const args = ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', script]
  const { stdout } = await execFileAsync('powershell.exe', args, {
    windowsHide: true,
    encoding: 'utf8'
  })
  return stdout
}

/** 用 System.Speech 从 WAV 文件离线识别，返回文本（可能为空串）。 */
export async function recognizeWav(wavPath: string): Promise<string> {
  const ps = `
Add-Type -AssemblyName System.Speech
$rec = New-Object System.Speech.Recognition.SpeechRecognitionEngine
$rec.SetInputToWaveFile('${wavPath.replace(/'/g, "''")}')
$r = $rec.Recognize()
if ($r) { Write-Output $r.Text }
$rec.Dispose()
`
  try {
    const out = (await runPowerShell(ps)).trim()
    return out
  } catch (err) {
    throw new Error(`系统语音识别失败：${err instanceof Error ? err.message : String(err)}`)
  }
}

/** 用 System.Speech 合成文本为 WAV，返回 base64。 */
export async function synthesizeWavBase64(text: string, voice?: string, rate?: number): Promise<string> {
  const wavPath = join(tmpdir(), `smc_tts_${randomUUID()}.wav`)
  const safeText = text.replace(/'/g, "''")
  const ps = `
Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.SetOutputToWaveFile('${wavPath.replace(/'/g, "''")}')
if ('${voice ?? ''}' -ne '') {
  $v = $synth.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Name -eq '${(voice ?? '').replace(/'/g, "''")}' } | Select-Object -First 1
  if ($v) { $synth.SelectVoice($v.VoiceInfo.Name) }
}
if (${rate ?? 0} -gt 0) { $synth.Rate = [Math]::Min(10, ${rate ?? 0}) }
$synth.Speak('${safeText}')
$synth.Dispose()
`
  try {
    await runPowerShell(ps)
    const buf = await readFile(wavPath)
    await unlink(wavPath).catch(() => {})
    return buf.toString('base64')
  } catch (err) {
    await unlink(wavPath).catch(() => {})
    throw new Error(`系统语音合成失败：${err instanceof Error ? err.message : String(err)}`)
  }
}

/** 把 base64 WAV 写入临时文件，返回路径（调用方负责删除）。 */
export async function writeWavTemp(base64: string): Promise<string> {
  const wavPath = join(tmpdir(), `smc_stt_${randomUUID()}.wav`)
  await writeFile(wavPath, Buffer.from(base64, 'base64'))
  return wavPath
}
