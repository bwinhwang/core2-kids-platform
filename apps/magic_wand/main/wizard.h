// wizard —— 屏幕视觉:静态场景层 + 魔法师精灵 + 各法术专属特效精灵
//
// 只管屏幕(渲染红线见根 CLAUDE.md §6:静态层进场景画一次,施法只刷小容器);
// 音/震/底座灯/魔法棒灯由 magic_wand.c 与本文件平行调用(spellbook.c 提供数据),
// 不经本文件转发。
#pragma once

#include "lvgl.h"
#include "unit_gesture.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_W  320
#define SCREEN_H  240

/** @brief 画静态层(背景/地毯/星星)+ 创建魔法师精灵(整局一次,之后只改精灵状态)。 */
void wizard_scene_create(lv_obj_t *parent);

/** @brief 完整法术动画(可被新手势打断:内部先掐掉上一个法术的残留动画)。 */
void wizard_cast(gesture_event_t g);

/** @brief 冷却窗口内的同一手势重复触发:轻量重复(不重放整套大动画)。 */
void wizard_cast_light(gesture_event_t g);

/** @brief 微光回应:魔法师手边一粒微光单闪一下(感应到动静但未分类 / 退化 ping)。 */
void wizard_shimmer(void);

/** @brief 隐藏法术(P3 连击彩蛋):头顶双层烟花,不影响法术书状态。 */
void wizard_hidden_spell(void);

/** @brief 法术书集满大派对:接力回放一个法术(压缩版,magic_wand.c 按 SPELL_ORDER 依次调)。 */
void wizard_party_step(gesture_event_t g);

/** @brief 派对开场(魔法师原地欢跳提示)。 */
void wizard_party_begin(void);

/** @brief 派对收场(回归待机姿态)。 */
void wizard_party_end(void);

#ifdef __cplusplus
}
#endif
