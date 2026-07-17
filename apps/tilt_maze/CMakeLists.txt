# 倾斜迷宫(Tilt Maze for Toddlers)—— 游戏槽 ota_0
# 组件层与分区表共享仓库根的 components/ 与 partitions.csv(唯一真源)。
# ⚠️ 烧录:严禁 idf.py flash(会覆盖 factory 的 launcher)!
#         用 esptool.py write_flash 0x190000 build/tilt_maze.bin(见 tools/flash_map.md)
cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.platform;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tilt_maze)
