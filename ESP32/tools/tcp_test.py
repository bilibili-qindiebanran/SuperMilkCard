# SuperMilkCard ↔ ESP32 TCP 协议联调测试
# 用法: python tcp_test.py [host] [port]
# 验证: HELLO 帧 / live2d_state 解析（含未知值回退、超长截断）/ chat 帧

import socket
import struct
import sys
import json
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.127'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000


def frame(ftype, payload=b''):
    return bytes([0xAA, 0x55, ftype]) + struct.pack('>I', len(payload)) + payload


def recv_frame(s):
    hdr = b''
    while len(hdr) < 7:
        c = s.recv(7 - len(hdr))
        if not c:
            return None
        hdr += c
    if hdr[0] != 0xAA or hdr[1] != 0x55:
        print('BAD MAGIC', hdr.hex())
        return None
    plen = struct.unpack('>I', hdr[3:7])[0]
    payload = b''
    while len(payload) < plen:
        c = s.recv(plen - len(payload))
        if not c:
            return None
        payload += c
    return hdr[2], payload


def main():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(5)
    print(f'connected to {HOST}:{PORT}')

    # 1) 期望收到 HELLO 帧
    ft, pl = recv_frame(s)
    print(f'frame type=0x{ft:02x} payload={pl.decode("utf-8")}')

    # 2) 发送 live2d_state（正常）
    state = json.dumps({
        'type': 'live2d_state',
        'expression': 'happy',
        'motion': 'speaking',
        'messagePreview': '今天也要元气满满哦！'
    }).encode('utf-8')
    s.sendall(frame(0x02, state))
    print('sent live2d_state happy/speaking')

    # 3) 发送未知情绪 + 超长预览（应回退 neutral/idle 并截断）
    state2 = json.dumps({
        'type': 'live2d_state',
        'expression': 'hyper_excited',
        'motion': 'dancing',
        'messagePreview': 'x' * 200
    }).encode('utf-8')
    s.sendall(frame(0x02, state2))
    print('sent unknown emotion + long preview')

    # 4) 发送 chat
    chat = json.dumps({'type': 'chat', 'role': 'assistant', 'content': '你好呀'}).encode('utf-8')
    s.sendall(frame(0x02, chat))
    print('sent chat')

    time.sleep(2)
    s.close()
    print('done')


if __name__ == '__main__':
    main()
