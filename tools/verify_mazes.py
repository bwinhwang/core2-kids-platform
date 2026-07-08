#!/usr/bin/env python3
"""tilt_maze 关卡库离线验证(README「设计纪律」所指脚本,直接解析 maze.c)。

用法:python3 tools/verify_mazes.py [apps/tilt_maze/main/maze.c]

逐关校验:①BFS 起点→家连通;②全路面格可达(无孤岛);③S/H/* 网格字符与
start/home/stars 字段一致;④行数/行宽/边框墙完整。另报告难度指标
(最短路格数/弯数/死胡同/岔口/环路),供"同难度带"复核。有任一错误退出码非 0。
"""
import re
import sys
from collections import deque

COLS, ROWS = 16, 12


def parse(path):
    src = open(path).read()
    levels = []
    pat = (r'\.id\s*=\s*(\d+).*?\.grid\s*=\s*\{(.*?)\},\s*'
           r'\.start\s*=\s*\{(\d+),\s*(\d+)\},\s*\.home\s*=\s*\{(\d+),\s*(\d+)\}'
           r'(.*?)\.n_stars\s*=\s*(\d+)')
    for m in re.finditer(pat, src, re.S):
        rows = re.findall(r'"([#.SH*]+)"', m.group(2))
        stars = [(int(a), int(b)) for a, b in re.findall(r'\{(\d+),\s*(\d+)\}', m.group(7))]
        levels.append(dict(id=int(m.group(1)), grid=rows,
                           start=(int(m.group(3)), int(m.group(4))),
                           home=(int(m.group(5)), int(m.group(6))),
                           stars=stars[:int(m.group(8))], n_stars=int(m.group(8))))
    return levels


def nbrs(c, r):
    return [(c + 1, r), (c - 1, r), (c, r + 1), (c, r - 1)]


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
