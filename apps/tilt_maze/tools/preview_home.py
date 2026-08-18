#!/usr/bin/env python3
"""tilt_maze「家」图标候选效果图 —— 主机离屏预览。

根 CLAUDE.md §11.2:改图/改布局先在主机画一遍再烧板(WSL 不能烧录,每轮实机都要人工
介入)。本文件用与 render.c 烘星/烘怪**同一套算法**(逐像素 4×4 超采样 + 距离场判定)
烘「家」的候选精灵 —— 选定哪版,就把对应的 sample_*() 逐行译成 C 的 bake_home_sprite()。

⚠️ 精灵 32px > 家格 20px:家本来就"略盖过格子"(现状 26px 圆盘),再加一圈柔光晕。
   放大对比图上画了 20px 家格边界,溢出多少一眼可见。

用法: python3 apps/tilt_maze/tools/preview_home.py [输出目录]
"""
import math
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
MAZE_C = os.path.join(HERE, "..", "main", "maze.c")

# ── 与 tuning.h / render.c 对齐的常量 ────────────────────────────────
CELL = 20              # MAZE_CELL
COLS, ROWS = 16, 12    # MAZE_COLS / MAZE_ROWS
GOAL_R = 13.0          # 到家判定半径(玩法量,本轮不动)
C_FLOOR = (0xD7, 0xEC, 0xBF)     # k_floor_color
C_WALL = (0x7F, 0xB0, 0x69)      # k_wall_color
C_BALL = (0xFF, 0xD2, 0x3F)
C_HOME_NOW = (0xC6, 0x8A, 0x52)  # 现状:纯色圆盘

# ── 家精灵画布 ───────────────────────────────────────────────────────
IMG = 32
CX = CY = IMG / 2.0
R_BODY = 12.8          # 实体外沿(≈ 现状 26px 直径,保持"陷进格子里"的观感)
R_GLOW = 15.7          # 柔光晕外沿(alpha 渐隐到 0)
GLOW_A = 0.30
GLOW_A_LIT = 0.48      # 亮灯态:光晕浓一档

C_GLOW = (0xFF, 0xB2, 0x5E)
C_TWIG_L = (0xD9, 0x9C, 0x63)    # 窝沿:草茎受光面
C_TWIG_D = (0xA1, 0x68, 0x3A)    # 窝沿:草茎背光面
C_HOLLOW = (0x74, 0x47, 0x28)    # 窝底(暗 → 显出"凹"进去)
C_EGG = (0xFF, 0xF1, 0xD6)
C_HEART = (0xFF, 0x7E, 0x9B)
C_ROOF = (0x8A, 0x5A, 0x33)
C_WALLH = (0xD9, 0xA0, 0x5B)     # 屋身
C_BASE = (0xA1, 0x68, 0x3A)      # 地基
C_LIGHT = (0xFF, 0xD9, 0x8A)     # 窗/门里的暖灯
C_LIGHT2 = (0xFF, 0xF0, 0xC0)
WHITE = (0xFF, 0xFF, 0xFF)


def mix(a, b, t):
    t = 0.0 if t < 0 else (1.0 if t > 1 else t)
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def glow_layer(d, amax=None):
    """柔光晕:实体内为常数(会被实体盖住),外沿线性渐隐。"""
    amax = GLOW_A if amax is None else amax
    if d <= R_BODY:
        return amax
    if d >= R_GLOW:
        return 0.0
    return amax * (R_GLOW - d) / (R_GLOW - R_BODY)


# ── 候选 0:现状(纯色圆盘,无纹理无光晕)────────────────────────────
def sample_now(x, y):
    d = math.hypot(x - CX, y - CY)
    return (C_HOME_NOW, 1.0) if d <= GOAL_R else ((0, 0, 0), 0.0)


# ── 窝(候选 A/C 共用底座)───────────────────────────────────────────
R_IN = 8.0             # 窝口内沿


