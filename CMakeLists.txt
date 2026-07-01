# 顶层工程文件 —— Core2 + Bottom2 幼儿应用平台模板
# 工程名由 tools/new_app.sh 替换;直接 build 也可跑默认 demo。
cmake_minimum_required(VERSION 3.16)
# 让 components/units/ 下的每个 UNIT 子目录都被当作组件发现
set(EXTRA_COMPONENT_DIRS "components/units")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kids_app)
