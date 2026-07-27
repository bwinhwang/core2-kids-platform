#!/usr/bin/env python3
"""tilt_maze 关卡库离线验证(README「设计纪律」所指脚本,直接解析 maze.c)。

用法:python3 tools/verify_mazes.py [apps/tilt_maze/main/maze.c]

逐关校验:①BFS 起点→家连通;②全路面格可达(无孤岛);③S/H/*/X 网格字符与
start/home/stars/trap 字段一致;④行数/行宽/边框墙完整;⑤hazard_a/hazard_b
(若本关有巡逻怪)在界内、非墙、同行或同列直线;⑥永久障碍(陷阱格 / 巡逻怪整段 /
两者叠加)当墙后,起点仍能到"全部必达目标"(家 + 全部星,星星已改过关必收)——
防"必收目标被堵死 = 关卡不可解"。另报告难度指标(最短路/弯/死胡同/岔口/环)。
有任一错误退出码非 0。(2026-07-27 加 trap/hazard 字段;同日二次加固 ⑥ 覆盖星可达,
起因 L10 的星只有一条经过巡逻怪的入口,见 maze.h 顶注。)
"""
import re
import sys
from collections import deque

COLS, ROWS = 16, 12


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
           r'\.hazard_a\s*=\s*\{(-?\d+),\s*(-?\d+)\},\s*'
           r'\.hazard_b\s*=\s*\{(-?\d+),\s*(-?\d+)\}')
    for m in re.finditer(pat, src, re.S):
        rows = re.findall(r'"([#.SH*X]+)"', m.group(2))
        stars = [(int(a), int(b)) for a, b in re.findall(r'\{(\d+),\s*(\d+)\}', m.group(7))]
        levels.append(dict(id=int(m.group(1)), grid=rows,
                           start=(int(m.group(3)), int(m.group(4))),
                           home=(int(m.group(5)), int(m.group(6))),
                           stars=stars[:int(m.group(8))], n_stars=int(m.group(8)),
                           trap=(int(m.group(9)), int(m.group(10))),
                           hazard_a=(int(m.group(11)), int(m.group(12))),
                           hazard_b=(int(m.group(13)), int(m.group(14)))))
    return levels


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

    # 巡逻怪端点(不体现在网格字符上,只校验坐标本身):界内、非墙、同行或同列直线
    ha, hb = lv['hazard_a'], lv['hazard_b']
    span = set()
    if ha[0] < 0 or hb[0] < 0:
        if ha[0] >= 0 or hb[0] >= 0:
            errs.append(f"hazard_a{ha}/hazard_b{hb} 只设了一个 -1,应成对")
    else:
        for name, p in (('hazard_a', ha), ('hazard_b', hb)):
            if not (0 <= p[0] < COLS and 0 <= p[1] < ROWS):
                errs.append(f"{name}{p} 越界")
            elif g[p[1]][p[0]] == '#':
                errs.append(f"{name}{p} 落在墙上")
        if ha[0] != hb[0] and ha[1] != hb[1]:
            errs.append(f"hazard_a{ha}/hazard_b{hb} 不同行也不同列,巡逻怪只走直线")
        elif 0 <= ha[0] < COLS and 0 <= ha[1] < ROWS and 0 <= hb[0] < COLS and 0 <= hb[1] < ROWS:
            if ha[1] == hb[1]:
                span = {(c, ha[1]) for c in range(min(ha[0], hb[0]), max(ha[0], hb[0]) + 1)}
            else:
                span = {(ha[0], r) for r in range(min(ha[1], hb[1]), max(ha[1], hb[1]) + 1)}

    # ── 永久障碍不得封死"必达目标"(2026-07-27 二次加固,统一 trap+hazard)──────
    # 星星已改过关必收(game_state.c 到家判定),故"必达目标" = 家 + 全部星,任一
    # 都不能被永久障碍堵死。永久障碍两类:陷阱 X(踩中即回起点,等价永久墙)、
    # 巡逻怪整段(可掐点穿,但对幼儿≈永久墙,必须留免掐点的绕路)。分三档当墙试:
    #   ①陷阱 ②巡逻怪整段 ③两者同时(最严:完全不躲不掐也能拿全星回家)。
    # 起因:首版只查"巡逻怪不堵到家",漏了 L10 的星恰好只有一条经过巡逻怪的入口。
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
    if span:
        seal_check(span, f"巡逻怪整段 {sorted(span)} 当墙")
    if trap and span:
        seal_check(span | {trap}, "巡逻怪整段+陷阱 同时当墙")

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
    return errs, dict(plen=len(path) - 1, turns=t, dead=dead, branch=branch, cycles=cycles)


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
                  f"死胡同={m['dead']:>2} 岔口={m['branch']:>2} 环={m['cycles']}")
    print(f"共 {len(levels)} 关,{bad} 关有错")
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
