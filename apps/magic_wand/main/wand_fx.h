// wand_fx —— 法术事件 → unit_rgb(魔法棒 3px)特效调用的薄封装
//
// 只做一件事:把 unit_rgb 是否就位这件事从 magic_wand.c 的主派发逻辑里拿掉——
// P4(魔法棒 RGB)是加分项,RGB 单元缺席不阻塞整卡启动(SPEC.md §13)。
#pragma once

#include "unit_rgb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 起 unit_rgb(容错:失败只记日志,后续 trigger 静默忽略)。 */
void wand_fx_start(void);

/** @brief 非阻塞触发一次魔法棒特效;unit_rgb 未就位时静默忽略。 */
void wand_fx_trigger(wand_fx_t fx);

#ifdef __cplusplus
}
#endif
