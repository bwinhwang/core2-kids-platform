#!/usr/bin/env python3
"""clock_turn 离屏 UI 预览 —— 按 SPEC §6.1 的同一套几何算式在主机画典型帧。

根 CLAUDE.md §11 平台坑:「改布局常量先在主机离屏画一遍再烧板」。WSL 不能烧录,
每轮实机都要人工介入,几何冲突(对象互相罩住/出屏/顶穿分带)在这里当场就能看出来。

2026-08-06 大改版(语音整章作废,两模式落地):
  - 状态条①从纯展示徽标升级成两位模式开关(人形=FREE/屏形=QUIZ)+ 长按进度条(SPEC §5.3.5)
  - 反馈脸(原"说话脸")三态改成 idle/yay/huh,talk 态删除(SPEC §5.3.4)
  - 渐进提示弧改走时针角、半径 50(SPEC §5.6/§6.1),不再是分针角/半径 84
  - 数字钟揭晓机制从"轻触整齐时刻播报"改成"按键揭晓"(SPEC §5.4)

2026-08-10 降难度批(用户实机反馈"对幼儿难度偏高"):
  - 时针改砖红 C_HAND_HOUR —— 推翻 §5.3.1「两针同色」硬规矩,代价记在 SPEC §5.3.1 修订段
  - 步进 MIN_PER_STEP 5→15 分钟(tuning.h,本文件无输入不含该常量)→ **样帧时刻必须是
    15 的倍数**,否则画的是实机永远转不到的姿态(本轮已把 6:50 / 7:35 两帧改掉)

用法:  python3 apps/clock_turn/tools/preview.py [输出目录]
产物:  见 main() 底部的样帧列表(320x240,与实机同分辨率)

⚠️ 本文件里的常量必须与 main/tuning.h 保持一致。改一边就改另一边。
"""
import math
import os
import sys
from PIL import Image, ImageDraw, ImageFont

# ── 屏 ────────────────────────────────────────────────────────────────
W, H = 320, 240
SS = 4                      # 超采样倍数(仅预览用,实机靠 LVGL 抗锯齿)

# ── 钟面几何(= tuning.h) ─────────────────────────────────────────────
CLOCK_CX, CLOCK_CY, CLOCK_R = 101, 120, 95    # 钟面右沿 = 196,右侧留出读数走廊
HAND_MIN_LEN, HAND_MIN_W = 72, 5       # 分针:细而长
HAND_HOUR_LEN, HAND_HOUR_W = 42, 8     # 时针:粗而短(1.6x 宽、0.58x 长),短于数字圈
CAP_R = 8                              # 中心帽(随指针变细同步收小,否则显得臃肿)
HAND_EDGE = 2                          # 指针描边(钟面色):6:30 这类两针近重叠时的分界
TICK_OUT, TICK_MAJ_IN, TICK_MIN_IN = 89, 79, 82
NUM_RING_R, NUM_H = 64, 13             # 刻度数字圈(装饰级,不受 64px 约束)

# 🔴 渐进提示弧(SPEC §5.6/§6.1):2026-08-06 改走**时针角**,半径 50。
#    分针角每 60 分钟绕回一次——目标差 95 分钟时分针弧只显示 35 分(吞掉"多一圈"),
#    给的是错的提示;时针角 = t*0.5° 在 720 分钟周期内单调不重复,唯一编码 Δt。
#    半径卡在时针尖(42+4=46)与数字圈内沿(64-6.5=57.5)之间,不遮不挡。
HINT_R = 50
HINT_ARC_W1, HINT_ARC_W2 = 4, 7        # 第1次按错 / 第2次及以后的弧宽(§5.6)

# ── 右侧信息区(三个子区) ────────────────────────────────────────────
# 🔴 走廊 = 钟面右沿 196 → 屏右 320,只有 124px。子区**只能纵向切**:
#    横向切必爆(最宽读数「12:45」自己就占 ~98px)。
# 🔴 子区**数量上限由根 CLAUDE.md §8 定死**:240px 高最多叠 3 条(孩子向的两条各 ≥64px),
#    切到 4 条则每条 ≤60px,等于把孩子向的信息降级成家长向的装饰。
INFO_X0, INFO_X1 = 201, 315            # 信息区左右沿(钟面最宽处 x=196,留 5px)
CARD_R = 8                             # 子区圆角

Z_STAT = (INFO_X0,   6, INFO_X1,  40)  # ① 状态条(**家长向**):两位模式开关 + 连接 + 电量
Z_TIME = (INFO_X0,  46, INFO_X1, 148)  # ② 数字钟(孩子向):读数 = 信息区主角
Z_FACE = (INFO_X0, 154, INFO_X1, 234)  # ③ 反馈脸(孩子向):idle/yay/huh

