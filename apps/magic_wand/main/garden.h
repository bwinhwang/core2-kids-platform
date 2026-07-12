// magic_wand v2「魔法萤火虫」—— 夜花园静态层(SPEC.md §8/§11)
//
// P1 范围:只画一次的静态层——纯色深蓝夜空 + 深色地面 + 月亮 + 星星 + 家花(萤火虫
// 停靠/入睡的位置)。P2 才加 5 个沉睡目标(dwell/预亮/BLOOM),本阶段不实现,但共用
// 几何(SCREEN_W/H 等)先放这里给后续复用,不堵死加入点。
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── 共用几何 ─────────────────────────────────────────────────────────────
#define SCREEN_W    320
#define SCREEN_H    240
#define GROUND_H     40
#define GROUND_Y    (SCREEN_H - GROUND_H)   // 200

// 家花位置(萤火虫 SEEK 态停靠点;P1 唯一的花园"目标")
#define GARDEN_HOME_X   160
#define GARDEN_HOME_Y   182

/** @brief 建静态层(夜空/地面/月亮/星星/家花),进场画一次,之后不重画。 */
void garden_create(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif
