#!/usr/bin/env python3
"""tilt_maze 死亡演出关键帧 —— 主机离屏预览。

根 CLAUDE.md §11.2 / ROADMAP §7:改画面先在主机画一遍再烧板(WSL 不能烧录,每轮实机
都要人工介入)。死亡演出是**时间轴**上的东西,一张静态图看不出"够不够有戏",所以本文件
按 render.c 的同一套曲线抽 7 个关键帧并排画出来 —— 节奏对不对、哪一拍空、红框会不会
盖住路,横着看一眼就知道。

⚠️ 这是**效果图不是回归测试**(平行翻译不算验证,见根 §11.2 chick_pour 那条)。
   时间线常量直接从 render.c 正则抠出来,防止改了 C 忘了改预览;曲线是手抄的,
   最终手感仍以实机为准。

用法: python3 apps/tilt_maze/tools/preview_fail.py [输出目录]
"""
import math
import os
import random
import re
import sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RENDER_C = os.path.join(HERE, "..", "main", "render.c")

# ── 从 render.c 抠时间线常量(别让预览和真身悄悄分家)────────────────
SRC = open(RENDER_C, encoding="utf-8").read()
def define(name):
    m = re.search(r"^#define\s+%s\s+(\d+)" % name, SRC, re.M)
    if not m:
        sys.exit("render.c 里找不到 #define %s —— 预览和真身已分家,先对齐再跑" % name)
    return int(m.group(1))

SHARDS     = define("FB_SHARDS")
CORE_MS    = define("FB_CORE_MS")
RING_MS    = define("FB_RING_MS")
RING2_DLY  = define("FB_RING2_DLY")
RING2_MS   = define("FB_RING2_MS")
RING_R0    = define("FB_RING_R0")
RING_R1    = define("FB_RING_R1")
CORE_R1    = define("FB_CORE_R1")
SHARD_MS_  = 0
SCAR_DLY   = define("FB_SCAR_DLY")
SCAR_MS    = define("FB_SCAR_MS")
SCAR_R     = define("FB_SCAR_R")
SHARD_MS  = define("FB_SHARD_MS")
SHAKE_MS  = define("FB_SHAKE_MS")
SHAKE_PX  = define("FB_SHAKE_PX")
BAR_DELAY = define("FB_BAR_DELAY")
BAR_MS    = define("FB_BAR_MS")
BAR_W     = define("FB_BAR_W")
BAR_PEAK  = define("FB_BAR_PEAK")

def definef(name):
    m = re.search(r"^#define\s+%s\s+([0-9.]+)f" % name, SRC, re.M)
    if not m:
        sys.exit("render.c 里找不到 #define %s" % name)
    return float(m.group(1))

CORE_HOLD  = definef("FB_CORE_HOLD")
RING_HOLD  = definef("FB_RING_HOLD")
SHARD_HOLD = definef("FB_SHARD_HOLD")
BAR_IN     = definef("FB_BAR_IN")
BAR_OUT    = definef("FB_BAR_OUT")


def fade_after(f, hold):
    """与 render.c 的 fade_after() 同一条:hold 之前满亮,之后才收。"""
    return 255.0 if f <= hold else 255.0 * (1.0 - (f - hold) / (1.0 - hold))

W, H, CELL = 320, 240, 20
C_FLOOR  = (0xD7, 0xEC, 0xBF)
C_WALL   = (0x7F, 0xB0, 0x69)
C_HAZARD = (0xD6, 0x3A, 0x2A)
C_BALL   = (0xFF, 0xD2, 0x3F)
C_SCAR   = (0x8E, 0x25, 0x19)
C_CORE   = (0xFF, 0xF2, 0xC8)
SHARD_COLS = [C_BALL, C_CORE, C_HAZARD]

DEATH = (170.0, 130.0)   # 死点:挑屏幕中偏右,能看清碎片有没有被边界吃掉
FRAMES = [0, 80, 180, 300, 440, 600, 780, 960]