# 🔴 读数尺寸由**最宽读数**倒推,不是拍脑袋:「12:45」5 字符,还要塞进子区②的内腔
#    (半宽 57、答题态再减 3px 描边)。放大 READOUT_H 或加宽钟面前必先重算,
#    check_layout() 已把它做成断言。
PANEL_CX, PANEL_CY = 258, 97           # = 子区② 的中心
READOUT_H = 25                         # 数字钟读数「7:30」横排 —— 标准写法,无歧义
PANEL_HW, PANEL_HH = 57, 51            # = 子区② 的半宽/半高
PANEL_BORDER = 3                       # 答题态橙描边宽度(占内腔)

FACE_R = 34                            # 反馈脸半径 → φ68,压着根 CLAUDE.md §8 的 64px 线

# ── 状态条①:两位模式开关几何(SPEC §5.3.5,2026-08-06 新增)────────────
MODE_SLOT_W, MODE_SLOT_H = 27, 20
MODE_SLOT_Y0             = 11          # 槽 y 11..31
MODE_A_X0, MODE_B_X0     = 207, 237    # 槽A 207..234(人形=FREE),槽B 237..264(屏形=QUIZ)
LINK_CX, LINK_CY, LINK_R = 277, 21, 4  # 旋钮连接点(在=绿/拔了=红)
BATT_X0, BATT_Y0, BATT_W, BATT_H = 288, 15, 21, 12   # 电量壳(本轮只画静态满格)
HOLD_BAR_X0, HOLD_BAR_X1 = 205, 311    # 长按进度条,宽度随进度从 X0 长到 X1
HOLD_BAR_Y0, HOLD_BAR_H  = 33, 4

# ── 配色(暖色、扁平、压低亮度;RGB565 友好) ──────────────────────────
C_BG        = (44, 48, 62)
C_FACE      = (245, 232, 205)
C_RIM       = (198, 158, 98)
C_TICK_MAJ  = (140, 100, 60)
C_TICK_MIN  = (196, 172, 138)
C_NUM       = (112, 80, 48)
C_HAND      = (58, 50, 44)      # 中心帽 + 反馈脸五官(暖黑)。中心帽刻意保持中性:染成任一针的
                                #   颜色都会让那根针在根部"长出去一截",破坏长短这条几何线索。
C_HAND_HOUR = (196, 69, 58)     # ★ 时针:砖红(2026-08-10 用户改判,推翻 §5.3.1「两针同色」,
                                #   代价=真挂钟上没有这条线索,见 SPEC §5.3.1 修订段)。
                                #   长短 42:72 + 粗细 8:5 这两条原生区分一条都不撤,颜色是第三条冗余。
C_HAND_MIN  = (30, 110, 120)    # ★ 分针:深青。与时针红配对 →「红=时针/小时、青=分针/分钟」两条规则。
                                #   钟面对比度 4.87,与时针 4.06 分量相当,谁都不压过谁。
C_DIGIT_MIN = (127, 216, 224)   # ★ 读数的**分钟**段:C_HAND_MIN 的提亮档(底卡 6.49/6.94)
C_DIGIT_SEP = (143, 152, 171)   # ★ 读数的**冒号**:中性灰蓝,不跟红也不跟青(理由见 tuning.h)
C_DIGIT_HOUR = (255, 138, 122)  # ★ 读数的**小时**段:C_HAND_HOUR 的提亮档(同色相 ~6°)。
                                #   不能直接用砖红:指针在浅色钟面、数字在深色底卡,对比度要求
                                #   一个要 ≤0.233 亮度、一个要 ≥0.250,两区间不相交(见 tuning.h 注释)。
C_CAP       = (58, 50, 44)
C_QUIZ      = (255, 154, 60)    # 暖橙:QUIZ 模式的统一强调色
C_HINT      = (255, 196, 72)    # 提示弧-第1次按错(暗)
C_HINT2     = (255, 224, 140)   # 提示弧-第2次及以后(更亮)
C_GREEN     = (110, 200, 120)   # QUIZ 答对:数字变绿
C_CARD      = (56, 62, 80)      # 子区底卡(比背景略亮,划出信息区)
C_CARD_Q    = (74, 54, 36)      # QUIZ 态的子区②(暖橙暗调)
C_CARD_HI   = (78, 86, 108)     # 状态条①选中槽的底(比 C_CARD 更亮一档)
C_MUTED     = (122, 130, 150)   # 家长向图标 —— 刻意压暗,不跟钟面抢注意力
C_LINK_OK   = (120, 190, 120)
C_LINK_BAD  = (206, 96, 84)

F_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf"


def pol(cx, cy, r, deg):
    """0° = 12 点方向,顺时针为正(与 tuning.h / clock_model 同一约定)。"""
    a = math.radians(deg - 90)
    return cx + r * math.cos(a), cy + r * math.sin(a)