def nest_base(dx, dy):
    d = math.hypot(dx, dy)
    a = glow_layer(d)
    col = C_GLOW
    if d <= R_BODY:
        th = math.atan2(dy, dx)
        # 盘绕的草:同心环(d)被角向正弦揉皱 → 一圈圈草茎,不是螺旋
        # (第一版写成 sin(th*11 + d*1.7),等值线是螺旋,画出来像漩涡饼干)
        w = 0.5 + 0.5 * math.sin(d * 4.3 + 1.15 * math.sin(th * 5.0))
        w *= 0.82 + 0.18 * (0.5 + 0.5 * math.sin(th * 17.0))   # 断续的草节
        col = mix(C_TWIG_D, C_TWIG_L, w)
        col = mix(col, WHITE, 0.13 * max(0.0, -dy) / R_BODY)   # 上缘受光
        a = 1.0
    if d <= R_IN:
        col = mix(C_HOLLOW, mix(C_HOLLOW, C_TWIG_D, 0.55), d / R_IN)   # 窝底进深
        a = 1.0
    return col, a


def sample_nest_eggs(x, y):
    dx, dy = x - CX, y - CY
    col, a = nest_base(dx, dy)
    for (ex, ey) in ((-3.3, 0.9), (3.1, -1.0)):
        ux, uy = dx - ex, dy - ey
        rx = 3.0 * (1.0 + 0.13 * uy / 3.6)      # 下宽上窄 = 蛋形
        if (ux / rx) ** 2 + (uy / 3.6) ** 2 <= 1.0:
            col = C_EGG
            if math.hypot(ux + 1.0, uy + 1.3) <= 1.05:
                col = WHITE
            a = 1.0
    return col, a


def sample_nest_heart(x, y):
    dx, dy = x - CX, y - CY
    col, a = nest_base(dx, dy)
    s = 5.0
    u, v = dx / s, (-0.82 - dy) / s
    q = u * u + v * v - 1.0
    if q * q * q - u * u * v * v * v <= 0.0:
        col = mix(C_HEART, WHITE, 0.30 * max(0.0, -dy - 1.0) / 5.0)
        a = 1.0
    return col, a


# ── 候选 B:小木屋(侧视)────────────────────────────────────────────
ROOF_APEX, ROOF_EAVE, ROOF_HALF = -11.9, -1.2, 11.9
BODY_TOP, BODY_BOT, BODY_HALF = -1.2, 10.6, 8.6


def sample_house(x, y, lit=False):
    """lit = 亮灯态(球接近时换的那张):只动光的颜色/浓度,几何一个像素都不动。"""
    lite = mix(C_LIGHT, WHITE, 0.42) if lit else C_LIGHT
    lite2 = mix(C_LIGHT2, WHITE, 0.55) if lit else C_LIGHT2
    dx, dy = x - CX, y - CY
    col, a = C_GLOW, glow_layer(math.hypot(dx, dy), GLOW_A_LIT if lit else GLOW_A)

    if BODY_TOP <= dy <= BODY_BOT and abs(dx) <= BODY_HALF:
        col = mix(C_WALLH, C_BASE, 0.22 * (dy - BODY_TOP) / (BODY_BOT - BODY_TOP))
        a = 1.0
    if 8.8 <= dy <= BODY_BOT and abs(dx) <= BODY_HALF:      # 地基压深一档
        col, a = C_BASE, 1.0

    if ROOF_APEX <= dy <= ROOF_EAVE:                        # 屋顶三角 + 屋檐
        halfw = ROOF_HALF * (dy - ROOF_APEX) / (ROOF_EAVE - ROOF_APEX)
        if dy >= -2.6:
            halfw = ROOF_HALF
        if abs(dx) <= halfw:
            col = mix(mix(C_ROOF, WHITE, 0.18), C_ROOF, (dy - ROOF_APEX) / 10.7)
            a = 1.0

            if abs(abs(dx) - halfw) < 0.9 and dy < -2.6:    # 屋脊两坡的受光沿
                col = mix(col, WHITE, 0.30)

    if BODY_TOP <= dy <= 8.8 and abs(abs(dx) - 4.6) < 0.35:  # 木板缝
        col, a = mix(col, C_BASE, 0.45), 1.0

    if math.hypot(dx, dy + 5.2) <= 2.3:                     # 阁楼圆窗
        col, a = mix(lite2, lite, 0.4), 1.0

    door_top, door_half = 4.0, 2.9                          # 拱门 + 暖光
    if abs(dx) <= door_half and door_top <= dy <= BODY_BOT + 0.2:
        col, a = mix(lite, lite2, (BODY_BOT - dy) / 8.0), 1.0
    if dy < door_top and dy >= BODY_TOP and math.hypot(dx, dy - door_top) <= door_half:
        col, a = mix(lite, lite2, 0.6), 1.0

    # 门口洒到地上的一片暖光:整块图最"活"的一笔,幼儿一眼看出"里面有人在等"
    if BODY_BOT + 0.2 < dy <= 13.4:
        if abs(dx) <= door_half + (dy - BODY_BOT) * 1.15:
            col, a = lite, (0.78 if lit else 0.50) * (13.4 - dy) / 2.6
    return col, a


