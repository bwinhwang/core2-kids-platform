#!/usr/bin/env bash
# 加一个 UNIT 外设:复制 unit_template + 改名 + 生成文档雏形
# 用法: tools/add_unit.sh <name> <port: A|B|C>
set -euo pipefail

NAME="${1:?用法: tools/add_unit.sh <name> <A|B|C>}"
PORT="${2:-A}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/components/units/unit_template"
DST="$ROOT/components/units/unit_${NAME}"

[ -d "$DST" ] && { echo "已存在: $DST"; exit 1; }

cp -r "$SRC" "$DST"
mv "$DST/unit_template.c"          "$DST/unit_${NAME}.c"
mv "$DST/include/unit_template.h"  "$DST/include/unit_${NAME}.h"
grep -rl unit_template "$DST" | xargs sed -i "s/unit_template/unit_${NAME}/g"

DOC="$ROOT/docs/units/${NAME}.md"
cp "$ROOT/docs/units/_UNIT_TEMPLATE.md" "$DOC"
sed -i "s/__UNIT__/${NAME}/g; s/__PORT__/${PORT}/g" "$DOC"

echo "已创建:"
echo "  组件  components/units/unit_${NAME}"
echo "  文档  docs/units/${NAME}.md  (接 PORT.${PORT})"
echo "下一步:"
echo "  1) 填 docs/units/${NAME}.md(地址/寄存器/坑,查 MCP 后写 Confirmed via)"
echo "  2) 在 main/CMakeLists.txt 的 REQUIRES 加 unit_${NAME}"
echo "  3) main 里 unit_${NAME}_init(bsp_i2c_port_a(), 0x??)"