def hand_angles(t):
    """t = 分钟数(0..719) → (时针角, 分针角)。核心齿轮比,与 clock_model.c 同式。"""
    return t * 0.5, (t % 60) * 6.0


def hint_arc_angles(hour_now, hour_target):
    """渐进提示弧的 (start_deg, end_deg,含符号的扫过量)——走**短边方向**(SPEC §1.4)。

    两个候选弧:顺时针从 now 到 target,或逆时针。取角度更小(<=180°)的那个。
    返回 (start, sweep):sweep>0 表示从 start 顺时针画 sweep 度到 target。
    """
    cw = (hour_target - hour_now) % 360.0          # 顺时针从 now 到 target 的角度,[0,360)
    if cw <= 180.0:
        return hour_now, cw
    ccw = 360.0 - cw                                # 逆时针更短 == 顺时针从 target 到 now
    return hour_target, ccw


def font(px):
    """px = 期望的数字实际高度(非 em)。DejaVu 数字高 ≈ 0.73 em。"""
    return ImageFont.truetype(F_BOLD, int(px * SS / 0.73))


def draw_face_static(d, t):
    """静态层:圆盘 + 边框 + 60 刻度 + 12 数字。实机进场画一次,之后永不重画。"""
    cx, cy, r = CLOCK_CX * SS, CLOCK_CY * SS, CLOCK_R * SS
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=C_FACE,
              outline=C_RIM, width=5 * SS)
    for i in range(60):
        deg = i * 6
        maj = (i % 5 == 0)
        r_in = (TICK_MAJ_IN if maj else TICK_MIN_IN) * SS
        x1, y1 = pol(cx, cy, r_in, deg)
        x2, y2 = pol(cx, cy, TICK_OUT * SS, deg)
        d.line((x1, y1, x2, y2), fill=C_TICK_MAJ if maj else C_TICK_MIN,
               width=(5 if maj else 2) * SS)
    f = font(NUM_H)
    for n in range(1, 13):
        x, y = pol(cx, cy, NUM_RING_R * SS, n * 30)
        d.text((x, y), str(n), font=f, fill=C_NUM, anchor="mm")


def draw_hand(d, deg, length, width, color):
    """两针同色,靠长短粗细区分(真表就是这样)。描边只在两针重叠时显形。"""
    cx, cy = CLOCK_CX * SS, CLOCK_CY * SS
    x, y = pol(cx, cy, length * SS, deg)
    for w, c in ((width + HAND_EDGE * 2, C_FACE), (width, color)):
        d.line((cx, cy, x, y), fill=c, width=w * SS)
        rr = w * SS / 2
        d.ellipse((x - rr, y - rr, x + rr, y + rr), fill=c)   # 圆头


def draw_cap(d):
    cx, cy, r = CLOCK_CX * SS, CLOCK_CY * SS, CAP_R * SS
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=C_CAP)


def rrect(d, box, **kw):
    x0, y0, x1, y1 = box
    d.rounded_rectangle((x0 * SS, y0 * SS, x1 * SS, y1 * SS), radius=CARD_R * SS, **kw)


def draw_cards(d, quiz):
    """信息区三块底卡。**实机属静态层**:进场画一次,之后只有子区②在换态时重画一次。
    分区本身因此是零常态开销的 —— 帧预算表(§6.2)不变。
    """
    rrect(d, Z_STAT, fill=C_CARD)
    if quiz:                                    # 子区②的边框 = 模式指示(不再单独画橙框)
        rrect(d, Z_TIME, fill=C_CARD_Q, outline=C_QUIZ, width=PANEL_BORDER * SS)
    else:
        rrect(d, Z_TIME, fill=C_CARD)
    rrect(d, Z_FACE, fill=C_CARD)


def draw_mode_icon_person(d, cx, cy, on):
    """槽 A:人形图标 = MODE_FREE(大人出题)。圆头 + 圆角矩形肩,纯几何、无文字。"""
    color = C_QUIZ if on else C_MUTED
    hr = 3.5 * SS
    hy = cy - 5 * SS
    d.ellipse((cx - hr, hy - hr, cx + hr, hy + hr), fill=color)
    sw, sh = 15 * SS, 8 * SS
    sy0 = hy + hr - 1 * SS
    d.rounded_rectangle((cx - sw / 2, sy0, cx + sw / 2, sy0 + sh), radius=4 * SS, fill=color)


