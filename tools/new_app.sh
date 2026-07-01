#!/usr/bin/env bash
# 实例化:把模板改成一个新工程名(平台固定 Core2+Bottom2,只改名)
# 用法: tools/new_app.sh <project_name>
set -euo pipefail

NAME="${1:?用法: tools/new_app.sh <project_name>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 顶层 CMake 的 project() 改名
sed -i "s/^project(.*/project(${NAME})/" "$ROOT/CMakeLists.txt"
# CLAUDE.md 标题占位替换
sed -i "s/__PROJECT__/${NAME}/g" "$ROOT/CLAUDE.md" || true

echo "工程名已设为 '${NAME}'。"
echo "下一步:"
echo "  1) 填 docs/HARDWARE.md(接了哪些 UNIT/哪个口)与 CLAUDE.md(产品目标)"
echo "  2) source IDF 环境后:idf.py set-target esp32 && idf.py build"
