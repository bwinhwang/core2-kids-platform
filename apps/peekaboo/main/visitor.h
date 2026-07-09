// 躲猫猫昼夜屋 v2 —— 访客层:访客池数据表、精灵构建、抽签、悬念节拍、出场/游行动画。
//
// 只管"访客"本身(造型/音签/悬念/出场),不管场景背景(见 scene.h)、不管相册/NVS(见 album.h)、
// 不管反馈总线(haptics/ledstrip 由 peekaboo_game.c 按事件统一分派)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VISITOR_POOL_N   6              // 常驻访客 id: 0..5(见 SPEC §5.1 表)
#define VISITOR_RAINBOW  VISITOR_POOL_N // id=6:稀客彩虹鸟(不入册/不占轮次)

typedef enum {
    DAWN_NORMAL = 0,   // 普通松开
    DAWN_FAST,         // 猛松(§4.2 ≤DAWN_FAST_READS)
    DAWN_SLOW,         // 慢掀(§4.2 ≥DAWN_SLOW_READS)
} dawn_speed_t;

/** @brief 建 7 位访客的精灵(身体+夜眼,全 HIDDEN)+ 站位/藏点几何。scene_create 之后调。 */
void visitor_create(lv_obj_t *scr);

/** @brief 访客主色(album 画头像用)。id 越界返回 0。 */
uint32_t visitor_color(int id);

/**
 * @brief 入夜瞬间抽签:从本轮未收集访客中均匀随机选一位,首抽不与上轮末位重复;
 *        再掷稀客骰(RARE_PCT),命中则本夜改彩虹鸟(原抽签作废退回池)。
 * @param collected_mask 本轮已收集位图(bit i = 已收集 id i,album 提供)。
 * @param last_id        上一轮/上次末位访客 id(-1 = 无,如开机首次)。
 * @return 本夜访客 id(0~VISITOR_POOL_N-1 或 VISITOR_RAINBOW)。
 */
int visitor_draw_for_night(uint8_t collected_mask, int last_id);

/** @brief 入夜瞬间调:该访客悬念节拍归零(动静/眼睛/闷叫的内部计时器复位)。 */
void visitor_tease_start(int visitor_id);

/** @brief 每 AWAKE 帧调一次(游戏层按 §15 冻结规则决定是否调);elapsed_ms = 本夜已过毫秒。 */
void visitor_tease_tick(int visitor_id, int elapsed_ms);

/** @brief 打盹/深度省电唤醒回 AWAKE 时调:节拍重置,但眼睛若已亮起则保持亮着。
 *  @return 恢复后的 elapsed_ms 基准(眼睛已亮起则从 TEASE_EYES_MS 起、否则从 0 起),
 *          调用方据此重设自己的夜时长计数,使之与本组件内部节拍时间线对齐。 */
int visitor_tease_wake_resume(int visitor_id);

/**
 * @brief 天亮揭晓:按速度档编排出场(视觉+音签),自持锁。
 * @param first_time 本轮是否首次收集到这位(决定出场收尾是否有"入册"动作,由调用方在
 *                    entrance 总时长后自行触发 album_mark_collected + COLLECT 反馈)。
 * @return 出场编排总时长 ms(含慢掀探头停顿),调用方据此安排"首遇入册"延时反馈。
 */
int visitor_reveal(int visitor_id, dawn_speed_t speed, bool first_time);

/** @brief 强制隐藏当前在场/出场中的访客(打断出场动画→瞬移站位→隐藏,§15.2)。入夜/游行开场时调。 */
void visitor_hide_all_instant(void);

/** @brief 游行开场:6 位常驻依次错峰横穿全屏,返回总时长 ms(自持锁)。 */
int visitor_parade_start(void);

/** @brief 游行收场:访客池状态复位待下一轮(不改 album,由调用方另行 album_reset)。 */
void visitor_parade_reset(void);

#ifdef __cplusplus
}
#endif