def draw_mode_icon_screen(d, cx, cy, on):
    """槽 B:屏形图标 = MODE_QUIZ(机器出题)。圆角矩形边框 + 两条横线,示意"屏幕显示题面"。"""
    color = C_QUIZ if on else C_MUTED
    w, h = 17 * SS, 13 * SS
    d.rounded_rectangle((cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2),
                        radius=3 * SS, outline=color, width=2 * SS)
    d.line((cx - 4 * SS, cy - 2 * SS, cx + 4 * SS, cy - 2 * SS), fill=color, width=2 * SS)
    d.line((cx - 4 * SS, cy + 2 * SS, cx + 1 * SS, cy + 2 * SS), fill=color, width=2 * SS)


def draw_status(d, quiz, linked=True, batt=1.0, hold_frac=0.0):
    """① 状态条 —— **家长向**,刻意做小做暗,2026-08-06 升级成可交互的两位模式开关。

    孩子不需要它:模式对孩子已由子区②表达(§5.3.2.1)。这条是给经过的大人一眼看的:
    现在是哪个模式 / 旋钮还连着吗 / 还有电吗。⚠️ 这里的图标豁免根 CLAUDE.md §8 的
    64px —— 它们不承载孩子需要的核心信息(同 12 刻度数字)。
    """
    y0 = MODE_SLOT_Y0
    # 槽 A(人形=FREE)
    ax0, ax1 = MODE_A_X0, MODE_A_X0 + MODE_SLOT_W
    if not quiz:
        rrect(d, (ax0, y0, ax1, y0 + MODE_SLOT_H), fill=C_CARD_HI)
    draw_mode_icon_person(d, (ax0 + ax1) / 2 * SS, (y0 + MODE_SLOT_H / 2) * SS, on=(not quiz))
    # 槽 B(屏形=QUIZ)
    bx0, bx1 = MODE_B_X0, MODE_B_X0 + MODE_SLOT_W
    if quiz:
        rrect(d, (bx0, y0, bx1, y0 + MODE_SLOT_H), fill=C_CARD_HI)
    draw_mode_icon_screen(d, (bx0 + bx1) / 2 * SS, (y0 + MODE_SLOT_H / 2) * SS, on=quiz)

    # 旋钮连接点:绿 = 在,红 = 拔了
    lx, ly, lr = LINK_CX * SS, LINK_CY * SS, LINK_R * SS
    d.ellipse((lx - lr, ly - lr, lx + lr, ly + lr), fill=C_LINK_OK if linked else C_LINK_BAD)

    # 电量:本轮只画静态壳(满格),不接 AXP192 真读数(SPEC §5.3 TODO)
    bx0p, by0p = BATT_X0 * SS, BATT_Y0 * SS
    bx1p, by1p = (BATT_X0 + BATT_W) * SS, (BATT_Y0 + BATT_H) * SS
    d.rounded_rectangle((bx0p, by0p, bx1p, by1p), radius=2 * SS, outline=C_MUTED, width=2 * SS)
    d.rectangle((bx1p, by0p + 3.5 * SS, bx1p + 3 * SS, by1p - 3.5 * SS), fill=C_MUTED)
    pad = 3 * SS
    d.rectangle((bx0p + pad, by0p + pad, bx0p + pad + (BATT_W * SS - 2 * pad) * batt,
                by1p - pad), fill=C_MUTED)

    # 长按进度条(按住状态条切模式期间从 X0 长到 X1;松手不足时长则不画/归零)
    if hold_frac > 0.0:
        hx0, hy0 = HOLD_BAR_X0 * SS, HOLD_BAR_Y0 * SS
        hx1, hy1 = HOLD_BAR_X1 * SS, (HOLD_BAR_Y0 + HOLD_BAR_H) * SS
        d.rounded_rectangle((hx0, hy0, hx1, hy1), radius=2 * SS, fill=(40, 44, 58))
        cur = hx0 + (hx1 - hx0) * min(1.0, hold_frac)
        d.rounded_rectangle((hx0, hy0, cur, hy1), radius=2 * SS, fill=C_QUIZ)


