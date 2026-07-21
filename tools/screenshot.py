#!/usr/bin/env python3
"""抓设备当前屏幕存 PNG(配合固件组件 components/screenshot)。

用法:  python3 tools/screenshot.py [PORT] [OUT.png]           # 主机主动:发 SHOT 抓一张
       python3 tools/screenshot.py --watch [PORT] [DIR/前缀]   # 常驻监听:设备端 BtnB 触发时接收
默认:  PORT=/dev/ttyUSB0  OUT=screenshot.png   波特率 115200

原理:固件把 LVGL 活动屏 snapshot 成 RGB565,RLE+Base64 从日志串口吐回(哨兵行
<<<SHOT …>>> / <<<SHOT-END>>> 包住,数据行带 $ 前缀,期间设备日志静默)→ 本脚本
解码校验 CRC32 存 PNG。存完把绝对路径打在最后一行,Claude Code 直接 Read 那个 PNG 即可"看屏"。

两种触发方向:
  · 默认(请求-应答):主机向 UART0 发 "SHOT\\n" 主动要一张。
  · --watch(常驻监听):不发命令,一直读串口;设备端按屏下 BtnB(截屏键)时会自吐一帧,
    脚本见到 <<<SHOT 哨兵即开收、到 <<<SHOT-END>>> 存盘,文件名带时间戳,可连续接多张。
    适合"手边按物理键截图"的实机场景。Ctrl-C 退出。

⚠️ 与 serial_capture.py 相反,这里**绝不能复位芯片**(要抓的就是当前画面):
pyserial 打开端口默认拉 DTR/RTS 会把 ESP32 带进复位/下载,所以先建对象、
把 DTR/RTS 置低再 open。纯标准库 + pyserial(esptool 已带),无 PIL 依赖。
"""
import base64
import os
import re
import struct
import sys
import time
import zlib

import serial

WATCH = "--watch" in sys.argv[1:]
_args = [a for a in sys.argv[1:] if not a.startswith("--")]
PORT = _args[0] if len(_args) > 0 else "/dev/ttyUSB0"
OUT  = _args[1] if len(_args) > 1 else ("." if WATCH else "screenshot.png")

HDR_RE = re.compile(rb"<<<SHOT (.+?)>>>")


def open_port_no_reset(port: str) -> serial.Serial:
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 1
    s.dtr = False   # open 前置好:IO0 保持高(不进下载)
    s.rts = False   # EN 保持高(不复位)
    s.open()
    return s


def _collect_payload(s: serial.Serial, hdr: dict, buf: bytes) -> tuple[bytes, bool]:
    """已拿到头、buf 是头之后的残留;继续读 $ 数据行到 <<<SHOT-END>>>。
    返回 (RLE 字节流, 是否完整收齐)。"""
    rle_len = int(hdr[b"rle"])
    payload = bytearray()
    deadline = time.time() + 60          # 115200 下最坏 ~30s,留裕量
    done = False
    while time.time() < deadline and not done:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.strip()
            if line.startswith(b"$"):
                payload += base64.b64decode(line[1:])
            elif line.startswith(b"<<<SHOT-END"):
                done = True
                break
    return bytes(payload), (done and len(payload) == rle_len)


def grab(s: serial.Serial) -> tuple[dict, bytes]:
    """主机主动:发 SHOT、收哨兵包住的 Base64 数据,返回 (头字段, RLE 字节流)。"""
    for attempt in range(3):
        s.reset_input_buffer()
        s.write(b"\nSHOT\n")
        s.flush()
        deadline = time.time() + 6
        buf = b""
        hdr = None
        while time.time() < deadline:
            chunk = s.read(4096)
            if chunk:
                buf += chunk
                m = HDR_RE.search(buf)
                if m:
                    hdr = dict(kv.split(b"=", 1) for kv in m.group(1).split(b" "))
                    buf = buf[m.end():]
                    break
        if hdr is None:
            print(f"[{attempt + 1}/3] 设备没应答 SHOT,重试…(固件带 screenshot 组件了吗?)")
            continue
        payload, ok = _collect_payload(s, hdr, buf)
        if ok:
            return hdr, payload
        print(f"[{attempt + 1}/3] 传输不完整(收 {len(payload)}/{int(hdr[b'rle'])} B),重试…")
    sys.exit("✗ 三次都没抓到完整截图,检查:串口被占用?固件是否已重刷带 screenshot 组件?")


