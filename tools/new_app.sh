#!/usr/bin/env bash
# 脚手架:在 apps/ 下新建一个游戏工程(共享 components/ + partitions.csv + sdkconfig.platform)
# 用法: tools/new_app.sh <app名>
# 生成后:1) 写玩法进 apps/<app名>/main/  2) 在 tools/flash_one.sh 的 SLOT 表登记槽位偏移
#        3) idf.py -C apps/<app名> build  4) 单刷进自己的 ota 槽(tools/flash_map.md)
set -euo pipefail

NAME="${1:?用法: tools/new_app.sh <app名>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/apps/$NAME"

[[ ! -e "$DIR" ]] || { echo "apps/$NAME 已存在" >&2; exit 1; }
mkdir -p "$DIR/main"

cat > "$DIR/CMakeLists.txt" <<EOF
# $NAME —— 游戏槽 ota_?(在 tools/flash_map.md / flash_one.sh 登记)
# ⚠️ 烧录:严禁 idf.py flash(会覆盖 factory 的 launcher)!单刷见 tools/flash_map.md
cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "\${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.platform;\${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS "\${CMAKE_CURRENT_LIST_DIR}/../../components")

include(\$ENV{IDF_PATH}/tools/cmake/project.cmake)
project($NAME)
EOF

cat > "$DIR/sdkconfig.defaults" <<EOF
# $NAME 工程差异项(共享默认值见 ../../sdkconfig.platform)
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../partitions.csv"
EOF

cat > "$DIR/main/CMakeLists.txt" <<EOF
idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        m5stack_core_2
        core2_board
        app_slot
        audio_fx
        haptics
)
EOF

cat > "$DIR/main/idf_component.yml" <<EOF
dependencies:
  idf: ">=5.3"
  espressif/m5stack_core_2: "^3.0.1"
  espressif/led_strip: "^3.0.0"
EOF

cat > "$DIR/main/app_main.c" <<EOF
// $NAME —— 新游戏骨架(平台复用指南:docs/platform/BSP_GUIDE.md)
#include "esp_log.h"

#include "app_slot.h"
#include "audio_fx.h"
#include "core2_board.h"
#include "haptics.h"

static const char *TAG = "$NAME";

void app_main(void)
{
    ESP_LOGI(TAG, "=== $NAME 启动 ===");
    app_slot_return_to_factory();   // 第一行:之后任何复位/崩溃都回 launcher

    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;
    ESP_ERROR_CHECK(core2_board_init(&cfg));

    app_slot_enable_button_exit();  // 电源键短按 = 回 launcher

    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);
    // ↓ 从这里写你的玩法(LVGL 记得 bsp_display_lock/unlock;省电用 core2_sleep)
}
EOF

echo "已生成 apps/$NAME/。下一步:"
echo "  1) 在 tools/flash_one.sh 的 SLOT 表 + tools/flash_map.md 登记一个空闲 ota 槽"
echo "  2) launcher/main/app_main.c 可为它加专属图标分支(默认显示通用笑脸)"
echo "  3) idf.py -C apps/$NAME build"