def draw_panel(d, t, quiz=False, reveal=False, correct=False):
    """② 数字钟读数 —— 🔴 **不是实时的**,与按键动作同门槛(SPEC §5.3.2.1/§5.4)。

    ⚠️ 横排「7:30」是标准写法。竖排(小时一行、分钟一行)读起来像三位数 730,试过,不行。
    ⚠️ 2026-08-10 起小时段染 C_DIGIT_HOUR、分钟段染 C_DIGIT_MIN(两针颜色各自的提亮档),
       与钟面的「红=时针、青=分针」对上;原「小时与分钟不分色」的规矩随 §5.3.1 一起改判,
       代价见那一节修订段。
    🔴 **MODE_FREE 默认只显示占位点**:实时读数会让孩子直接读数字、一眼都不看指针 ——
       数字对他不是"冗余的第二通道",是**更便宜的那条通道**,3~4 岁一定走最省力的路。
       按旋钮键揭晓,数秒后淡回占位。
    🔴 **MODE_QUIZ 显示的是目标时刻且锁定**,那是"题面"不是"答案",必须给——孩子能自己读题,
       不必等大人念(§0 独自可玩)。但绝不能同时给当前时刻(ROADMAP §6 辅助线索检验)。
    """
    cx, cy = PANEL_CX * SS, PANEL_CY * SS
    if not quiz and not reveal:                 # 占位点:有格子,但不泄答案
        for k in (-1, 0, 1):
            r = 4 * SS
            d.ellipse((cx + k * 17 * SS - r, cy - r, cx + k * 17 * SS + r, cy + r),
                      fill=(92, 100, 120))
        return
    hh = (t // 60) or 12
    # 🔴 读数恒为「红小时 + 青分钟」→ MODE_QUIZ 原本的"读数常亮橙"孩子向模式信号没了,
    #    只剩底卡(暖橙暗底 + 橙描边)。见 SPEC §12 风险 1。
    color = C_GREEN if correct else C_DIGIT_MIN
    f = font(READOUT_H)
    s_h, s_sep, s_m = f"{hh}", ":", f"{t % 60:02d}"
    # 三段分别染色(实机走 LVGL 行内着色,这里手工拼)。整串仍按合并宽度居中 —— 不能各自
    # 居中,否则会叠在一起。🔴 分段宽度用 getlength(前进宽度)不用 getbbox:冒号左右
    # 有可观的 side bearing,按墨迹宽度累加会让分钟段压到冒号上。
    w_h, w_sep, w_m = (f.getlength(x) for x in (s_h, s_sep, s_m))
    x0 = cx - (w_h + w_sep + w_m) / 2
    # 答对庆祝态整串变绿,不掺第二色(§7)
    h_color = color if correct else C_DIGIT_HOUR
    sep_color = color if correct else C_DIGIT_SEP
    d.text((x0, cy), s_h, font=f, fill=h_color, anchor="lm")
    d.text((x0 + w_h, cy), s_sep, font=f, fill=sep_color, anchor="lm")
    d.text((x0 + w_h + w_sep, cy), s_m, font=f, fill=color, anchor="lm")


def draw_face(d, mood):
    """③ 反馈脸 —— 把只有声/震/灯的事变成看得见的事(2026-08-06 由"说话脸"改名)。

    idle 平静微笑(常态,两模式共同基线)/ yay 大笑+眼睛弯成弧(QUIZ 答对)/
    huh 歪头+波浪嘴(QUIZ 按键但未到位)。原 talk(张嘴说话)态随语音一起删除。
    """
    cx, cy = PANEL_CX * SS, (Z_FACE[1] + Z_FACE[3]) / 2 * SS
    r = FACE_R * SS
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=C_FACE)   # 脸用钟面同色

    if mood == "yay":                                          # 大笑:眼睛弯成弧
        for ex in (-13, 13):
            d.arc((cx + ex * SS - 6 * SS, cy - 13 * SS, cx + ex * SS + 6 * SS, cy - 3 * SS),
                  start=200, end=340, fill=C_HAND, width=3 * SS)
        d.chord((cx - 15 * SS, cy - 2 * SS, cx + 15 * SS, cy + 18 * SS),
                start=10, end=170, fill=C_HAND)
        return

    for ex in (-13, 13):                                       # 眼(idle/huh 共用圆眼)
        er = 4.5 * SS
        ey = cy - 9 * SS + (2 * SS if mood == "huh" and ex < 0 else 0)   # huh 歪头:左眼略低
        d.ellipse((cx + ex * SS - er, ey - er, cx + ex * SS + er, ey + er), fill=C_HAND)

    if mood == "huh":
        d.arc((cx - 2 * SS, cy - 24 * SS, cx + 20 * SS, cy - 12 * SS),   # 挑起的眉
              start=180, end=340, fill=C_HAND, width=3 * SS)
        # ⚠️ 疑问的嘴必须是波浪线,不能用小圆 —— 小圆跟"张嘴说话"太像,语音已砍,
        #    孩子会以为"它在说话"而干等一个永远不会来的声音(§5.3.4 定案)。
        for k, sx in enumerate((-11, -1, 9)):
            d.arc((cx + sx * SS, cy + (7 if k % 2 else 3) * SS,
                   cx + (sx + 12) * SS, cy + (15 if k % 2 else 11) * SS),
                  start=180 if k % 2 else 0, end=360 if k % 2 else 180,
                  fill=C_HAND, width=3 * SS)
    else:                                                       # idle:平静微笑
        d.arc((cx - 14 * SS, cy - 2 * SS, cx + 14 * SS, cy + 16 * SS),
              start=15, end=165, fill=C_HAND, width=3 * SS)


