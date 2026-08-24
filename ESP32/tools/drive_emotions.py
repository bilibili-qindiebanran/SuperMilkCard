# 驱动 ESP32 屏幕依次显示 6 种表情（供视觉确认）
# 用法: python drive_emotions.py [host] [port]

import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.127'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000


def frame(ftype, payload=b''):
    return bytes([0xAA, 0x55, ftype]) + len(payload).to_bytes(4, 'big') + payload


def send_state(s, expression, motion, preview):
    st = '{"type":"live2d_state","expression":"%s","motion":"%s","messagePreview":"%s"}' % (
        expression, motion, preview
    )
    s.sendall(frame(0x02, st.encode('utf-8')))
    print(f'→ {expression}/{motion}: {preview}')


def main():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(5)
    # 收 HELLO
    try:
        s.recv(1024)
    except Exception:
        pass
    print('已连接，开始驱动表情序列…')

    seq = [
        ('neutral', 'idle', '平静状态'),
        ('happy', 'speaking', '今天也要元气满满哦！'),
        ('sad', 'idle', '有点难过呢…'),
        ('angry', 'speaking', '哼，不理你了！'),
        ('surprised', 'thinking', '哇，竟然是这样？！'),
        ('thinking', 'thinking', '让我想想…'),
        ('happy', 'waving', '再见啦～'),
        ('neutral', 'idle', '等待指令…'),
    ]
    for expression, motion, preview in seq:
        send_state(s, expression, motion, preview)
        time.sleep(2.5)

    s.close()
    print('=== 完成 ===')


if __name__ == '__main__':
    main()
