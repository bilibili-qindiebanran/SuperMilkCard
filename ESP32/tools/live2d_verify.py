# 单脚本验证：读 COM11 日志 + TCP live2d_state 解析验证
# 依赖：console 已切到 UART0（COM11），设备已连上局域网
# 用法: python live2d_verify.py [host] [port]

import serial
import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.127'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000


def frame(ftype, payload=b''):
    return bytes([0xAA, 0x55, ftype]) + len(payload).to_bytes(4, 'big') + payload


def main():
    # 先确认设备 TCP 在线
    try:
        s = socket.create_connection((HOST, PORT), timeout=5)
    except Exception as e:
        print(f'设备 TCP 不可达: {e}')
        return
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
        print('HELLO:', payload.decode('utf-8'))

    # live2d_state 正常
    st = '{"type":"live2d_state","expression":"happy","motion":"speaking","messagePreview":"今天也要元气满满"}'
    s.sendall(frame(0x02, st.encode('utf-8')))
    print('sent live2d_state happy/speaking')

    # live2d_state 未知值 + 超长预览
    st2 = '{"type":"live2d_state","expression":"hyper_excited","motion":"dancing","messagePreview":"' + 'x' * 200 + '"}'
    s.sendall(frame(0x02, st2.encode('utf-8')))
    print('sent unknown emotion + 200B preview')

    # chat 帧
    chat = '{"type":"chat","role":"assistant","content":"你好呀"}'
    s.sendall(frame(0x02, chat.encode('utf-8')))
    print('sent chat')

    s.close()

    # 读 COM11 上的解析日志
    print('--- ESP32 解析日志 ---')
    try:
        ser = serial.Serial('COM11', 115200, timeout=0.3)
    except Exception as e:
        print('COM11 open fail:', e)
        return
    time.sleep(0.3)
    ser.reset_input_buffer()
    deadline = time.time() + 6
    while time.time() < deadline:
        d = ser.read(8192)
        if d:
            txt = d.decode('utf-8', errors='replace')
            for line in txt.splitlines():
                if 'net_tcp' in line or 'live2d' in line:
                    print('DEV:', line.strip())
    ser.close()
    print('=== done ===')


if __name__ == '__main__':
    main()
