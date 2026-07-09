// 躲猫猫昼夜屋 v2 —— 场景层:昼/夜/黄昏静态背景、圆圆(吉祥物)、夜氛围环境动效。
//
// 只管"环境"(天空/地面/草丛/圆圆/星/萤火虫/梦泡泡/流星),不管访客(见 visitor.h)。
// 三态背景整屏级切换(scene_apply),日常帧只有限量小精灵脏矩形(§12 预算)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── 共用几何(视觉层/访客层都按此对齐)────────────────────────────────────
#define SCREEN_W    320
#define SCREEN_H    240
#define GROUND_H    40
#define GROUND_Y    (SCREEN_H - GROUND_H)   // 200

#define BODY_D      104
#define BODY_X      ((SCREEN_W - BODY_D) / 2)   // 108
#define BODY_Y      92
#define BODY_CX     (BODY_X + BODY_D / 2)       // 160
#define BODY_CY     (BODY_Y + BODY_D / 2)       // 144

// 藏点草丛(左/右,昼夜场景都有;§5.1)
#define GRASS_L_X   16
#define GRASS_R_X   240
#define GRASS_Y     176
#define GRASS_W     64
#define GRASS_H     30

// 夜眼位置(藏点草丛暗处;§6)
#define EYE_L_X     36
#define EYE_L_Y     182
#define EYE_R_X     260
#define EYE_R_Y     182

typedef enum {
    SCENE_DAY = 0,
    SCENE_NIGHT,
    SCENE_DUSK,
} scene_kind_t;

/** @brief 建全部静态层(昼/夜/黄昏背景 + 圆圆 + 夜氛围精灵),初始 SCENE_DAY。调用方持锁前先建好。 */
void scene_create(lv_obj_t *scr);

/** @brief 整屏级场景切换(自持锁)。相同 kind 重复调用不做事。 */
void scene_apply(scene_kind_t kind);

/** @brief 当前场景。 */
scene_kind_t scene_current(void);

// ── 圆圆 ─────────────────────────────────────────────────────────────
/** @brief 圆圆整体弹跳(自持锁)。 */
void scene_char_bounce(int lift, int up_ms, int down_ms);

/** @brief 瞳孔看向一侧(dx 像素偏移,0=回中;自持锁)。只在白天(睁眼)可见有意义。 */
void scene_char_gaze(int dx);

// ── 夜氛围(仅 AWAKE+夜逻辑态时驱动;调用方按 §15 冻结规则决定是否调)────────────
/** @brief 入夜瞬间重置氛围(梦泡泡回 0 档、萤火虫回起始只数)。 */
void scene_night_ambience_reset(void);

/** @brief 按夜内经过的毫秒数推进氛围(梦泡泡分档长大 / 萤火虫累积;自持锁,内部去重)。 */
void scene_night_ambience_tick(int night_elapsed_ms);

/** @brief 草丛晃动(入夜后~0.8s 的"动静";side: 0=左 1=右;自持锁)。 */
void scene_grass_rustle(int side);

/** @brief 流星划过(彩虹鸟之夜悬念,~1.2s 一次;自持锁)。 */
void scene_shooting_star_trigger(void);

/** @brief 长夜有梦:梦泡泡"啵"消失 + 2~3 只蝴蝶飘出屏外(一次性;自持锁)。 */
void scene_dream_butterflies_trigger(void);

// ── 迸发小星(celebrate / 游行彩纸 / 彩虹鸟迸发共用;自持锁,内部限量单缓冲)──────
void scene_burst(uint32_t color, int count);

#ifdef __cplusplus
}
#endif
