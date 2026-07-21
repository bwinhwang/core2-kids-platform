#!/usr/bin/env python3
"""非交互串口日志抓取(idf.py monitor 是交互式,脚本化用这个)。

用法:  python3 tools/serial_capture.py [PORT] [SECONDS] [--csv OUTFILE]
默认:  PORT=/dev/ttyUSB0  SECONDS=60   波特率 115200

它会先把芯片**硬复位到"运行模式"**(不是下载模式),再抓 SECONDS 秒日志,
每行带 [相对秒数] 前缀,便于核对 12s 打盹 / 60s 深度省电这类时序。

--csv OUTFILE:额外按 `data_log` 组件打的 "#CSV-BEGIN name"/"#CSV-END" 哨兵提取 CSV 段,
写入 OUTFILE(哨兵之间的行原样落盘,不带 [相对秒数] 前缀;支持一次抓取里出现多段
BEGIN/END,追加写入同一文件,段间空一行分隔)。

注意:pyserial 打开端口默认会拉 DTR/RTS,可能把 ESP32 带进下载模式导致抓不到 App
输出——所以这里显式做 DTR=False + 脉冲 RTS 的运行态复位。需要 pyserial(esptool 已带)。
"""
import sys, time
import serial

argv = sys.argv[1:]
csv_out = None
if "--csv" in argv:
    i = argv.index("--csv")
    csv_out = argv[i + 1] if i + 1 < len(argv) else None
    argv = argv[:i] + argv[i + 2:]

port = argv[0] if len(argv) > 0 else "/dev/ttyUSB0"
dur  = int(argv[1]) if len(argv) > 1 else 60

s = serial.Serial(port, 115200, timeout=1)
# 硬复位到运行模式:DTR=False 保持 IO0 高(正常启动),脉冲 RTS 拉一次 EN
s.setDTR(False)   # IO0 = HIGH -> normal boot
s.setRTS(True)    # EN  = LOW  -> 复位
time.sleep(0.15)
s.setRTS(False)   # EN  = HIGH -> 出复位,运行 App
s.setDTR(False)
time.sleep(0.05)
s.reset_input_buffer()

csv_fh = open(csv_out, "a") if csv_out else None
in_csv = False

t0, buf = time.time(), b""
while time.time() - t0 < dur:
    data = s.read(256)
    if not data:
        continue
    buf += data
    while b"\n" in buf:
        raw, buf = buf.split(b"\n", 1)
        line = raw.decode("utf-8", "replace").rstrip("\r")
        print(f"[{time.time()-t0:6.1f}] {line}", flush=True)

        if csv_fh:
            if line.startswith("#CSV-BEGIN"):
                in_csv = True
                continue
            if line.startswith("#CSV-END"):
                in_csv = False
                csv_fh.write("\n")
                csv_fh.flush()
                continue
            if in_csv:
                csv_fh.write(line + "\n")
                csv_fh.flush()
s.close()
if csv_fh:
    csv_fh.close()
    print(f"[{time.time()-t0:6.1f}] === CSV 段已写入 {csv_out} ===", flush=True)
print(f"[{time.time()-t0:6.1f}] === capture end ===", flush=True)