def draw_hint_arc(d, hour_now, hour_target, width):
    """渐进提示:走**时针角**(不是分针角,理由见文件头),半径 HINT_R,走短边方向。

    SPEC §5.6:第 1 次按错细而暗(width=HINT_ARC_W1),第 2 次及以后粗而亮(HINT_ARC_W2,
    颜色换 C_HINT2)。答对/换题隐藏(不画即可,调用方控制)。
    """
    a0, sweep = hint_arc_angles(hour_now, hour_target)
    cx, cy, r = CLOCK_CX * SS, CLOCK_CY * SS, HINT_R * SS
    color = C_HINT2 if width >= HINT_ARC_W2 else C_HINT
    d.arc((cx - r, cy - r, cx + r, cy + r),
          start=a0 - 90, end=a0 - 90 + sweep, fill=color, width=width * SS)
    xe, ye = pol(cx, cy, r, a0 + sweep)                  # 终点圆点(指向目标一端)
    rr = (width / 2 + 2) * SS
    d.ellipse((xe - rr, ye - rr, xe + rr, ye + rr), fill=color)


def new_canvas():
    img = Image.new("RGB", (W * SS, H * SS), C_BG)
    return img, ImageDraw.Draw(img)


def finish(img, path):
    img.resize((W, H), Image.LANCZOS).save(path)
    print(path)


def frame(t, path, *, quiz_target=None, miss=0, win=False, mood="idle",
          linked=True, reveal=False, hold_frac=0.0):
    """通用出图:两模式共用一套渲染管线。

    @param t            当前钟面 t(分钟)。
    @param quiz_target  非 None = MODE_QUIZ,值 = 目标时刻;None = MODE_FREE。
    @param miss         MODE_QUIZ 按错次数(0=无提示弧,1=细弧,>=2=粗亮弧)。
    @param win          是否画庆祝(单次泛光 + 12 数字点亮 + 面板变绿)。
    @param mood         反馈脸表情:idle / yay / huh。
    @param reveal       MODE_FREE 是否处于"按键揭晓"态(显示当前读数)。
    @param hold_frac    状态条①长按进度 0..1(0 = 不画进度条)。
    """
    img, d = new_canvas()
    cx, cy = CLOCK_CX * SS, CLOCK_CY * SS
    if win:                                     # 庆祝:单次柔和泛光(非连闪,根§8 光敏安全)
        for k, rr in enumerate([CLOCK_R + 18, CLOCK_R + 11, CLOCK_R + 5]):
            g = 70 + k * 30
            d.ellipse((cx - rr * SS, cy - rr * SS, cx + rr * SS, cy + rr * SS),
                      outline=(g + 100, g + 55, 70), width=4 * SS)
    draw_face_static(d, t)
    if win:                                     # 刻度数字点亮
        f = font(NUM_H)
        for n in range(1, 13):
            x, y = pol(cx, cy, NUM_RING_R * SS, n * 30)
            d.text((x, y), str(n), font=f, fill=C_QUIZ, anchor="mm")
    quiz = quiz_target is not None
    if quiz and miss > 0 and not win:
        ha, _ = hand_angles(t)
        ta, _ = hand_angles(quiz_target)
        draw_hint_arc(d, ha, ta, HINT_ARC_W2 if miss >= 2 else HINT_ARC_W1)
    ha, ma = hand_angles(t)
    # 🔴 画序:分针在下、**时针在上**,不可交换(2026-08-06 实机截图实证)。
    #    分针描边宽 HAND_MIN_W+2*HAND_EDGE = 9 > 时针本体宽 HAND_HOUR_W = 8 —— 分针若后画,
    #    两针重合时(12:00 最典型)那圈钟面色描边会把时针整条抹掉,屏上只剩一条 5px 细线,
    #    "粗短=时针"这条唯一的区分线索当场失效(SPEC §5.3.1)。时针在上则得到
    #    「粗短桩 + 细长尖」,重合姿态照样读得出来。clock_ui.c::create_dynamic_hands 同此序。
    draw_hand(d, ma, HAND_MIN_LEN, HAND_MIN_W, C_HAND_MIN)
    draw_hand(d, ha, HAND_HOUR_LEN, HAND_HOUR_W, C_HAND_HOUR)
    draw_cap(d)
    # 信息区
    draw_cards(d, quiz)
    draw_status(d, quiz, linked, hold_frac=hold_frac)
    draw_panel(d, quiz_target if quiz else t, quiz, reveal=reveal, correct=win)
    draw_face(d, "yay" if win else mood)
    finish(img, path)


