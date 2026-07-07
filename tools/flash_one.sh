#!/usr/bin/env bash
# 单刷一个 App 的 bin 到它的分区槽(偏移对照 tools/flash_map.md,与 partitions.csv 一致)
# 用法: tools/flash_one.sh <app名|launcher> [PORT]
#   不给 PORT → 只打印命令(本机 WSL 烧不了,拿到 WSL 外执行)
#   给 PORT   → 直接执行(需 esptool 可用)
set -euo pipefail

APP="${1:?用法: tools/flash_one.sh <app名|launcher> [PORT]}"
PORT="${2:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# 槽位分配(加新游戏在此登记 + 更新 flash_map.md)
declare -A SLOT=(
    [tilt_maze]=0x190000
    [busy_knobs]=0x390000
    [peekaboo]=0x590000
    [feed_monster]=0x790000
    [chain_lab]=0x990000
)

if [[ "$APP" == "launcher" ]]; then
    echo "launcher 走全量刷(bootloader+分区表+otadata+factory):"
    echo "  idf.py -C $ROOT/launcher -p <PORT> flash"
    exit 0
fi

OFFSET="${SLOT[$APP]:-}"
[[ -n "$OFFSET" ]] || { echo "未知 App '$APP',已登记: ${!SLOT[*]}" >&2; exit 1; }
BIN="$ROOT/apps/$APP/build/$APP.bin"
[[ -f "$BIN" ]] || { echo "找不到 $BIN,先: idf.py -C $ROOT/apps/$APP build" >&2; exit 1; }

CMD=(python -m esptool --chip esp32 ${PORT:+-p "$PORT"} write-flash "$OFFSET" "$BIN")
echo "${CMD[*]}"
[[ -n "$PORT" ]] && exec "${CMD[@]}"
