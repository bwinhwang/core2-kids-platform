// bucket —— 木桶计数 + 罐头入桶动画 + 放生派对(SPEC.md §6.4;影子变量纪律见 §10)
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "sprites.h"

#ifdef __cplusplus
extern "C" {
#endif

void bucket_create(lv_obj_t *scr);

/** @brief 出水瞬间调(fish.c 已先发 feedback_surface()):开始水花停留→抛物线入桶。 */
void bucket_catch_start(fish_species_t species, int from_x, int from_y);

/** @brief 每帧推进入桶动画 / 放生派对;桶满自动进派对,派对结束回调 fish_round_setup()。 */
void bucket_tick(int dt_ms);

/** @brief 入桶飞行动画或派对进行中 = true(此时 fish.c 不应再放行新的咬钩)。 */
bool bucket_is_busy(void);

#ifdef __cplusplus
}
#endif