def frame_no_unit(path):
    """没插单元:M0 简化提示(SPEC §13 M0 已完成,完整无字提示卡是 M6 范围)。
    这里仍画出完整提示卡供 M6 施工时参考——插头图标 + 箭头指 PORT.C(机身侧面蓝口)。
    """
    img, d = new_canvas()
    cx, cy = 126 * SS, 120 * SS
    body = (70, 120, 190)                                              # 蓝 = PORT.C
    d.rounded_rectangle((cx - 46 * SS, cy - 34 * SS, cx + 46 * SS, cy + 34 * SS),
                        radius=10 * SS, fill=body)                     # 连接器本体
    d.rounded_rectangle((cx + 34 * SS, cy - 16 * SS, cx + 62 * SS, cy + 16 * SS),
                        radius=6 * SS, fill=body)                      # 前端插舌
    for dy in (-18, -6, 6, 18):                                        # 四针
        d.rounded_rectangle((cx + 40 * SS, cy + (dy - 4) * SS,
                             cx + 60 * SS, cy + (dy + 4) * SS),
                            radius=3 * SS, fill=(214, 206, 186))
    for dy in (-20, 0, 20):                                            # 线缆
        d.rounded_rectangle((cx - 92 * SS, cy + (dy - 5) * SS,
                             cx - 40 * SS, cy + (dy + 5) * SS),
                            radius=5 * SS, fill=(96, 104, 122))
    ax, ay = 250 * SS, 120 * SS                                        # 箭头指向机身侧面接口
    d.polygon([(ax + 40 * SS, ay), (ax - 4 * SS, ay - 30 * SS), (ax - 4 * SS, ay + 30 * SS)],
              fill=C_QUIZ)
    d.rounded_rectangle((ax - 44 * SS, ay - 10 * SS, ax - 2 * SS, ay + 10 * SS),
                        radius=5 * SS, fill=C_QUIZ)
    finish(img, path)


