#!/usr/bin/env bash
# 加一个 UNIT 外设:复制 unit_template + 改名 + 生成文档雏形
# 用法: tools/add_unit.sh <name> <port: A|B|C> [doc_name]
#   name     组件基名,生成 components/units/unit_<name>(**强制小写**)
#   port     A=外部I2C / B=ADC·DAC·GPIO / C=UART(含 Chain 系列)
#   doc_name 文档文件名(不含 .md),默认 Unit_<name>;Chain 类传 Chain_<xxx>
set -euo pipefail

RAW="${1:?用法: tools/add_unit.sh <name> <A|B|C> [doc_name]}"
PORT="${2:-A}"
NAME="$(printf '%s' "$RAW" | tr '[:upper:]' '[:lower:]')"   # 组件名强制小写
DOCNAME="${3:-Unit_${RAW}}"                                  # 文档显示名(保留大小写)
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/components/units/unit_template"
DST="$ROOT/components/units/unit_${NAME}"

[ -d "$DST" ] && { echo "已存在: $DST"; exit 1; }

cp -r "$SRC" "$DST"
mv "$DST/unit_template.c"          "$DST/unit_${NAME}.c"
mv "$DST/include/unit_template.h"  "$DST/include/unit_${NAME}.h"
grep -rl unit_template "$DST" | xargs sed -i "s/unit_template/unit_${NAME}/g"

DOC="$ROOT/docs/units/${DOCNAME}.md"
cp "$ROOT/docs/units/_UNIT_TEMPLATE.md" "$DOC"
sed -i "s/__UNIT__/${RAW}/g; s/__PORT__/${PORT}/g" "$DOC"

echo "已创建:"
echo "  组件  components/units/unit_${NAME}"
echo "  文档  docs/units/${DOCNAME}.md  (接 PORT.${PORT})"
echo "下一步(照新模板 7 节填):"
echo "  1) §2 引脚用 Core2 三口表(A=G32/33 · B=G26出/G36入 · C=G13/14),别抄别的板"
echo "  2) §3 寄存器/协议查 MCP 后写 Confirmed via;§1/§4/§6/§7 补齐"
echo "  3) main/CMakeLists.txt 的 REQUIRES 加 unit_${NAME}"
echo "  4) main 里接入:I2C→ unit_${NAME}_init(bsp_i2c_port_a(), 0x??);GPIO/UART 用 bsp_port.h 宏"
