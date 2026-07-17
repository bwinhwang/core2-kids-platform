// 动物精灵(SPEC §1/§8)—— 烘焙身体 + 眼/喙装扮件,§6.4 纪律:烘好再贴、零运行时 alpha。
// 只管"画",不管物理;读 flock.c 的 animal_t 数组画出来,不反过来改它。
#pragma once

#include "flock.h"

/** @brief 建 10 只动物的 LVGL 对象(容器+烘焙身体图+眼+喙)并按 animals[] 首次摆位。
 *         须在 scene_init() 之后调(动物要叠在场景上面)、n 与 game_state 的数组一致。 */
void critters_init(const animal_t animals[], int n);

/** @brief 每帧:active 的位置跟 animals[] 走(脏矩形)+ 挤压/摇头脉冲推进;
 *         !active 的跳过(位置归捕获动画管,不抢)。game_task 每帧调一次。 */
void critters_update(const animal_t animals[], int n);

/** @brief 第 idx 只触发一次"挤压"视觉脉冲(反馈层调,下一帧 critters_update 起效并衰减)。 */
void critters_squash(int idx);

/** @brief 第 idx 只进错家:摇头动画(左右各摆一下,~400ms;SPEC §5.2)。
 *         与挤压同款影子变量纪律:只置计数,critters_update 逐帧推进。 */
void critters_shake_head(int idx);

/** @brief 第 idx 只归家(SPEC §5.3):从当前位置"蹦进门"(~CAPTURE_ANIM_MS,弧线位移+
 *         缩小,lv_anim),完成后自隐藏。调用方须已把 animals[idx].active 置 false
 *         (flock_step 捕获时已做),进度计数在调用方即时累加、不等动画完成
 *         (影子变量纪律:不用 anim 回调跨任务碰游戏状态)。 */
void critters_capture(int idx, const animal_t *a);

/** @brief 重散一批后复位全部精灵:停掉残余动画、恢复缩放/位移/显示,按 animals[] 摆位。 */
void critters_respawn(const animal_t animals[], int n);

/** @brief ATTRACT 睡/醒切换(SPEC §4):睡 = 全体闭眼(眼睛压扁)+ 显示 Zzz;
 *         醒 = 睁眼 + 藏 Zzz + 复位呼吸缩放。呼吸推进见 critters_idle_tick()。 */
void critters_set_asleep(bool asleep);

/** @brief ATTRACT 每帧:慢呼吸缩放(~2s 周期,每只相位错开,有机感)。
 *         只在睡着时由 game_task 调;醒来后不再调,缩放由 set_asleep(false) 复位。 */
void critters_idle_tick(void);

/** @brief 在场(active)动物全体原地小跳,按下标错拍开跳(波浪感)。
 *         醒来(§4)与摇一摇彩蛋(§3)共用;translate_y 脉冲,critters_update 逐帧推进。 */
void critters_hop_all(void);
