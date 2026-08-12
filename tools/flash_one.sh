#!/usr/bin/env bash
# 单刷 / 置空一个 App 的分区槽(偏移对照 tools/flash_map.md,与 partitions.csv 一致)
#
# 🔴 本脚本只认登记过的 app 名 / 槽位名,不收裸偏移——正是为了让"手抖把 0x10000
#    (launcher)当成某个游戏槽擦掉"在语法上就不可能。要动 factory 只能走全量刷。
set -euo pipefail

usage() {
    cat <<'EOF'
用法:
  tools/flash_one.sh <app名|launcher> [PORT]            单刷该 app 的 bin 到自己的槽
  tools/flash_one.sh --erase <app名|ota_N> [PORT]       置空槽位(擦头 4KB,~0.1s)
  tools/flash_one.sh --erase-full <app名|ota_N> [PORT]  整槽 2MB 全擦(十几秒)

  不给 PORT → 只打印命令(本机 WSL 烧不了,拿到 WSL 外执行)
  给 PORT   → 直接执行(需 esptool 可用)
EOF
    exit "${1:-1}"
}

# 槽位偏移(partitions.csv 是唯一真源,此表随之冻结)
declare -A OTA_OFFSET=(
    [ota_0]=0x190000 [ota_1]=0x390000 [ota_2]=0x590000
    [ota_3]=0x790000 [ota_4]=0x990000 [ota_5]=0xB90000
)
# App → 槽位(加新游戏在此登记 + 更新 flash_map.md)
declare -A SLOT=(
    [tilt_maze]=ota_0
    [busy_knobs]=ota_1
    [chick_pour]=ota_2
    [clock_turn]=ota_3
    [chain_lab]=ota_4
    # ota_5 空闲(2026-08-03 fish_pond 放弃删除,见 docs/ROADMAP.md §5)
)

MODE=flash
case "${1:-}" in
    --erase)      MODE=erase;      shift ;;
    --erase-full) MODE=erase_full; shift ;;
    -h|--help)    usage 0 ;;
esac

TARGET="${1:-}"
[[ -n "$TARGET" ]] || usage
PORT="${2:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ "$MODE" != flash ]]; then
    # 擦除按槽位名走:已删 app 的残留镜像(如 ota_5 的 fish_pond)在 SLOT 表里已经没有
    # 名字了,只能直接点槽位名,所以两张表都查。
    SLOT_NAME="${OTA_OFFSET[$TARGET]:+$TARGET}"
    [[ -n "$SLOT_NAME" ]] || SLOT_NAME="${SLOT[$TARGET]:-}"
    [[ -n "$SLOT_NAME" ]] || {
        echo "未知目标 '$TARGET';已登记 App: ${!SLOT[*]};槽位名: ${!OTA_OFFSET[*]}" >&2
        exit 1
    }
    # 擦头 4KB 就够:launcher 判空槽走 esp_ota_get_partition_description(),它只读偏移 0x20
    # 处的 esp_app_desc_t 校验 magic_word——头扇区一没,整槽即判"空",不必等 2MB 擦完。
    # 残留的半截镜像也点不进去:app_slot_launch() 的 esp_ota_set_boot_partition() 自带校验。
    SIZE=0x1000
    [[ "$MODE" == erase_full ]] && SIZE=0x200000
    echo "置空 $SLOT_NAME @ ${OTA_OFFSET[$SLOT_NAME]}(擦 $SIZE);擦完重新上电,卡带架上该格变空位"
    CMD=(python -m esptool --chip esp32 ${PORT:+-p "$PORT"} erase-region "${OTA_OFFSET[$SLOT_NAME]}" "$SIZE")
    echo "${CMD[*]}"
    [[ -n "$PORT" ]] && exec "${CMD[@]}"
    exit 0
fi

if [[ "$TARGET" == "launcher" ]]; then
    echo "launcher 走全量刷(bootloader+分区表+otadata+factory):"
    echo "  idf.py -C $ROOT/launcher -p <PORT> flash"
    exit 0
fi

SLOT_NAME="${SLOT[$TARGET]:-}"
[[ -n "$SLOT_NAME" ]] || { echo "未知 App '$TARGET',已登记: ${!SLOT[*]}" >&2; exit 1; }
BIN="$ROOT/apps/$TARGET/build/$TARGET.bin"
[[ -f "$BIN" ]] || { echo "找不到 $BIN,先: idf.py -C $ROOT/apps/$TARGET build" >&2; exit 1; }

CMD=(python -m esptool --chip esp32 ${PORT:+-p "$PORT"} write-flash "${OTA_OFFSET[$SLOT_NAME]}" "$BIN")
echo "${CMD[*]}"
[[ -n "$PORT" ]] && exec "${CMD[@]}"
exit 0   # 不给 PORT 时上一行的 [[ ]] 为假,没这句脚本会以 1 退出(误导调用方)
