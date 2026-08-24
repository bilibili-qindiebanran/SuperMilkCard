# 触发 ESP32 TCP 崩溃并抓取串口 backtrace（用于定位崩溃点）
# 用法: python crash_monitor.py [host] [port]

import serial
import socket
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.127'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

# 先触发一次 TCP 连接 + live2d_state 帧，让设备崩溃
try:
    s = socket.create_connection((HOST, PORT), timeout=4)
    payload = (
        b'{"type":"live2d_state","expression":"happy",'
        b'"motion":"speaking","messagePreview":"hello"}'
    )
    frame = b'\xaa\x55\x02' + len(payload).to_bytes(4, 'big') + payload
    s.sendall(frame)
    s.close()
    print('trigger sent')
except Exception as e:
    print('trigger error:', e)

# 抓串口看崩溃
try:
    ser = serial.Serial('COM10', 115200, timeout=1)
    try:
        ser.setDTR(False)
        ser.setRTS(False)
    except Exception:
        pass
except Exception as e:
    print('serial open error:', e)
    sys.exit(1)

deadline = time.time() + 20
buf = b''
while time.time() < deadline:
    d = ser.read(8192)
    if d:
        buf += d
        sys.stdout.buffer.write(d)
        sys.stdout.flush()
ser.close()
with open('crash_full.log', 'wb') as f:
    f.write(buf)
print()
print('=== captured', len(buf), 'bytes (saved crash_full.log) ===')
