#!/usr/bin/env python3
"""tilt_maze 关卡库离线验证(README「设计纪律」所指脚本,直接解析 maze.c)。

用法:python3 tools/verify_mazes.py [apps/tilt_maze/main/maze.c]

逐关校验:①BFS 起点→家连通;②全路面格可达(无孤岛);③S/H/*/X 网格字符与
start/home/stars/trap 字段一致;④行数/行宽/边框墙完整;⑤每只巡逻怪的路径拐点在界内、
段段是同行/同列直角折线、整条路径全落在路面上、不压 S/H/星/陷阱、同关两只怪的路径不重叠;
⑥**直线怪**整段(及与陷阱叠加)当永久墙后,起点仍能到"全部必达目标"(家 + 全部星,
星星已改过关必收)——防"必收目标被堵死 = 关卡不可解"。
另报告难度指标(最短路/弯/死胡同/岔口/环)与每只怪的绕行周期。有任一错误退出码非 0。

🔴 **绕圈怪(loop=true)刻意不做 ⑥ 那条"整条路径当墙"的校验**,这不是漏检:
环是图论上的圈,拿掉圈上任意一格,另一条弧仍连通两侧,所以绕圈怪**不可能把迷宫劈成
两半**,只会轮流给圈上每格投下 ~1/n 圈时长的、全程可见且周期恒定的阴影。直线怪没有
这个性质(整段来回扫 = 对幼儿≈永久墙),才必须被赶出必经通道。改成"整环当墙"去查,
等于把绕圈怪也赶回支路,恰好废掉它唯一的价值——**发现"⑥ 没覆盖环"时别顺手补上**。
取而代之的硬约束是⑤(环必须是首尾闭合、不自交、≥8 格的真圈)+ 下方 info 行提示
哪些必达目标位于"单格咽喉"上(那是设计意图:要等怪转开才能过)。

(2026-07-27 加 trap/hazard 字段;同日二次加固 ⑥ 覆盖星可达,起因 L10 的星只有一条
经过巡逻怪的入口。2026-08-03 七写:hazard_a/hazard_b 两点直线 → hazards[] 折线路径 +
loop 绕圈,见 maze.h 顶注。)
"""
import re
import sys
from collections import deque

COLS, ROWS = 16, 12
CELL_PX = 20.0      # maze.h MAZE_CELL
LOOP_SPEED = 44.0   # tuning.h HAZARD_LOOP_SPEED(只用于报告绕圈周期)


def parse(path):
    src = open(path).read()
    # 先剥掉 // 行注释:否则 struct 字段间插的说明注释会撑破字段衔接的 \s*,
    # 令 finditer 把相邻两关粘成一条、悄悄漏关(maze.c 里 '//' 不会出现在字符串内)。
    src = re.sub(r'//[^\n]*', '', src)
    levels = []
    pat = (r'\.id\s*=\s*(\d+).*?\.grid\s*=\s*\{(.*?)\},\s*'
           r'\.start\s*=\s*\{(\d+),\s*(\d+)\},\s*\.home\s*=\s*\{(\d+),\s*(\d+)\}'
           r'(.*?)\.n_stars\s*=\s*(\d+),\s*'
           r'\.trap\s*=\s*\{(-?\d+),\s*(-?\d+)\},\s*'
           r'\.hazards\s*=\s*\{(.*?)\},\s*\.n_hazards\s*=\s*(\d+)')
    haz_pat = (r'\.pts\s*=\s*\{(.*?)\},\s*\.n_pts\s*=\s*(\d+),\s*\.loop\s*=\s*(true|false)')
    for m in re.finditer(pat, src, re.S):
        rows = re.findall(r'"([#.SH*X]+)"', m.group(2))
        stars = [(int(a), int(b)) for a, b in re.findall(r'\{(\d+),\s*(\d+)\}', m.group(7))]
        hazards = []
        for h in re.finditer(haz_pat, m.group(11), re.S):
            pts = [(int(a), int(b)) for a, b in re.findall(r'\{(-?\d+),\s*(-?\d+)\}', h.group(1))]
            hazards.append(dict(pts=pts, n_pts=int(h.group(2)), loop=h.group(3) == 'true'))
        levels.append(dict(id=int(m.group(1)), grid=rows,
                           start=(int(m.group(3)), int(m.group(4))),
                           home=(int(m.group(5)), int(m.group(6))),
                           stars=stars[:int(m.group(8))], n_stars=int(m.group(8)),
                           trap=(int(m.group(9)), int(m.group(10))),
                           hazards=hazards, n_hazards=int(m.group(12))))
    return levels


