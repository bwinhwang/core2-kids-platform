#!/usr/bin/env bash
# 脚手架:在 apps/ 下新建一个评估台工程(共享 components/ + partitions.csv + sdkconfig.platform)
# 用法: tools/new_app.sh <app名>
# 生成后:1) 写评估逻辑进 apps/<app名>/main/  2) 在 tools/flash_one.sh 的 SLOT 表登记槽位偏移
#        3) idf.py -C apps/<app名> build  4) 单刷进自己的 ota 槽(tools/flash_map.md)
set -euo pipefail

NAME="${1:?用法: tools/new_app.sh <app名>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/apps/$NAME"

[[ ! -e "$DIR" ]] || { echo "apps/$NAME 已存在" >&2; exit 1; }
mkdir -p "$DIR/main"

cat > "$DIR/CMakeLists.txt" <<EOF
# $NAME —— 评估槽 ota_?(在 tools/flash_map.md / flash_one.sh 登记)
# ⚠️ 烧录:严禁 idf.py flash(会覆盖 factory 的 launcher)!单刷见 tools/flash_map.md
cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "\${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.platform;\${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "\${CMAKE_CURRENT_LIST_DIR}/../../components"
    "\${CMAKE_CURRENT_LIST_DIR}/../../components/units")

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
        core2_sleep
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
// $NAME —— 新评估台骨架(平台复用指南:docs/platform/BSP_GUIDE.md)
#include "esp_log.h"

#include "app_slot.h"
#include "audio_fx.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "haptics.h"

static const char *TAG = "$NAME";

void app_main(void)
{
    ESP_LOGI(TAG, "=== $NAME 启动 ===");
    app_slot_return_to_factory();   // 第一行:之后任何复位/崩溃都回 launcher

    core2_board_cfg_t cfg = CORE2_BOARD_CFG_DEFAULT;
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        // 可观测优先:init 失败一律屏上显式呈现(错误码 + 原因),不静默——
        // 这里先留日志占位,新 app 应换成真正的错误画面(ui_kit 就绪后用
        // ui_value_card_set_error 或整屏红字提示,见 CLAUDE.md §2 原则 2)。
        ESP_LOGE(TAG, "平台初始化失败(%s)", esp_err_to_name(err));
        return;
    }

    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);

    // ↓ 从这里写评估逻辑(LVGL 记得 bsp_display_lock/unlock;数值/图表控件见
    //   components/ui_kit/README.md;持久化标定见 components/kv_store/README.md;
    //   串口 CSV 导出见 components/data_log/README.md)
    //
    // 100ms 主循环示例(评估台不追求 60Hz 游戏手感,数值/波形按需推点即可):
    //   TickType_t last = xTaskGetTickCount();
    //   core2_sleep_t sl; core2_sleep_init(&sl, NULL);
    //   for (;;) {
    //       // 读传感器 → 更新 UI(包 bsp_display_lock/unlock)
    //       int delay_ms = core2_sleep_feed(&sl, NULL, true);
    //       vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms > 100 ? delay_ms : 100));
    //   }
}
EOF

echo "已生成 apps/$NAME/。下一步:"
echo "  1) 在 tools/flash_one.sh 的 SLOT 表 + tools/flash_map.md 登记一个空闲 ota 槽"
echo "  2) launcher 会自动发现并显示这个槽(数据驱动渲染工程名/版本),无需改 launcher 代码"
echo "  3) idf.py -C apps/$NAME build"
