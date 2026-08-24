# 完整联调：COM12 持续读日志 + TCP live2d_state 解析验证
# 前提：console 已切 UART0（COM12=CH340 接设备 GPIO43/44），设备连上局域网
# 用法: python live2d_com12_verify.py [host] [port]

import serial
import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.127'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000


def frame(ftype, payload=b''):
    return bytes([0xAA, 0x55, ftype]) + len(payload).to_bytes(4, 'big') + payload


def main():
    # 打开 COM12（持续读日志）
    ser = serial.Serial('COM12', 115200, timeout=0.3)
    ser.reset_input_buffer()

    # 1) 发 TCP live2d_state（设备应已在线上）
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(5)

    # HELLO
    hdr = b''
    while len(hdr) < 7:
        c = s.recv(7 - len(hdr))
        if not c:
            break
        hdr += c
    if len(hdr) == 7:
        plen = int.from_bytes(hdr[3:7], 'big')
        payload = b''
        while len(payload) < plen:
            c = s.recv(plen - len(payload))
            if not c:
                break
            payload += c
        print('PC → HELLO:', payload.decode('utf-8'))

    # live2d_state 正常
    st = '{"type":"live2d_state","expression":"happy","motion":"speaking","messagePreview":"今天也要元气满满"}'
    s.sendall(frame(0x02, st.encode('utf-8')))
    print('PC → live2d_state happy/speaking')

    # live2d_state 未知值 + 超长
    st2 = '{"type":"live2d_state","expression":"hyper_excited","motion":"dancing","messagePreview":"' + 'x' * 200 + '"}'
    s.sendall(frame(0x02, st2.encode('utf-8')))
    print('PC → unknown emotion + 200B preview')

    # chat 帧
    chat = '{"type":"chat","role":"assistant","content":"你好呀"}'
    s.sendall(frame(0x02, chat.encode('utf-8')))
    print('PC → chat')

    s.close()

    # 2) 读 COM12 上设备对这三帧的解析日志
    print('--- ESP32 解析日志（COM12） ---')
    deadline = time.time() + 6
    while time.time() < deadline:
        d = ser.read(8192)
        if d:
            txt = d.decode('utf-8', errors='replace')
            for line in txt.splitlines():
                if 'net_tcp' in line or 'live2d' in line or 'incoming' in line:
                    print('DEV:', line.strip())
    ser.close()
    print('=== done ===')


if __name__ == '__main__':
    main()
