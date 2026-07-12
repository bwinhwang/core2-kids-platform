// magic_wand v2.1「魔法萤火虫」—— 萤火虫精灵(SPEC.md §8/§11)
//
// 只管萤火虫自己的显示位置与外观:贴玻璃盘旋(8 方位查表,离散步进)、九手势方向
// 翻滚(位移/缩放叠加层)、尾迹、回家动画、SEEK 慢眨眼,都在这里。不碰音效/震动/
// 灯带(那些是跨通道编排,留给 magic_wand.c),不碰在场/手势读取本身(那是
// unit_gesture)、也不碰在场信号的 EMA/迟滞/档位判定(那是 magic_wand.c 的状态机)。
//
// 帧模型:`firefly_dance_advance()` 只更新内部"权威位置"(浮点,像素,不触屏);
// `firefly_tick()` 每个清醒帧调用一次,把权威位置(+ 翻滚叠加的位移/缩放)应用到
// 实际 LVGL 对象(halo/core/尾迹),推进尾迹历史环形缓冲——与当前是否在跳舞无关,
// 任何模式下都调用它。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建萤火虫精灵(halo+core+3 尾迹点),初始停在家位置、慢眨眼(SEEK 态外观)。 */
void firefly_create(lv_obj_t *parent, int home_x, int home_y);

/** @brief 每个清醒帧调用一次:推进尾迹历史 + 把权威位置(含翻滚叠加)应用到 LVGL 对象
 *         (自持锁)。 */
void firefly_tick(void);

/**
 * @brief SEEK→DANCE 的入场:停止 SEEK 慢眨眼,从当前位置起飞进入盘旋轨道(§3/§5.1)。
 *        调用一次即可,之后每帧配合 firefly_dance_advance() 推进。
 */
void firefly_enter_dance(void);

/**
 * @brief DANCE 态每个清醒帧调用一次:按强度档位推进盘旋的 8 方位离散步进(SPEC §5.1),
 *        或(CW/CCW 触发时)推进筋斗云快速整圈。不触屏,只更新权威位置。
 * @param dt_ms 本帧时长(ms,即 core2_sleep_feed 返回的 delay_ms)。
 * @param level 强度档 1(远)/2(中)/3(近),决定步速与盘旋半径(SPEC §10)。
 */
void firefly_dance_advance(uint32_t dt_ms, int level);

// ── 九手势方向翻滚(SPEC §4.2,DANCE 内的瞬时叠加动画,~TUMBLE_MS)───────────
// 每个函数内部先掐掉正在播的翻滚(不论哪种)、瞬移叠加量归零,再开始新的——
// 因此"不同手势随时打断"不需要调用方额外处理(SPEC §4.2/§6)。CW/CCW 会顺带
// 打断/接管盘旋步进本身(筋斗云是加速整圈,不是叠加位移),完成后自然回到常速盘旋。
void firefly_tumble_left(void);
void firefly_tumble_right(void);
void firefly_tumble_up(void);
void firefly_tumble_down(void);
void firefly_tumble_forward(void);
void firefly_tumble_backward(void);
void firefly_tumble_cw(void);
void firefly_tumble_ccw(void);
void firefly_tumble_wave(void);

/** @brief 宽容期耗尽:从当前权威位置起飞,HOME_FLY_MS 内飞回家(自持锁,自带完成回调)。 */
void firefly_go_home_start(void);

/** @brief 回家动画是否已自然播完(供 magic_wand 轮询以切回 SEEK 态)。 */
bool firefly_go_home_is_done(void);

/** @brief 回家途中在场信号重新 ON:掐动画,权威位置保留在中断处,立即可续跳舞。 */
void firefly_go_home_interrupt(void);

/** @brief 强制回到"停在家、慢眨眼"外观(初始化 / DEEP 唤醒后重置用,幂等)。 */
void firefly_enter_seek(void);

/**
 * @brief 冻结外观动画(慢眨眼 + 盘旋/翻滚 + 回家飞行,SPEC §3:非 AWAKE 冻结一切
 *        动画):AWAKE→NAP/DEEP 前调一次。位置保留在冻结瞬间,不续播;唤醒后调用方
 *        应接着调 `firefly_enter_seek()` 复位(本文件按"每次唤醒回 SEEK"设计)。
 */
void firefly_freeze(void);

#ifdef __cplusplus
}
#endif
