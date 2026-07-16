// slingshot_feed —— 静态层:草地全景(天空/草坡/云/树/篱笆 + 弹弓 Y 叉)+ 动物位置表 +
// 加载校验(SPEC.md §5.5/§9:满拉状态下仅靠角度可命中)。进场画一次,之后草地本身不
// 重画;miss 小花落定后一次性并入静态层(§7,零每帧成本)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float x, y; } vec2_t;

/** @brief 建静态层(挂在 parent,通常是 lv_screen_active())。只调一次。
 *  内部会校验动物位置表(SPEC §5.5 真实可达包络 safety parabola:以锚点为原点、y 向上,
 *  y ≤ v²/(2·GRAVITY) − GRAVITY·x²/(2·v²),v=LAUNCH_POWER,留 ~10% 距离余量),
 *  不满足的位置会被剔除并 ESP_LOGW,不静默。 */
void meadow_create(lv_obj_t *parent);

/** @brief 弹弓兜(pouch)/果子装填点 = 发射原点,固定坐标。 */
vec2_t meadow_sling_anchor(void);

/** @brief 校验通过后的动物位置槽位数(≥1,已在 meadow_create 校验)。 */
int meadow_spot_count(void);

/** @brief 第 idx 个动物位置槽位坐标(= 动物嘴心)。 */
vec2_t meadow_spot_pos(int idx);

/** @brief 随机取一个位置槽位,exclude_idx 传上一次的槽位(不重复,SPEC §4);槽位数为 1 时忽略排除。 */
int meadow_pick_spot(int exclude_idx);

/** @brief miss 小花落定,一次性烘进静态层(单次创建,之后零成本);达 MISS_FLOWER_MAX 时静默忽略。 */
void meadow_flower_add(float x, float y);

/** @brief 清场(全部小花隐藏 + 计数清零),批次切换时调。 */
void meadow_flower_clear(void);

// ── "好朋友"聚集排(改进 B:喂饱的动物攒到草地边,凑够 ANIMAL_QUOTA 只开派对群跳)──────
/** @brief 已聚集的好朋友数(0..ANIMAL_QUOTA)。 */
int  meadow_friend_count(void);

/** @brief 新增一个好朋友(身体色=所喂物种),蹦到草地边下一个空位;满 ANIMAL_QUOTA 静默忽略。自带锁。 */
void meadow_friend_add(uint32_t body_col);

/** @brief 整排好朋友一起上下偏移 dy(派对群跳,仅 PARTY 态低频调用)。自带锁。 */
void meadow_friends_bob(int dy);

/** @brief 清空好朋友排(全部隐藏 + 计数清零),派对结束换新批时调。自带锁。 */
void meadow_friends_clear(void);

#ifdef __cplusplus
}
#endif