def maze_bg():
    """一张够典型的底图:窄走廊 + 几块墙,用来判断红框/碎片会不会糊住路。"""
    img = Image.new("RGB", (W, H), C_WALL)
    d = ImageDraw.Draw(img)
    for row in range(H // CELL):
        for col in range(W // CELL):
            if row in (0, H // CELL - 1) or col in (0, W // CELL - 1):
                continue
            if (col % 3 == 0 and row % 2 == 0) or (col % 5 == 2 and row % 3 == 1):
                continue
            x, y = col * CELL, row * CELL
            d.rounded_rectangle([x, y, x + CELL - 1, y + CELL - 1], 4, fill=C_FLOOR)
    return img


def blend(img, box, color, opa, radius=None):
    """把一块半透明色贴上去(LVGL 的 opa 就是整体不透明度)。"""
    if opa <= 0:
        return
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rgba = color + (int(max(0, min(255, opa))),)
    if radius is None:
        d.rectangle(box, fill=rgba)
    else:
        d.ellipse(box, fill=rgba)
    img.alpha_composite(layer)


def ring(img, cx, cy, r, opa, width):
    if opa <= 0:
        return
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(layer).ellipse([cx - r, cy - r, cx + r, cy + r],
                                  outline=C_HAZARD + (int(opa),), width=width)
    img.alpha_composite(layer)


def shock_ring(img, t, delay, dur, width):
    """一圈冲击环(cb_ring):半径线性扩,透明度跟着半径掉。"""
    if not (delay <= t < delay + dur):
        return
    f = (t - delay) / dur
    r = RING_R0 + (RING_R1 - RING_R0) * f
    ring(img, DEATH[0], DEATH[1], r,
         fade_after((r - RING_R0) / (RING_R1 - RING_R0), RING_HOLD), width)


def frame(t, shards):
    base = maze_bg()
    img = Image.new("RGBA", (W, H))

    # 世界震动:阻尼正弦(cb_shake)。整张迷宫横移,露出的底色仍是墙色。
    dx = 0
    if t < SHAKE_MS and SHAKE_PX > 0:
        f = t / SHAKE_MS
        dx = int(SHAKE_PX * (1.0 - f) * math.sin(f * 4.0 * math.pi))
    shifted = Image.new("RGB", (W, H), C_WALL)
    shifted.paste(base, (dx, 0))
    img.paste(shifted.convert("RGBA"), (0, 0))

    cx, cy = DEATH[0] + dx, DEATH[1]   # 死点跟着迷宫一起被震(碎片挂 s_scr 不跟,同真身)

    # ① 白闪核(cb_core)
    if t < CORE_MS:
        f = t / CORE_MS
        r = 7 + (CORE_R1 - 7) * f
        blend(img, [DEATH[0] - r, DEATH[1] - r, DEATH[0] + r, DEATH[1] + r],
              C_CORE, fade_after((r - 7) / (CORE_R1 - 7), CORE_HOLD), radius=True)

    # ②b 焦痕:淡入后一直留着(不自删,重进本关才清)——中后段全靠它守住死点
    if t >= SCAR_DLY:
        opa = 165 * min(1.0, (t - SCAR_DLY) / SCAR_MS)
        blend(img, [DEATH[0] - SCAR_R, DEATH[1] - SCAR_R,
                    DEATH[0] + SCAR_R, DEATH[1] + SCAR_R], C_SCAR, opa, radius=True)

    # ② 冲击环 ×2(cb_ring;第二圈延后出,接住中段)
    shock_ring(img, t, 0, RING_MS, 7)
    shock_ring(img, t, RING2_DLY, RING2_MS, 5)

    # ③ 碎片(cb_shard)
    if t < SHARD_MS:
        f = t / SHARD_MS
        e = 1.0 - (1.0 - f) ** 2
        opa = fade_after(f, SHARD_HOLD)
        for (ang, dist, sz, col) in shards:
            x = DEATH[0] - sz / 2 + math.cos(ang) * dist * e
            y = DEATH[1] - sz / 2 + math.sin(ang) * dist * e
            blend(img, [x, y, x + sz, y + sz], col, opa, radius=True)

    # ⑤ 四边红框告警(单次淡入淡出,不是频闪)
    bt = t - BAR_DELAY
    if 0 <= bt < BAR_MS:
        k = 1.0 - abs(bt / BAR_MS * 2.0 - 1.0)
        opa = 200 * k
        for box in ([0, 0, W, BAR_W], [0, H - BAR_W, W, H],
                    [0, BAR_W, BAR_W, H - BAR_W], [W - BAR_W, BAR_W, W, H - BAR_W]):
            blend(img, box, C_HAZARD, opa)

    # 球:整段演出都是"炸没了"(重进本关才弹回来),这里只标注一下原位
    return img.convert("RGB")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else HERE
    rnd = random.Random(7)   # 固定种子:每次预览同一朵花,便于前后对比
    shards = []
    for i in range(SHARDS):
        ang = i * (2 * math.pi / SHARDS) + rnd.random() * 0.45
        shards.append((ang, 62 + rnd.random() * 34, 10 + rnd.randrange(5), SHARD_COLS[i % 3]))

    pad, cols = 8, 4
    rows = (len(FRAMES) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W + pad) + pad, rows * (H + pad + 14) + pad), (24, 24, 26))
    d = ImageDraw.Draw(sheet)
    for i, t in enumerate(FRAMES):
        r, c = divmod(i, cols)
        x = pad + c * (W + pad)
        y = pad + r * (H + pad + 14)
        sheet.paste(frame(t, shards), (x, y))
        d.text((x + 2, y + H + 2), "t = %d ms" % t, fill=(230, 230, 230))

    path = os.path.abspath(os.path.join(out_dir, "preview_fail.png"))
    sheet.save(path)
    print("死亡演出关键帧(震 %dms / 核 %dms / 环 %dms / 碎片 %dms / 红框 %d~%dms):"
          % (SHAKE_MS, CORE_MS, RING_MS, SHARD_MS, BAR_DELAY, BAR_DELAY + BAR_MS))
    print(path)


if __name__ == "__main__":
    main()
