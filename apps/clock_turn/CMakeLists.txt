# clock_turn —— 转转钟(教育卡带,占 ota_3,见 tools/flash_map.md)
# ⚠️ 烧录:严禁 idf.py flash(会覆盖 factory 的 launcher)!单刷见 tools/flash_map.md
cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.platform;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/units")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(clock_turn)
