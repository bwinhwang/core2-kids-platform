// fish —— 鱼 AI 状态机(SPEC.md §6.2)+ 收线拉扯(§6.3)
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void fish_create(lv_obj_t *scr);

/** @brief 每帧推进两条鱼的 AI / 收线 / 渲染(读 boat_bait_x/y()/boat_line_len())。 */
void fish_tick(int dt_ms);

/** @brief (重新)摆放两条鱼:随机起点+方向,PATROL 起步。冷启动 + 放生派对结束后调。 */
void fish_round_setup(void);

#ifdef __cplusplus
}
#endif
