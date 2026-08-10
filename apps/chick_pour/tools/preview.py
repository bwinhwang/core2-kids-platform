#!/usr/bin/env python3
"""chick_pour 离屏预览 —— 「后院图纸」批的几何真值来源(2026-08-10 落地;初版效果图见 git log)。

根 CLAUDE.md §11 平台坑:「改布局常量先在主机离屏画一遍再烧板」;
ROADMAP §7 立项流程:「SPEC → 主机离屏效果图 → 过 §5 两道实现级验收 → 才轮到写 C」。
WSL 不能烧录,每轮实机都要人工插线,几何冲突在这里当场就能看出来。

**本文件常量 = main/tuning.h + main/layout.h + main/layout.c/scene.c 的落地值,不是提案。**
图纸 A/C/B 已落地进 `main/layout.c` 的图纸表(依次轮换,§5.4);E 漏斗 / D 中央岛
**本批不落地**(E 构图吵、D 需要先做颜色双重编码,评审结论),原样留在下面 BLUEPRINTS
表里当候选池,后续批次要用就直接从这里抄。ui_wide 同理:「加宽家换更大招牌」的方案本批
不做(场地净宽缩水 21% 代价太大),留一帧当参考,不是已批准的待办。

用法:  python3 apps/chick_pour/tools/preview.py [输出目录]
产物:  bp_a_facing / bp_e_funnel / bp_b_sameside / bp_c_diagonal / bp_d_island(候选池)
        + ui_wide(加宽方案参考帧,不是待落地项)

⚠️ 本文件的常量必须与 main/tuning.h、main/layout.h、main/layout.c 保持一致。改一边就改另一边。
⚠️ 本文件的 check_layout() 是 C 版 layout_verify() 的**平行翻译**,两份各写各的 —— 2026-08-13
   就出过"这边全过、C 版三张全挂"。C 侧另有主机回归测试直接编译真身:tools/verify_host.sh。
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

# ── 屏 ────────────────────────────────────────────────────────────────
W, H = 320, 240
SS = 4                      # 超采样倍数(仅预览用,实机靠 LVGL)

# ── 几何(= tuning.h / scene.h,落地值)──────────────────────────────────
FENCE_THICK = 10.0
ANIMAL_R    = 11.0          # 2026-08-10 美术批:8→11(φ22)。§8 护眼线 64px 的在役个案豁免
                             # (ROADMAP §4);脏矩形见 main() 打印,须 < PIXEL_BUDGET。
ANIMAL_N    = 10
HOME_W      = 46.0          # 家的进深(伸进场地)
HOME_H      = 70.0          # 家的沿边高 = 13 顶带 + 44 墙身(=GATE_W) + 13 底带
GATE_W      = 44.0
GATE_DEPTH  = 13.0          # 🔴 2026-08-10 随 ANIMAL_R 8→11 同步上调(原 10):必须 > ANIMAL_R,
                             # 否则动物中心还没进门区、身体已经先蹭到家墙(见 check_layout 断言)。
GATE_INSET  = 6.0           # 门区向墙内嵌(软分离把动物挤进墙面线时仍在豁免区)
BAND        = 13.0          # 顶带/底带厚
SIGN_R      = 12.0          # 招牌脸半径 → φ24。46 进深下的合规上限(见 check_sign_fit),
                             # 再大会压住门洞;要真做大只能加宽家(见 ui_wide 参考帧)。
BUSH_R      = 36.0          # 四角灌木碰撞圆(功能件:把直角变斜坡,防堆死角落)默认半径
BUSH_INSET  = 26.0
BUSH_HI_R   = 12.0          # 灌木高光圆 φ24(碰撞半径不动,只是视觉,§8 降权见配色)

SCATTER_MIN_GAP    = 24.0
SCATTER_GATE_CLEAR = 40.0
SCATTER_MAX_TRIES  = 200

PIXEL_BUDGET = 15000        # §6.2 每帧「在动的像素」预算

# ── 配色(逐色抄自 scene.c / critters.c,落地值)──────────────────────────
C_FENCE      = (0x8A, 0x5A, 0x3C)
C_GRASS      = (0x9E, 0xD9, 0x7A)
C_HOUSE_BODY = (0xE8, 0xC7, 0x9A)
C_ROOF       = (0xD9, 0x48, 0x3A)
C_HOUSE_BASE = (0xB4, 0x85, 0x5A)
C_MAT        = (0xE6, 0xCC, 0x92)
C_DOORFRAME  = (0xFF, 0xF1, 0xCE)
C_DOOR       = (0x45, 0x2F, 0x1D)
C_POND_WATER = (0x5F, 0xB6, 0xDC)
C_POND_RIM   = (0xD9, 0xBC, 0x7E)
C_POND_FRAME = (0xEA, 0xDB, 0xA8)
C_POND_GAP   = (0x1F, 0x33, 0x40)   # 近黑冷色(原 0x2F6E92 深蓝压蓝水,对比度不够,与鸡窝
                                     # 黑洞不同量级;2026-08-10 美术批改定)
C_POND_SHEEN = (0xC5, 0xEC, 0xF7)
C_BUSH       = (0x7C, 0xB8, 0x6F)   # 降对比(原 0x4E8F4A);碰撞半径不动,只降视觉权重
C_BUSH_HI    = (0x8E, 0xC9, 0x80)   # 原 0x6FB86A
C_BAR_H      = (0x8A, 0x62, 0x38)   # 鸡窝天窗条(探头小脸从这儿冒)
C_BAR_P      = (0xA9, 0x89, 0x53)   # 池塘水草条
C_EYE        = (0x3A, 0x3A, 0x38)
C_CHICK      = (0xF7, 0xC2, 0x33)
C_CHICK_HI   = (0xFF, 0xF0, 0xAE)
C_CHICK_BEAK = (0xF0, 0xA0, 0x30)
C_DUCK       = (0xE8, 0xF0, 0xF5)   # 冷白(原 0xF2F2ED 与奶油门框/沙框明度几乎一样,小鸭
                                     # 飘到自己家门口会融进背景);拉开色相、接上池塘蓝色系
C_DUCK_HI    = (0xFF, 0xFF, 0xFF)
C_DUCK_BEAK  = (0xF2, 0xC1, 0x4E)

# ── 加宽方案参考帧(ui_wide;评审已否决本批不做,§8 护眼线够不着的记录,留作后续参考)
UI_HOME_W      = 68.0        # 加宽方案:进深 46→68,换来招牌 φ42
UI_WIDE_SIGN_R = 21.0
DOOR_FRAME_D   = 17.0        # 门框进深(scene.c 定值)
SIGN_PAD       = 5.0

CHICK, DUCK = 0, 1


# ── 图纸数据模型 ──────────────────────────────────────────────────────
class Home:
    """一个家。anchor = 门面所在的 x;face='R' 门朝右(家在左)/'L' 门朝左(家在右)。

    沿边轴恒为 y、进深轴恒为 x —— 见文件头的「零美术改动」约束。
    """

    def __init__(self, kind, face, anchor, cy, w=HOME_W):
        self.kind, self.face, self.cy, self.w = kind, face, cy, w
        if face == 'R':
            self.x0, self.x1 = anchor - w, anchor
        else:
            self.x0, self.x1 = anchor, anchor + w
        self.y0, self.y1 = cy - HOME_H / 2, cy + HOME_H / 2
        self.anchor = anchor

    @property
    def rect(self):
        return (self.x0, self.y0, self.x1, self.y1)

    @property
    def gate(self):
        """门区:跨骑门面,向场内伸 GATE_DEPTH、向墙内嵌 GATE_INSET。"""
        gy0, gy1 = self.cy - GATE_W / 2, self.cy + GATE_W / 2
        if self.face == 'R':
            return (self.anchor - GATE_INSET, gy0, self.anchor + GATE_DEPTH, gy1)
        return (self.anchor - GATE_DEPTH, gy0, self.anchor + GATE_INSET, gy1)

    @property
    def approach(self):
        """门外「进场点」:动物中心要能到这里,门才算不被堵死。"""
        d = GATE_DEPTH + ANIMAL_R + 2
        return (self.anchor + d, self.cy) if self.face == 'R' else (self.anchor - d, self.cy)


class Blueprint:
    def __init__(self, key, name, homes, bushes, note, landed=False):
        self.key, self.name, self.homes, self.bushes, self.note = key, name, homes, bushes, note
        self.landed = landed   # True = 已落地进 main/layout.c 的图纸表(依次轮换)


def corners(r=BUSH_R, inset=BUSH_INSET):
    return [(inset, inset, r), (W - inset, inset, r),
            (inset, H - inset, r), (W - inset, H - inset, r)]


# 🔴 图纸表 —— landed=True 的三张已进 main/layout.c(BP_A/BP_C/BP_B,§5.4 依次轮换)。
#    E/D 是候选池(评审否决/暂缓),留着给以后的批次抄,check_layout() 仍会校验它们
#    (常量以后再变,候选池的几何也要继续成立)。
BLUEPRINTS = [
    Blueprint(
        "a_facing", "A 对门(已落地:main/layout.c BP_A,轮换首张 + 校验失败时的回退图纸)",
        [Home(CHICK, 'R', 56, 120), Home(DUCK, 'L', 264, 120)],
        corners(),
        "两次单轴倾斜:先全体往左、再全体往右。最易,当基准。", landed=True),

    Blueprint(
        "e_funnel", "E 漏斗(候选池,本批不落地:评审认为构图吵)",
        [Home(CHICK, 'R', 56, 120), Home(DUCK, 'L', 264, 120)],
        corners() + [(160, 54, 44), (160, 186, 44)],
        "中央夹出 44px 窄口(≈GATE_W),群体一次只能少量通过 → 逼出轻柔倾斜。"),

    Blueprint(
        "b_sameside", "B 同侧上下(已落地:main/layout.c BP_B)",
        [Home(CHICK, 'R', 56, 72), Home(DUCK, 'R', 56, 168)],
        [(14, 14, 18), (W - BUSH_INSET, BUSH_INSET, BUSH_R),
         (14, H - 14, 18), (W - BUSH_INSET, H - BUSH_INSET, BUSH_R)],
        "两个家都在左边缘。往左倒会同时喂两个门 → 必须先把群体上下分开,斜向两段式。"
        " ⚠️ 也是「错种类堵门」的压力测试图纸:两个门朝同一方向,flock.c 弹出方向必须按"
        " 各自 face 算,不能沿用图纸 A 的写死方向。", landed=True),

    Blueprint(
        "c_diagonal", "C 对角(已落地:main/layout.c BP_C)",
        [Home(CHICK, 'R', 56, 78), Home(DUCK, 'L', 264, 162)],
        [(18, 18, 22), (W - BUSH_INSET, BUSH_INSET, BUSH_R),
         (BUSH_INSET, H - BUSH_INSET, BUSH_R), (W - 18, H - 18, 22)],
        "全场对角搬运,行程最长、群体被拉散得最厉害。", landed=True),

    Blueprint(
        "d_island", "D 中央岛(候选池,本批不落地:需要先做颜色双重编码)",
        [Home(CHICK, 'L', 114, 120), Home(DUCK, 'R', 206, 120)],
        corners(),
        "两家背靠背立在场地正中,门朝左右。动物在外圈要往中心「收」,"
        "推向一侧就把另一侧推离目标。最难。"),
]


# ── 绘制小工具 ────────────────────────────────────────────────────────
def s(v):
    return int(round(v * SS))


def rect(d, box, color, radius=0):
    x0, y0, x1, y1 = [s(v) for v in box]
    if x1 <= x0 or y1 <= y0:
        return
    if radius > 0:
        d.rounded_rectangle([x0, y0, x1 - 1, y1 - 1], radius=s(radius), fill=color)
    else:
        d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=color)


def disc(d, cx, cy, r, color):
    d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=color)


# ── 场景各件 ──────────────────────────────────────────────────────────
def draw_ground(d):
    """栅栏木色铺满 + 内侧草地(= scene.c 的省事画法:露出来的外圈就是栅栏)。"""
    d.rectangle([0, 0, s(W), s(H)], fill=C_FENCE)
    rect(d, (FENCE_THICK, FENCE_THICK, W - FENCE_THICK, H - FENCE_THICK), C_GRASS, 6)


def draw_bushes(d, bushes):
    for (bx, by, br) in bushes:
        disc(d, bx, by, br, C_BUSH)
        hx = bx + (8 if bx < W / 2 else -14)
        hy = by + (8 if by < H / 2 else -14)
        disc(d, hx, hy, BUSH_HI_R * (br / BUSH_R), C_BUSH_HI)


def draw_face(d, cx, cy, r, body, beak):
    """招牌脸 / 探头小脸共用:圆脸 + 两眼 + 喙(按半径等比缩放,C 侧 draw_face_c 同款公式)。"""
    disc(d, cx, cy, r, body)
    e = max(1.0, r * 0.20)
    rect(d, (cx - r * 0.42 - e, cy - r * 0.30 - e, cx - r * 0.42 + e, cy - r * 0.30 + e), C_EYE, e)
    rect(d, (cx + r * 0.42 - e, cy - r * 0.30 - e, cx + r * 0.42 + e, cy - r * 0.30 + e), C_EYE, e)
    rect(d, (cx - r * 0.28, cy + r * 0.18, cx + r * 0.28, cy + r * 0.58), beak, r * 0.15)


def draw_home(d, hm):
    """一个家。鸡窝 = 木屋(顶带/墙身/底带 + 门垫/奶油框/深棕洞);
    池塘 = 沙沿水塘,**逐件镜像同构**(2026-07-12 加强批),只换材质。"""
    x0, y0, x1, y1 = hm.rect
    gy0, gy1 = hm.cy - GATE_W / 2, hm.cy + GATE_W / 2
    door_x = hm.anchor
    out = 1 if hm.face == 'R' else -1        # 场地方向

    if hm.kind == CHICK:
        body, band_t, band_b = C_HOUSE_BODY, C_ROOF, C_HOUSE_BASE
        frame_c, hole_c, bar_c = C_DOORFRAME, C_DOOR, C_BAR_H
        sign_body, sign_beak = C_CHICK, C_CHICK_BEAK
    else:
        body, band_t, band_b = C_POND_WATER, C_POND_RIM, C_POND_RIM
        frame_c, hole_c, bar_c = C_POND_FRAME, C_POND_GAP, C_BAR_P
        sign_body, sign_beak = C_DUCK, C_DUCK_BEAK

    rect(d, (x0, y0, x1, y1), body, 6)
    rect(d, (x0 - 2, y0, x1 + 2, y0 + BAND), band_t, 6)
    rect(d, (x0 - 2, y1 - BAND, x1 + 2, y1), band_b, 6)

    if hm.kind == DUCK:                      # 水面高光(避开门框/招牌)—— 🔴 2026-08-10 修正:
        # 原公式固定按"门在 x0 侧"写(只对 face='L' 成立);图纸 B 的鸭门 face='R',不镜像会被
        # 门框盖掉大半(2026-08-10 落地图纸批实测:preview 里 bp_b_sameside 的水面高光只剩
        # ~7px 可见)。镜像后与门框/招牌一样,恒定"22px 让开门、6px 让开内墙"。
        p1, p2 = door_x - out * 22, door_x - out * (hm.w - 6)
        rect(d, (min(p1, p2), y0 + 17, max(p1, p2), y0 + 23), C_POND_SHEEN, 3)

    # 门三件套:门垫(伸进场地)/ 框(嵌在墙内 17)/ 洞(嵌在墙内 13)
    m0, m1 = sorted([door_x, door_x + out * (GATE_DEPTH + 4)])
    rect(d, (m0, gy0 + 2, m1, gy1 - 2), C_MAT, 4)
    f0, f1 = sorted([door_x, door_x - out * 17])
    rect(d, (f0, gy0 - 2, f1, gy1 + 2), frame_c, 8)
    h0, h1 = sorted([door_x, door_x - out * 13])
    rect(d, (h0, gy0, h1, gy1), hole_c, 8)

    # 天窗条 / 水草条(探头小脸从这儿冒;本预览只画条,不画 φ7 的小脸)
    rect(d, (x0 + 2, y0 + 2, x1 - 2, y0 + 11), bar_c, 3)

    # 招牌脸:门面的另一侧(门占了朝场地那面墙),垂直对齐门洞中线。
    # 🔴 可用进深 = 家的进深 - 门框深(17),招牌必须整个塞进去 —— 见 check_sign_fit()。
    sx = door_x - out * (hm.w - SIGN_R - 5)
    draw_face(d, sx, hm.cy, SIGN_R, sign_body, sign_beak)


def draw_animal(d, x, y, kind):
    """φ(2r) 圆身 + 绒毛高光 + 两眼 + 喙(相对中心的偏移抄 critters.c 的装扮件坐标)。"""
    r = ANIMAL_R
    if kind == CHICK:
        body, hi, beak = C_CHICK, C_CHICK_HI, C_CHICK_BEAK
    else:
        body, hi, beak = C_DUCK, C_DUCK_HI, C_DUCK_BEAK
    disc(d, x, y, r, body)
    disc(d, x - 2.9, y - 3.9, r * 0.42, hi)
    e = 1.3
    rect(d, (x - 3.9 - e, y - 3.1 - e, x - 3.9 + e, y - 3.1 + e), C_EYE, e)
    rect(d, (x + 3.9 - e, y - 3.1 - e, x + 3.9 + e, y - 3.1 + e), C_EYE, e)
    rect(d, (x - 2.6, y + 1.4, x + 2.6, y + 3.4), beak, 1.3)


# ── 布点(= flock_scatter 的约束随机 + 拒绝采样 + 网格兜底)────────────
def blocked(bp, x, y, clear_gates=True):
    """动物中心能不能待在 (x,y):栅栏内 / 不在家里 / 不在灌木里 /(可选)离门区够远。"""
    if not (FENCE_THICK + ANIMAL_R <= x <= W - FENCE_THICK - ANIMAL_R):
        return True
    if not (FENCE_THICK + ANIMAL_R <= y <= H - FENCE_THICK - ANIMAL_R):
        return True
    for hm in bp.homes:
        x0, y0, x1, y1 = hm.rect
        if x0 - ANIMAL_R < x < x1 + ANIMAL_R and y0 - ANIMAL_R < y < y1 + ANIMAL_R:
            return True
        if clear_gates:
            gx0, gy0, gx1, gy1 = hm.gate
            cx, cy = (gx0 + gx1) / 2, (gy0 + gy1) / 2
            if math.hypot(x - cx, y - cy) < SCATTER_GATE_CLEAR:
                return True
    for (bx, by, br) in bp.bushes:
        if math.hypot(x - bx, y - by) < br + ANIMAL_R:
            return True
    return False


def scatter(bp, n=ANIMAL_N, seed=7):
    """约束随机布点;拒绝采样失败则退化为网格兜底(永不失败)。
    @return (点表, 是否走了兜底)"""
    rnd = random.Random(seed)
    pts = []
    for _ in range(n):
        for _try in range(SCATTER_MAX_TRIES):
            x = rnd.uniform(FENCE_THICK + ANIMAL_R, W - FENCE_THICK - ANIMAL_R)
            y = rnd.uniform(FENCE_THICK + ANIMAL_R, H - FENCE_THICK - ANIMAL_R)
            if blocked(bp, x, y):
                continue
            if any(math.hypot(x - px, y - py) < SCATTER_MIN_GAP for px, py in pts):
                continue
            pts.append((x, y))
            break
        else:
            return grid_fallback(bp, n), True
    return pts, False


def grid_fallback(bp, n):
    pts = []
    for gy in range(28, H - 20, 26):
        for gx in range(24, W - 20, 30):
            if not blocked(bp, gx, gy):
                pts.append((float(gx), float(gy)))
                if len(pts) == n:
                    return pts
    return pts


# ── 🔴 加载时校验(= 提案里要写进 C 的那道 scene_verify)──────────────
def flood_free_cells(bp, step=4.0):
    """粗网格 flood fill —— tilt_maze BFS 可解性校验的开阔场地降级版。"""
    cols = int(W / step) + 1
    rows = int(H / step) + 1
    free = [[not blocked(bp, c * step, r * step, clear_gates=False)
             for r in range(rows)] for c in range(cols)]
    # 从场地里第一个自由格灌水
    start = None
    for c in range(cols):
        for r in range(rows):
            if free[c][r]:
                start = (c, r)
                break
        if start:
            break
    seen = set([start])
    stack = [start]
    while stack:
        c, r = stack.pop()
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nc, nr = c + dc, r + dr
            if 0 <= nc < cols and 0 <= nr < rows and free[nc][nr] and (nc, nr) not in seen:
                seen.add((nc, nr))
                stack.append((nc, nr))
    return seen, step


def check_sign_fit(w, sign_r, tag):
    """🔴 招牌脸必须整个塞进「家的进深 - 门框」里,否则会盖住门洞。

    2026-08-10 实证:UI 提案初版把招牌放到 φ42 塞进 46 进深的家,渲染出来招牌压在门洞上
    —— 正是 CLAUDE.md §11 记的「中心定位的对象 vs 边界约束的常量」那类错法(pipe_garden
    一次踩中三处)。招牌按中心定位,进深是边界约束,少减一个门框深就整体越界。
    """
    limit = (w - DOOR_FRAME_D - SIGN_PAD) / 2
    assert sign_r <= limit, (
        f"{tag} 招牌脸 φ{2 * sign_r:.0f} 塞不进进深 {w:.0f} 的家(上限 φ{2 * limit:.0f})"
        f" —— 会盖住门洞")


def check_layout():
    """几何断言 —— 跑不过就别烧板。所有 assert 都对应一条会在实机上咬人的失败。
    对应 main/layout.c 的 layout_verify():这里 assert 硬失败(工具脚本,拍板用);
    C 侧同样的条件校验失败只 ESP_LOGE + 回退图纸 A,不 abort(§2 原则 1"永不失败")。"""
    check_sign_fit(HOME_W, SIGN_R, "[落地值]")
    check_sign_fit(UI_HOME_W, UI_WIDE_SIGN_R, "[ui_wide 参考帧]")
    # 🔴 2026-08-10 新增:ANIMAL_R 8→11 美术批之后必须重新验证的物理不变量 ——
    # 动物中心必须先进门区判定区、再可能碰到家墙,否则会在门口边缘出现"先小蹭一下墙、
    # 再被门判定接管"的 1px 窗口(见 scene.h GATE_DEPTH 注释 / 交付说明)。
    assert GATE_DEPTH > ANIMAL_R, \
        f"GATE_DEPTH({GATE_DEPTH}) 必须 > ANIMAL_R({ANIMAL_R}),否则门判定可能被家墙碰撞抢先"
    # 主角护眼自查(§8;放大后脏矩形复核,ROADMAP §4 在役个案豁免的是"多大算够大"不是"预算")
    px = ANIMAL_N * ((2 * ANIMAL_R + 4) ** 2) * 2
    assert px <= PIXEL_BUDGET, f"ANIMAL_R={ANIMAL_R} 脏矩形 {px:.0f}px/帧 超 §6.2 预算 {PIXEL_BUDGET}"

    for bp in BLUEPRINTS:
        tag = f"[{bp.key}]"
        assert len(bp.homes) == 2, f"{tag} 家的数量不是 2(音阶/计数逻辑按 2 家写死)"

        for hm in bp.homes:
            x0, y0, x1, y1 = hm.rect
            assert FENCE_THICK <= x0 and x1 <= W - FENCE_THICK, f"{tag} 家横向出栅栏"
            assert FENCE_THICK <= y0 and y1 <= H - FENCE_THICK, f"{tag} 家纵向出栅栏"
            gx0, gy0, gx1, gy1 = hm.gate
            assert 0 < gx0 and gx1 < W, f"{tag} 门区出屏"
            # 门要朝着场地,不能顶着栅栏
            ax, ay = hm.approach
            assert FENCE_THICK + ANIMAL_R <= ax <= W - FENCE_THICK - ANIMAL_R, \
                f"{tag} 门外进场点 {ax:.0f} 落在栅栏里 —— 门朝向错了"

        # 两个家不能撞在一起
        a, b = bp.homes
        assert not (a.x0 < b.x1 and b.x0 < a.x1 and a.y0 < b.y1 and b.y0 < a.y1) or \
               (a.x1 == b.x0 or b.x1 == a.x0), f"{tag} 两个家的外墙重叠"

        # 🔴 灌木不许压住家(pipe_garden 教训:中心定位的对象 vs 边界约束的常量)
        for (bx, by, br) in bp.bushes:
            for hm in bp.homes:
                x0, y0, x1, y1 = hm.rect
                nx, ny = min(max(bx, x0), x1), min(max(by, y0), y1)
                assert math.hypot(bx - nx, by - ny) >= br, \
                    f"{tag} 灌木({bx:.0f},{by:.0f},r{br:.0f})压住家 {x0:.0f},{y0:.0f}"
            # 灌木也不许堵在门外的进场点上
            for hm in bp.homes:
                ax, ay = hm.approach
                assert math.hypot(bx - ax, by - ay) >= br + ANIMAL_R, \
                    f"{tag} 灌木堵死了门外进场点"

        # 🔴 两个门都必须从场地走得到(flood fill;门被地形封死 = 这一轮打不完)
        seen, step = flood_free_cells(bp)
        for hm in bp.homes:
            ax, ay = hm.approach
            cell = (int(round(ax / step)), int(round(ay / step)))
            assert cell in seen, \
                f"{tag} {'鸡窝' if hm.kind == CHICK else '池塘'}的门从场地走不到 —— 被地形封死"

        # 🔴 散点放得下 10 只(放不下 = 每轮开局都走网格兜底,重散就白做了)
        pts, fell_back = scatter(bp)
        assert len(pts) == ANIMAL_N, f"{tag} 连网格兜底都摆不下 {ANIMAL_N} 只(只摆下 {len(pts)})"
        bp.fell_back = fell_back


# ── 出帧 ──────────────────────────────────────────────────────────────
# 加宽方案参考帧的图纸 A:家的进深 46→68,两个家紧贴栅栏,场地净宽随之缩水(本批不做)。
BP_WIDE = Blueprint(
    "wide", "A 对门(加宽参考帧:进深 46→68,本批不做)",
    [Home(CHICK, 'R', FENCE_THICK + UI_HOME_W, 120, UI_HOME_W),
     Home(DUCK, 'L', W - FENCE_THICK - UI_HOME_W, 120, UI_HOME_W)],
    corners(),
    "招牌脸换到 φ42 的代价:场地净宽 208 → 164px。")


def frame(bp, path, seed=7, sign_r=None, home_w=None):
    img = Image.new("RGB", (W * SS, H * SS), C_GRASS)
    d = ImageDraw.Draw(img)

    draw_ground(d)
    for hm in bp.homes:                       # 画序同 scene.c:先家,后灌木
        draw_home(d, hm)
    draw_bushes(d, bp.bushes)

    pts, _ = scatter(bp, seed=seed)
    for i, (x, y) in enumerate(pts):
        draw_animal(d, x, y, CHICK if i % 2 == 0 else DUCK)

    img.resize((W, H), Image.LANCZOS).save(path)
    return path


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    check_layout()
    os.makedirs(out, exist_ok=True)
    p = lambda n: os.path.join(out, n)

    print("── 图纸(landed=已进 main/layout.c;其余是候选池)──")
    for bp in BLUEPRINTS:
        frame(bp, p(f"bp_{bp.key}.png"))
        flag = "  ⚠️ 散点走了网格兜底" if getattr(bp, "fell_back", False) else ""
        tag = "✅ landed" if bp.landed else "· 候选池"
        print(f"  bp_{bp.key}.png   [{tag}] {bp.name}{flag}")

    print("── 加宽参考帧(评审已否决,本批不做)──")
    frame(BP_WIDE, p("ui_wide.png"))
    print("  ui_wide.png")

    px = ANIMAL_N * ((2 * ANIMAL_R + 4) ** 2) * 2
    mark = "✅" if px <= PIXEL_BUDGET else "❌ 超预算"
    print(f"\n脏矩形/帧(§6.2 预算 {PIXEL_BUDGET}):10 只 × φ{2 * ANIMAL_R:.0f} → {px:.0f}px  {mark}")
    print(f"招牌脸:φ{2 * SIGN_R:.0f}(46 进深上限,§8 核心信息线 64px 够不着,"
          f"加宽到 {UI_HOME_W:.0f} 进深可到 φ{2 * UI_WIDE_SIGN_R:.0f} 但本批不做)")
    print(f"GATE_DEPTH {GATE_DEPTH:.0f} > ANIMAL_R {ANIMAL_R:.0f}(门判定先于家墙碰撞的不变量,✅)")


if __name__ == "__main__":
    main()