def path_cells(pts, loop):
    """折线路径覆盖的格子(按行进顺序,闭环不重复首格);段不是直角直线时返回 None。"""
    segs = list(zip(pts, pts[1:])) + ([(pts[-1], pts[0])] if loop else [])
    out = []
    for a, b in segs:
        if a[0] != b[0] and a[1] != b[1]:
            return None
        step = (0 if a[0] == b[0] else (1 if b[0] > a[0] else -1),
                0 if a[1] == b[1] else (1 if b[1] > a[1] else -1))
        cur = a
        while cur != b:
            out.append(cur)
            cur = (cur[0] + step[0], cur[1] + step[1])
        if not loop and (a, b) == segs[-1]:
            out.append(b)
    return out


def nbrs(c, r):
    return [(c + 1, r), (c - 1, r), (c, r + 1), (c, r - 1)]


def reachable(floor, src):
    """floor 里从 src 泛洪能到的格集(src 不在 floor 时返回空集)。"""
    if src not in floor:
        return set()
    seen = {src}
    q = deque([src])
    while q:
        cur = q.popleft()
        for n in nbrs(*cur):
            if n in floor and n not in seen:
                seen.add(n)
                q.append(n)
    return seen


def check(lv):
    errs = []
    g = lv['grid']
    if len(g) != ROWS:
        return [f"行数 {len(g)} != {ROWS}"], None
    for r, row in enumerate(g):
        if len(row) != COLS:
            errs.append(f"第 {r} 行宽 {len(row)} != {COLS}")
    if errs:
        return errs, None
    for c in range(COLS):
        if g[0][c] != '#' or g[ROWS - 1][c] != '#':
            errs.append(f"上下边框第 {c} 列非墙")
    for r in range(ROWS):
        if g[r][0] != '#' or g[r][COLS - 1] != '#':
            errs.append(f"左右边框第 {r} 行非墙")

    fl = {(c, r) for r in range(ROWS) for c in range(COLS) if g[r][c] != '#'}
    for r in range(ROWS):
        for c in range(COLS):
            ch = g[r][c]
            if ch == 'S' and (c, r) != lv['start']:
                errs.append(f"'S'@{(c, r)} 与 start{lv['start']} 不符")
            if ch == 'H' and (c, r) != lv['home']:
                errs.append(f"'H'@{(c, r)} 与 home{lv['home']} 不符")
    if g[lv['start'][1]][lv['start'][0]] != 'S':
        errs.append(f"start{lv['start']} 处无 'S'")
    if g[lv['home'][1]][lv['home'][0]] != 'H':
        errs.append(f"home{lv['home']} 处无 'H'")
    for s in lv['stars']:
        if g[s[1]][s[0]] != '*':
            errs.append(f"star{s} 处无 '*'")
    if len(lv['stars']) != lv['n_stars']:
        errs.append(f"stars 字段 {len(lv['stars'])} 个 != n_stars {lv['n_stars']}")

    # 陷阱格('X'):字段与网格互相印证(是否堵死通路由下方"永久障碍"统一校验)
    x_cells = [(c, r) for r in range(ROWS) for c in range(COLS) if g[r][c] == 'X']
    if lv['trap'][0] < 0:
        if x_cells:
            errs.append(f"trap=(-1,-1) 但网格里有 'X' @ {x_cells}")
    else:
        if lv['trap'] not in x_cells:
            errs.append(f"trap{lv['trap']} 处无 'X'")
        if len(x_cells) > 1:
            errs.append(f"网格里 'X' 不止一个 {x_cells}(本关只声明 trap{lv['trap']})")

    # 巡逻怪路径(不体现在网格字符上,只校验坐标本身)
    specials = {lv['start']: 'S', lv['home']: 'H'}
    for s in lv['stars']:
        specials.setdefault(s, '*')
    if lv['trap'][0] >= 0:
        specials.setdefault(lv['trap'], 'X')

    line_spans, loops, seen_cells = [], [], {}
    if len(lv['hazards']) != lv['n_hazards']:
        errs.append(f"hazards 写了 {len(lv['hazards'])} 只 != n_hazards {lv['n_hazards']}")
    for i, h in enumerate(lv['hazards']):
        tag = f"怪{i + 1}"
        pts, loop = h['pts'], h['loop']
        if len(pts) != h['n_pts']:
            errs.append(f"{tag} pts 写了 {len(pts)} 个 != n_pts {h['n_pts']}")
        if not 2 <= len(pts) <= 6:
            errs.append(f"{tag} 拐点数 {len(pts)} 不在 2~6(MAZE_HAZARD_PTS)")
            continue
        if loop and len(pts) < 4:
            errs.append(f"{tag} loop=true 但只有 {len(pts)} 个拐点,围不出环")
            continue
        bad = [p for p in pts if not (0 <= p[0] < COLS and 0 <= p[1] < ROWS)]
        if bad:
            errs.append(f"{tag} 拐点越界 {bad}")
            continue
        # 相邻拐点相同 → 该段长度 0,game_state.c 里怪会卡死不动(seg_len 判 0 后原地不推进)
        same = [pts[k] for k in range(len(pts) - 1) if pts[k] == pts[k + 1]]
        if same or (loop and pts[0] == pts[-1]):
            errs.append(f"{tag} 有相邻重复拐点 {same or pts[0]}(零长段会让怪卡住;闭环别把首点再写一遍)")
            continue
        cells = path_cells(pts, loop)
        if cells is None:
            errs.append(f"{tag} 有一段既不同行也不同列,巡逻怪只走直角折线")
            continue
        onwall = [p for p in cells if g[p[1]][p[0]] == '#']
        if onwall:
            errs.append(f"{tag} 路径穿墙 {sorted(set(onwall))}")
        # 起点/家/星/陷阱都不能被怪压着走:压 S 会在重生点上转(必死循环),
        # 压 H/星 会让必收目标周期性不可碰,压 X 则两个危险物叠在一起看不清
        hit_sp = sorted({(p, specials[p]) for p in cells if p in specials})
        if hit_sp:
            errs.append(f"{tag} 路径压到 {hit_sp}")
        dup = [p for p in set(cells) if cells.count(p) > 1]
        if dup:
            errs.append(f"{tag} 路径自交 {sorted(dup)}(会原地打转/来回抖)")
        for p in cells:
            if p in seen_cells and seen_cells[p] != i:
                errs.append(f"{tag} 与怪{seen_cells[p] + 1} 的路径在 {p} 重叠(两只怪会穿模)")
                break
            seen_cells[p] = i
        if loop:
            if len(cells) < 8:
                errs.append(f"{tag} 环只有 {len(cells)} 格,太短(每格被遮挡时长占比过高)")
            loops.append((tag, cells))
        else:
            line_spans.append((tag, set(cells)))

    # ── 永久障碍不得封死"必达目标"(2026-07-27 二次加固,统一 trap+hazard)──────
    # 星星已改过关必收(game_state.c 到家判定),故"必达目标" = 家 + 全部星,任一
    # 都不能被永久障碍堵死。永久障碍两类:陷阱 X(踩中即回起点,等价永久墙)、
    # **直线怪**整段(可掐点穿,但对幼儿≈永久墙,必须留免掐点的绕路)。分三档当墙试:
    #   ①陷阱 ②直线怪整段 ③两者同时(最严:完全不躲不掐也能拿全星回家)。
    # 起因:首版只查"巡逻怪不堵到家",漏了 L10 的星恰好只有一条经过巡逻怪的入口。
    # 🔴 绕圈怪(loop)不进这个检查,理由见文件头注——它是圈,永远劈不开迷宫。
    trap = lv['trap'] if lv['trap'][0] >= 0 else None
    targets = [('家', lv['home'])] + [(f'星{i + 1}', s) for i, s in enumerate(lv['stars'])]

    def seal_check(blocked, label):
        if lv['start'] in blocked:
            errs.append(f"{label}压到了起点本身")
            return
        on_block = [nm for nm, p in targets if p in blocked]
        if on_block:
            errs.append(f"{label}压到了必达目标:{','.join(on_block)}")
        reach = reachable(fl - blocked, lv['start'])
        miss = [f"{nm}{p}" for nm, p in targets if p not in blocked and p not in reach]
        if miss:
            errs.append(f"{label}后 起点到不了:{','.join(miss)}(必收目标被堵死 = 不可解)")

    if trap:
        seal_check({trap}, f"陷阱 {trap} 当永久墙")
    for tag, span in line_spans:
        seal_check(span, f"直线{tag}整段 {sorted(span)} 当墙")
        if trap:
            seal_check(span | {trap}, f"直线{tag}整段+陷阱 同时当墙")

    # 绕圈怪:报告哪些必达目标坐落在"环上单格咽喉"后面(要等怪转开才能过)。
    # 这是设计意图不是错误,但一关里咽喉太多会变成频繁罚站,所以要看得见。
    chokes = []
    for tag, cells in loops:
        for p in cells:
            if p == lv['start']:
                continue
            reach = reachable(fl - {p}, lv['start'])
            lost = [nm for nm, t in targets if t != p and t not in reach]
            if lost:
                chokes.append(f"{tag}@{p}→{'/'.join(lost)}")

    dist = {lv['start']: 0}
    prev = {}
    q = deque([lv['start']])
    while q:
        cur = q.popleft()
        for n in nbrs(*cur):
            if n in fl and n not in dist:
                dist[n] = dist[cur] + 1
                prev[n] = cur
                q.append(n)
    if lv['home'] not in dist:
        errs.append("不可解:起点到家不连通!")
        return errs, None
    if set(dist) != fl:
        errs.append(f"孤岛路面格 {sorted(fl - set(dist))}")

    path = [lv['home']]
    while path[-1] != lv['start']:
        path.append(prev[path[-1]])
    path.reverse()
    t = 0
    for i in range(2, len(path)):
        d1 = (path[i - 1][0] - path[i - 2][0], path[i - 1][1] - path[i - 2][1])
        d2 = (path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1])
        if d1 != d2:
            t += 1
    deg = {p: sum(1 for n in nbrs(*p) if n in fl) for p in fl}
    dead = sum(1 for p, d in deg.items() if d == 1 and p not in (lv['start'], lv['home']))
    branch = sum(1 for d in deg.values() if d >= 3)
    edges = sum(1 for p in fl for n in ((p[0] + 1, p[1]), (p[0], p[1] + 1)) if n in fl)
    cycles = edges - len(fl) + 1
    # 怪的节奏:绕圈报"一圈几秒"(周长/HAZARD_LOOP_SPEED),直线报"单程几格"
    haz_desc = [f"{tag}绕{len(c)}格/{len(c) * CELL_PX / LOOP_SPEED:.1f}s" for tag, c in loops]
    haz_desc += [f"{tag}直线{len(s)}格" for tag, s in line_spans]
    return errs, dict(plen=len(path) - 1, turns=t, dead=dead, branch=branch, cycles=cycles,
                      haz=haz_desc, chokes=chokes)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'apps/tilt_maze/main/maze.c'
    levels = parse(path)
    if not levels:
        print(f"⚠ {path} 未解析到任何关卡")
        return 1
    bad = 0
    for lv in levels:
        errs, m = check(lv)
        if errs:
            bad += 1
            print(f"L{lv['id']:>2}: ❌ " + '; '.join(errs))
        else:
            print(f"L{lv['id']:>2}: ✅ 最短路={m['plen']:>3} 弯={m['turns']:>2} "
                  f"死胡同={m['dead']:>2} 岔口={m['branch']:>2} 环={m['cycles']} "
                  f"| {' + '.join(m['haz']) or '无怪'}")
            if m['chokes']:
                print(f"     ↳ 需等怪让路的咽喉:{' '.join(m['chokes'])}")
    print(f"共 {len(levels)} 关,{bad} 关有错")
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
