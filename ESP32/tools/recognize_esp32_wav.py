# 用 System.Speech 离线识别 ESP32 录音 WAV（先重采样到 22050Hz 兼容格式）
# 用法: python recognize_esp32_wav.py <wav_path>
# 输出识别文本；空表示未识别出内容。

import subprocess
import sys
import tempfile
import os

def main():
    if len(sys.argv) < 2:
        print('用法: python recognize_esp32_wav.py <wav_path>')
        return
    src = sys.argv[1]
    if not os.path.exists(src):
        print('文件不存在:', src)
        return

    # 重采样到 22050Hz/16bit/单声道（System.Speech 兼容格式）
    out = os.path.join(tempfile.gettempdir(), 'smc_resample.wav')
    subprocess.run(
        ['ffmpeg', '-y', '-i', src, '-ar', '22050', '-ac', '1', '-sample_fmt', 's16', out],
        capture_output=True
    )

    # 用 System.Speech 识别
    ps = f"""
Add-Type -AssemblyName System.Speech
$rec = New-Object System.Speech.Recognition.SpeechRecognitionEngine
$rec.SetInputToWaveFile('{out.replace("'", "''")}')
$r = $rec.Recognize()
if ($r) {{ Write-Output ('TEXT:' + $r.Text) }} else {{ Write-Output 'TEXT:(空)' }}
$rec.Dispose()
"""
    r = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', ps],
                       capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if line.startswith('TEXT:'):
            print('识别结果:', line[5:])
            return
    print('识别异常:', r.stdout, r.stderr)

if __name__ == '__main__':
    main()
