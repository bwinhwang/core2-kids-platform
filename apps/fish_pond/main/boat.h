// boat —— 船/线/饵运动(SPEC.md §6.1;宽容物理:速度封顶+缓动+撞边缘滑停不粘)
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void boat_create(lv_obj_t *scr);

/**
 * @brief 每帧推进 + 渲染。
 * @param dt_ms      本帧时长(ms)。
 * @param joy_x_raw  摇杆 X 原始归一化值(-1..1,未做死区/重缩放,由本层自己处理)。
 * @param enc_delta  本帧曲柄增量(chain_link_enc_delta();正=放线,负=收线)。
 */
void boat_tick(int dt_ms, float joy_x_raw, int enc_delta);

int boat_bait_x(void);
int boat_bait_y(void);
int boat_line_len(void);          // 当前线长(px),[LINE_MIN_PX, LINE_MAX_PX]

/** @brief 收线比例(1.0 正常;鱼上钩后按鱼种设 1.0/REEL_LAZY_RATIO,SPEC §6.3)。 */
void boat_set_reel_ratio(float ratio);

#ifdef __cplusplus
}
#endif
