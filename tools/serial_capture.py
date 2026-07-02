#!/usr/bin/env python3
"""非交互串口日志抓取(idf.py monitor 是交互式,脚本化用这个)。

用法:  python3 tools/serial_capture.py [PORT] [SECONDS]
默认:  PORT=/dev/ttyUSB0  SECONDS=60   波特率 115200

它会先把芯片**硬复位到"运行模式"**(不是下载模式),再抓 SECONDS 秒日志,
每行带 [相对秒数] 前缀,便于核对 12s 打盹 / 60s 深度省电这类时序。

注意:pyserial 打开端口默认会拉 DTR/RTS,可能把 ESP32 带进下载模式导致抓不到 App
输出——所以这里显式做 DTR=False + 脉冲 RTS 的运行态复位。需要 pyserial(esptool 已带)。
"""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
dur  = int(sys.argv[2]) if len(sys.argv) > 2 else 60

s = serial.Serial(port, 115200, timeout=1)
# 硬复位到运行模式:DTR=False 保持 IO0 高(正常启动),脉冲 RTS 拉一次 EN
s.setDTR(False)   # IO0 = HIGH -> normal boot
s.setRTS(True)    # EN  = LOW  -> 复位
time.sleep(0.15)
s.setRTS(False)   # EN  = HIGH -> 出复位,运行 App
s.setDTR(False)
time.sleep(0.05)
s.reset_input_buffer()

t0, buf = time.time(), b""
while time.time() - t0 < dur:
    data = s.read(256)
    if not data:
        continue
    buf += data
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        print(f"[{time.time()-t0:6.1f}] {line.decode('utf-8', 'replace').rstrip(chr(13))}", flush=True)
s.close()
print(f"[{time.time()-t0:6.1f}] === capture end ===", flush=True)
