// pond —— 静态层(天空/水线/双层水带/水草石头),进关画一次,之后不重画(SPEC §6/§10)
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 在活动屏上画池塘静态背景(先于船/鱼/桶等动态层调用,保证在最底层)。 */
void pond_create(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif
