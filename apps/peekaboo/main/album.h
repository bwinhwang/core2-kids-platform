// 躲猫猫昼夜屋 v2 —— 相册层:顶部 6 头像位 + (P4)NVS 持久化 + 游行小星。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建相册 UI(顶部一排 6 枚头像位 +(P4)游行小星),并从 NVS 恢复上次进度。 */
void album_create(lv_obj_t *scr);

bool album_is_collected(int id);

/** @brief 标记 id 已收集(点亮头像 + 写 NVS)。@return true = 本次是"新收集"(之前未集)。 */
bool album_mark_collected(int id);

int album_count(void);   // 本轮已收集几位(0~VISITOR_POOL_N)

/** @brief 上一轮(游行前)最后收集到的访客 id,供抽签"首抽不与上轮末位重复";-1=无。 */
int album_last_full_round_id(void);

/** @brief 游行收场:清灰头像 + 复位本轮计数 +(P4)游行数+1 写 NVS。 */
void album_reset(void);

int album_parade_count(void);   // (P4)累计游行次数,开机从 NVS 恢复

uint8_t album_mask(void);       // 本轮已收集位图(bit i = 已收集 id i),供 visitor 抽签用

#ifdef __cplusplus
}
#endif