def watch_loop(s: serial.Serial, out_dir: str) -> None:
    """常驻监听:不发命令,一直读;设备端(BtnB)自吐帧时接收并按时间戳存盘。"""
    os.makedirs(out_dir, exist_ok=True) if out_dir not in (".", "") else None
    print(f"● 监听 {PORT} …按设备屏下 BtnB(中键)截屏,存到 {os.path.abspath(out_dir)}/(Ctrl-C 退出)")
    buf = b""
    while True:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
        m = HDR_RE.search(buf)
        if not m:
            if len(buf) > 8192:
                buf = buf[-256:]          # 无头的日志噪声,别无限攒
            continue
        hdr = dict(kv.split(b"=", 1) for kv in m.group(1).split(b" "))
        payload, ok = _collect_payload(s, hdr, buf[m.end():])
        buf = b""
        if not ok:
            print(f"⚠ 收到一帧但不完整({len(payload)}/{int(hdr[b'rle'])} B),丢弃")
            continue
        out = os.path.join(out_dir, time.strftime("screenshot_%Y%m%d_%H%M%S.png"))
        save_png(hdr, payload, out)


def rle16_decode(rle: bytes, expect: int) -> bytes:
    out = bytearray()
    i = 0
    while i + 3 <= len(rle):
        n = rle[i]
        out += rle[i + 1:i + 3] * n
        i += 3
    if len(out) != expect:
        sys.exit(f"✗ RLE 解出 {len(out)} B,应为 {expect} B(流损坏)")
    return bytes(out)


def rgb565le_to_rgb888(raw: bytes, w: int, h: int) -> bytes:
    px = struct.unpack(f"<{w * h}H", raw)
    r5 = [(v << 3 | v >> 2) for v in range(32)]
    g6 = [(v << 2 | v >> 4) for v in range(64)]
    out = bytearray(w * h * 3)
    for i, v in enumerate(px):
        out[3 * i]     = r5[v >> 11]
        out[3 * i + 1] = g6[(v >> 5) & 0x3F]
        out[3 * i + 2] = r5[v & 0x1F]
    return bytes(out)


def write_png(path: str, w: int, h: int, rgb: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data)))

    stride = w * 3
    scan = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(scan, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def save_png(hdr: dict, rle: bytes, out: str) -> None:
    """校验 CRC、RLE 解码、RGB565→888、写 PNG,末行打绝对路径。"""
    w, h = int(hdr[b"w"]), int(hdr[b"h"])
    raw_len = int(hdr[b"raw"])
    crc = int(hdr[b"crc32"], 16)
    if zlib.crc32(rle) != crc:
        print(f"⚠ CRC 不匹配(设备 {crc:08x} vs 本地 {zlib.crc32(rle):08x}),图可能有杂点")
    raw = rle16_decode(rle, raw_len)
    write_png(out, w, h, rgb565le_to_rgb888(raw, w, h))
    ratio = raw_len / len(rle) if rle else 0
    print(f"✓ {w}x{h}  RLE {len(rle)} B({ratio:.1f}x 压缩)")
    print(os.path.abspath(out))


def main() -> None:
    s = open_port_no_reset(PORT)
    try:
        if WATCH:
            watch_loop(s, OUT)
        else:
            hdr, rle = grab(s)
            save_png(hdr, rle, OUT)
    except KeyboardInterrupt:
        print("\n退出监听")
    finally:
        s.close()


if __name__ == "__main__":
    main()