# ── 烘焙(与 render.c bake_*_sprite 同构:4×4 超采样)────────────────
def bake(sample, w=IMG, h=IMG, ss=4):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    n = ss * ss
    for yy in range(h):
        for xx in range(w):
            ar = ag = ab = aa = 0.0
            for sy in range(ss):
                for sx in range(ss):
                    c, al = sample(xx + (sx + 0.5) / ss, yy + (sy + 0.5) / ss)
                    ar += c[0] * al
                    ag += c[1] * al
                    ab += c[2] * al
                    aa += al
            if aa <= 0.0:
                continue
            px[xx, yy] = (int(ar / aa + 0.5), int(ag / aa + 0.5),
                          int(ab / aa + 0.5), int(aa * 255 / n + 0.5))
    return img


# ── 迷宫上下文(拿 maze.c 的 L1 真数据画,不自己编)──────────────────
def load_level1():
    src = re.sub(r"//[^\n]*", "", open(MAZE_C, encoding="utf-8").read())
    g = re.search(r"\.grid\s*=\s*\{(.*?)\}", src, re.S).group(1)
    rows = re.findall(r'"([^"]*)"', g)[:ROWS]
    seg = src[src.index(g):]
    home = re.search(r"\.home\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}", seg)
    start = re.search(r"\.start\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*\}", seg)
    stars = re.search(r"\.stars\s*=\s*\{(.*?)\}\s*,\s*\.n_stars", seg, re.S)
    st = [(int(a), int(b))
          for a, b in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", stars.group(1))]
    return rows, (int(home.group(1)), int(home.group(2))), \
        (int(start.group(1)), int(start.group(2))), st


def star_sprite():
    def vtx(cx, cy, ro, ri):
        return [(cx + (ro if i % 2 == 0 else ri) * math.cos(-math.pi / 2 + i * math.pi / 5),
                 cy + (ro if i % 2 == 0 else ri) * math.sin(-math.pi / 2 + i * math.pi / 5))
                for i in range(10)]
    img = Image.new("RGBA", (80, 80), (0, 0, 0, 0))
    ImageDraw.Draw(img).polygon([(x * 4, y * 4) for x, y in vtx(10.0, 10.8, 9.2, 9.2 * 0.47)],
                                fill=(0xFF, 0xCB, 0x2E, 0xFF))
    return img.resize((20, 20), Image.LANCZOS)


def draw_maze(home_img):
    rows, home, start, stars = load_level1()
    img = Image.new("RGB", (COLS * CELL, ROWS * CELL), C_WALL)
    d = ImageDraw.Draw(img)
    for r, line in enumerate(rows):
        for c, ch in enumerate(line):
            if ch != '#':
                d.rounded_rectangle([c * CELL, r * CELL, c * CELL + CELL - 1,
                                     r * CELL + CELL - 1], radius=4, fill=C_FLOOR)
    img = img.convert("RGBA")
    stp = star_sprite()
    for (c, r) in stars:
        img.alpha_composite(stp, (c * CELL + CELL // 2 - 10, r * CELL + CELL // 2 - 10))
    hx, hy = home[0] * CELL + CELL // 2, home[1] * CELL + CELL // 2
    img.alpha_composite(home_img, (hx - home_img.width // 2, hy - home_img.height // 2))

    ball = Image.new("RGBA", (14, 14), (0, 0, 0, 0))
    bd = ImageDraw.Draw(ball)
    bd.ellipse([0, 0, 13, 13], fill=C_BALL + (0xFF,))
    for ex in (1, 8):
        bd.ellipse([ex, 4, ex + 4, 8], fill=(0xFF, 0xFF, 0xFF, 0xFF))
        bd.ellipse([ex + 1, 5, ex + 3, 7], fill=(0x3A, 0x3A, 0x38, 0xFF))
    sx, sy = start[0] * CELL + CELL // 2, start[1] * CELL + CELL // 2
    img.alpha_composite(ball, (sx - 7, sy - 7))
    return img.convert("RGB")


# ── 出图 ─────────────────────────────────────────────────────────────
F_CJK = "/usr/share/fonts/opentype/noto/NotoSansCJK-Black.ttc"
CANDS = [("now", "现状 · 纯色圆盘", sample_now),
         ("A", "A · 鸟窝 + 两枚蛋", sample_nest_eggs),
         ("B", "B · 小木屋(定案)", sample_house),
         ("C", "C · 鸟窝 + 暖心", sample_nest_heart)]

# 定案版的两态:球进到 ~2 格内(near_level>=2)换成亮灯态
STATES = [("far", "常态 · 球还远", lambda x, y: sample_house(x, y, False)),
          ("lit", "亮灯态 · 球进 2 格内", lambda x, y: sample_house(x, y, True))]


def sheet_zoom(baked, out):
    f = ImageFont.truetype(F_CJK, 20)
    fs = ImageFont.truetype(F_CJK, 14)
    Z, colw = 8, 300
    hh = 40 + IMG * Z + 16 + IMG * 3 + 30
    img = Image.new("RGB", (colw * len(CANDS), hh), (0x1E, 0x22, 0x2B))
    d = ImageDraw.Draw(img)
    for i, ((_, name, _fn), sp) in enumerate(zip(CANDS, baked)):
        x0 = i * colw
        d.text((x0 + colw // 2, 20), name, font=f, fill=(0xF0, 0xF2, 0xF5), anchor="mm")
        bx, by = x0 + (colw - IMG * Z) // 2, 40
        d.rectangle([bx, by, bx + IMG * Z - 1, by + IMG * Z - 1], fill=C_FLOOR)
        c0 = (IMG - CELL) // 2 * Z          # 20px 家格边界(看溢出)
        d.rectangle([bx + c0, by + c0, bx + c0 + CELL * Z - 1, by + c0 + CELL * Z - 1],
                    outline=C_WALL, width=2)
        big = sp.resize((IMG * Z, IMG * Z), Image.NEAREST)
        img.paste(big, (bx, by), big)
        y3 = by + IMG * Z + 16
        for zoom, dx in ((3, -60), (1, 40)):
            w = IMG * zoom
            px = x0 + colw // 2 + dx - w // 2
            d.rectangle([px, y3, px + w - 1, y3 + w - 1], fill=C_FLOOR)
            r = sp.resize((w, w), Image.LANCZOS) if zoom > 1 else sp
            img.paste(r, (px, y3), r)
        d.text((x0 + colw // 2 - 60, y3 + IMG * 3 + 12), "×3", font=fs,
               fill=(0x9A, 0xA3, 0xB2), anchor="mm")
        d.text((x0 + colw // 2 + 40, y3 + IMG * 3 + 12), "×1 实际", font=fs,
               fill=(0x9A, 0xA3, 0xB2), anchor="mm")
    img.save(out)
    return out


def sheet_maze(baked, out):
    f = ImageFont.truetype(F_CJK, 18)
    w, h = int(COLS * CELL * 1.5), int(ROWS * CELL * 1.5)
    rows = (len(CANDS) + 1) // 2
    img = Image.new("RGB", (w * 2 + 24, (h + 30) * rows + 8), (0x1E, 0x22, 0x2B))
    d = ImageDraw.Draw(img)
    for i, ((_, name, _fn), sp) in enumerate(zip(CANDS, baked)):
        x0 = (i % 2) * (w + 24)
        y0 = (i // 2) * (h + 30) + 26
        d.text((x0 + w // 2, y0 - 14), name, font=f, fill=(0xF0, 0xF2, 0xF5), anchor="mm")
        img.paste(draw_maze(sp).resize((w, h), Image.LANCZOS), (x0, y0))
    img.save(out)
    return out


def main():
    global CANDS
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    baked = [bake(fn) for _, _, fn in CANDS]
    for (key, _, _fn), sp in zip(CANDS, baked):
        sp.save(os.path.join(out, "home_%s.png" % key))
    print(sheet_zoom(baked, os.path.join(out, "home_candidates.png")))
    print(sheet_maze(baked, os.path.join(out, "home_in_maze.png")))

    lit = [bake(fn) for _, _, fn in STATES]
    for (key, _, _fn), sp in zip(STATES, lit):
        sp.save(os.path.join(out, "home_state_%s.png" % key))
    CANDS = STATES          # 两态图复用同一套排版
    print(sheet_zoom(lit, os.path.join(out, "home_states.png")))
    print(sheet_maze(lit, os.path.join(out, "home_states_in_maze.png")))


if __name__ == "__main__":
    main()
