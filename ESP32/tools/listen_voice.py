# 监听 ESP32 录音时的网络流量：验证 voice_start/AUDIO/voice_end 是否真的发出
# 用法: python listen_voice.py [host] [port]
# 连接到 ESP32 后持续读取帧，打印收到的 voice_start / AUDIO 帧数 / voice_end。

import socket
import struct
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.127'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

s = socket.create_connection((HOST, PORT), timeout=30)
s.settimeout(2)
print(f'已连接 {HOST}:{PORT}，等待 ESP32 录音…（请在屏幕上按住「说话」）')

buf = b''
audio_frames = 0
audio_bytes = 0
start_t = None
end = time.time() + 60  # 最多监听 60s

while time.time() < end:
    try:
        d = s.recv(4096)
    except socket.timeout:
        continue
    if not d:
        print('连接断开')
        break
    buf += d

    # 解析帧
    while len(buf) >= 7:
        if buf[0] != 0xAA or buf[1] != 0x55:
            buf = buf[1:]
            continue
        plen = struct.unpack('>I', buf[3:7])[0]
        if len(buf) < 7 + plen:
            break
        ftype = buf[2]
        payload = buf[7:7 + plen]
        buf = buf[7 + plen:]

        if ftype == 0x01:
            print(f'HELLO: {payload.decode("utf-8")}')
        elif ftype == 0x02:
            txt = payload.decode('utf-8', errors='replace')
            print(f'TEXT: {txt}')
            if '"voice_start"' in txt:
                start_t = time.time()
                audio_frames = 0
                audio_bytes = 0
            elif '"voice_end"' in txt:
                dur = (time.time() - start_t) if start_t else 0
                print(f'VOICE_END: 录音 {dur:.1f}s, AUDIO帧={audio_frames}, 字节={audio_bytes}')
        elif ftype == 0x03:
            audio_frames += 1
            audio_bytes += plen
            if audio_frames % 20 == 0:
                print(f'  AUDIO 帧 {audio_frames}，累计 {audio_bytes} 字节')

print('=== 监听结束 ===')
s.close()