def check_layout():
    """几何自检 —— 挡住「中心定位的对象 vs 边界约束的常量」这一类错(根 CLAUDE.md §11)。

    2026-08-06 实录:READOUT_H=33 时「11:15」宽 129px,右栏只有 92px —— 左压钟面 3 点刻度、
    右切出屏,10/11/12 点全中(四分之一时刻)。只渲了 7:30/3:00 这种一位数小时,看不出来。
    """
    f = font(READOUT_H)
    worst = max((f.getbbox(f"{h}:{m:02d}")[2] / SS, f"{h}:{m:02d}")
                for h in range(1, 13) for m in (0, 45, 55))
    w, s = worst
    assert w + 2 * PANEL_BORDER + 8 <= 2 * PANEL_HW, \
        f"读数「{s}」宽 {w:.0f} 撑破子区②内腔(半宽 {PANEL_HW}、答题态描边 {PANEL_BORDER})"

    # 🔴 钟面是**圆**:每个子区要按自己那段 y 里钟面最宽的地方算,不能拿右沿 196 一刀切。
    #    只有横跨 y=120 的子区②才真正面对 196;上下两条其实宽松得多。
    bands = (("① 状态条", Z_STAT), ("② 数字钟", Z_TIME), ("③ 反馈脸", Z_FACE))
    for name, (x0, y0, x1, y1) in bands:
        ys = [y0, y1] + ([CLOCK_CY] if y0 <= CLOCK_CY <= y1 else [])
        rim = max(CLOCK_CX + math.sqrt(max(0.0, CLOCK_R ** 2 - (y - CLOCK_CY) ** 2))
                  for y in ys)
        assert x0 > rim, f"子区「{name}」左沿 {x0} 压住钟面(该 y 段钟面最宽到 {rim:.0f})"
        assert x1 < W, f"子区「{name}」右沿 {x1} 出屏({W})"
    for (_, a), (nb, b) in zip(bands, bands[1:]):
        assert a[3] < b[1], f"子区「{nb}」与上一条纵向重叠"
    assert bands[0][1][1] >= 0 and bands[-1][1][3] <= H, "信息区纵向出屏"

    # 根 CLAUDE.md §8 大对象:孩子向的两条子区里,主对象最小边 ≥64px(家长向的①豁免)
    assert 2 * FACE_R >= 64, f"反馈脸 φ{2 * FACE_R} < 64px 护眼线"
    assert HAND_HOUR_LEN + HAND_HOUR_W / 2 < NUM_RING_R - NUM_H / 2, "时针盖住刻度数字"
    assert HAND_MIN_LEN < CLOCK_R, "分针出钟面"
    # 两针的**几何**区分线索(SPEC §5.3.1):时针必粗必短。两条都塌了就没法读了。
    # ⚠️ 2026-08-10 时针改砖红后这两条断言**不放宽**:颜色是加上去的第三条冗余,不是几何的
    #    替代品——真挂钟上没有颜色线索,几何才是要迁移出去的那一条(SPEC §5.3.1 修订段)。
    assert HAND_HOUR_W > HAND_MIN_W, "时针不比分针粗 —— 重合时无从区分"
    assert HAND_HOUR_LEN < HAND_MIN_LEN, "时针不比分针短 —— 重合时无从区分"

    # 🔴 2026-08-06 新增:状态条①两位模式开关的自检(SPEC §5.3.5)。
    a_x0, a_x1 = MODE_A_X0, MODE_A_X0 + MODE_SLOT_W
    b_x0, b_x1 = MODE_B_X0, MODE_B_X0 + MODE_SLOT_W
    assert a_x1 <= b_x0, f"模式槽 A({a_x0}..{a_x1})与槽 B({b_x0}..{b_x1})重叠"
    assert b_x1 < LINK_CX - LINK_R, f"槽 B 右沿 {b_x1} 挤到连接点({LINK_CX}±{LINK_R})"
    assert LINK_CX + LINK_R < BATT_X0, "连接点压到电量壳"
    assert BATT_X0 + BATT_W + 3 <= INFO_X1, \
        f"电量右沿(含极耳){BATT_X0 + BATT_W + 3} 超出信息区右沿({INFO_X1})"
    assert MODE_SLOT_Y0 + MODE_SLOT_H <= HOLD_BAR_Y0, "长按进度条压住模式槽"
    assert HOLD_BAR_Y0 + HOLD_BAR_H <= Z_STAT[3], "长按进度条超出状态条卡片下沿"
    assert HOLD_BAR_X0 >= Z_STAT[0] and HOLD_BAR_X1 <= Z_STAT[2], "长按进度条超出状态条卡片左右沿"

    # 🔴 提示弧半径须卡在时针尖与数字圈内沿之间(SPEC §6.1),不遮时针、不越过装饰刻度数字。
    hour_tip = HAND_HOUR_LEN + HAND_HOUR_W / 2
    num_inner = NUM_RING_R - NUM_H / 2
    assert hour_tip < HINT_R < num_inner, \
        f"HINT_R={HINT_R} 须在时针尖 {hour_tip:.0f} 与数字圈内沿 {num_inner:.1f} 之间"

    print(f"[layout ok] 最宽读数「{s}」{w:.0f}px,子区②内腔 {2 * PANEL_HW - 2 * PANEL_BORDER}px;"
          f" 反馈脸 φ{2 * FACE_R};提示弧半径 {HINT_R}(时针尖{hour_tip:.0f}~数字圈{num_inner:.1f}"
          f" 之间);三子区均未压钟面;模式槽/进度条/连接点/电量互不重叠")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    check_layout()
    os.makedirs(out, exist_ok=True)
    p = lambda n: os.path.join(out, n)

    # ── MODE_FREE ──────────────────────────────────────────────────────
    frame(7 * 60 + 30, p("free_idle_0730.png"))                         # ★ M1 验收帧延续:7:30 时针须在 7/8 正中
    frame(7 * 60 + 30, p("free_reveal_0730.png"), reveal=True)           # 按键揭晓:数字亮起
    frame(12 * 60 - 45, p("free_reveal_1115.png"), reveal=True)          # ★ 两位数小时,读数最宽的一类
    frame(6 * 60 + 30, p("free_idle_0630.png"))                          # ★ M2 验收帧:两针夹角仅 15°,同色最难一关
    frame(12 * 60, p("free_idle_1200.png"))                              # ★ 画序回归帧:两针 100% 重合的唯一时刻
    frame(7 * 60 + 30, p("free_unlinked.png"), linked=False)             # 旋钮拔了(状态条转红点)
    frame(7 * 60 + 30, p("mode_hold_50.png"), hold_frac=0.5)             # 长按切模式进行中(FREE→QUIZ 半途)
    frame(7 * 60 + 30, p("mode_hold_100.png"), hold_frac=1.0)            # 长按满 1.5s(即将切换)

    # ── MODE_QUIZ ──────────────────────────────────────────────────────
    frame(5 * 60, p("quiz_pending.png"), quiz_target=7 * 60 + 30)                    # 出题态,尚未按错过
    frame(5 * 60, p("quiz_huh_miss1.png"), quiz_target=7 * 60 + 30, miss=1, mood="huh")   # 第1次按错:细暗弧
    frame(6 * 60 + 45, p("quiz_huh_miss2.png"), quiz_target=7 * 60 + 30, miss=2, mood="huh")  # 第2次+按错:粗亮弧
    # Δt=105min 跨小时的题:验证时针角(不是分针角)给出正确的短边方向
    frame(6 * 60, p("quiz_huh_cross_hour.png"), quiz_target=7 * 60 + 45, miss=1, mood="huh")
    frame(7 * 60 + 30, p("quiz_win.png"), quiz_target=7 * 60 + 30, win=True)          # 答对:庆祝+绿字+yay脸

    frame_no_unit(p("no_unit.png"))


if __name__ == "__main__":
    main()
