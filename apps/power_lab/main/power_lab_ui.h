// power_lab_ui —— view 层:page1(遥测 + 负载矩阵) / page2(休眠演练 + 续航 + 录制)
//
// 与 apps/unit_bench 的 unit_bench_ui 同一职责边界:本文件只碰 LVGL,读
// power_lab_ctl 暴露的只读状态渲染,点击回调再反过来调 pl_ctl_* 动作函数——不自己
// 维护"挂没挂/开没开"这类状态(那是 power_lab_ctl 的事)。
#pragma once

#include <stdint.h>

#include "power_lab_ctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建两页 LVGL 画面(静态层各画一次)+ 首次刷新。须在 pl_ctl_init 之后调用。 */
void power_lab_ui_start(pl_ctl_t *ctl);

/** @brief 主循环每帧调用一次:1Hz 状态栏/遥测卡刷新 + 休眠演练结果回放。
 *  演练进行中(ctl->drill_stage != PL_DRILL_IDLE)时本函数直接返回,不碰任何 LVGL
 *  对象——DEEP 演练期间背光/5V 已断,没必要也不应该刷屏(见 CLAUDE.md 任务说明)。 */
void power_lab_ui_tick(pl_ctl_t *ctl, int64_t now_ms);

#ifdef __cplusplus
}
#endif
