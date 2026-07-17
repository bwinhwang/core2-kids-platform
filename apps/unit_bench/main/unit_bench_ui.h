// unit_bench_ui —— LVGL 视图层(列表页 + 6 个详情页)+ 主循环轮询编排
//
// 只暴露两个入口给 app_main.c:建 UI 一次,之后每帧 tick 一次。内部视图切换/热插拔重试/
// CSV 导出/超声波标定全部自成一体,细节见 unit_bench_ui.c 顶部注释与 apps/unit_bench/
// README.md「代码结构」。
#pragma once

#include <stdint.h>
#include "core2_sleep.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建 UI(状态栏 + 列表页容器 + 详情页容器)并做一次首扫。
 *  须在 core2_board_init / kv_store_init / chain_bus_init_port_c 全部完成之后调用一次。 */
void unit_bench_ui_start(void);

/**
 * @brief 主循环每帧调:后台周期扫描(2s)+ 状态栏 1Hz 刷新 + 当前详情页的轮询/渲染 +
 *        桌面评估防误打盹(单元被操作但机身不动时调 core2_sleep_kick,CLAUDE.md §10)。
 * @param now_ms 单调时间戳(ms,任意起点,只用于内部计算间隔)。
 * @param sleep  core2_sleep 句柄——本函数只在检测到"评估对象被操作"时调用
 *               core2_sleep_kick,真正的每帧 core2_sleep_feed 仍由 app_main 主循环负责
 *               (喂机身加速度、推进省电状态机),两者职责分开。
 */
void unit_bench_ui_tick(int64_t now_ms, core2_sleep_t *sleep);

#ifdef __cplusplus
}
#endif
