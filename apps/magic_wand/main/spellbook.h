// spellbook —— 9(+1 隐藏)法术数据表(SPEC.md §5)+ 书页解锁状态 + 集齐判定 + 连击彩蛋
//
// 本组件是 §5 表的"数据"化身:每个手势 → 一整套跨通道反馈(音序/触觉/底座灯/魔法棒灯)。
// 屏幕视觉(wizard_cast)由 magic_wand.c 与本表数据平行调用,不经本文件转发
// (职责边界见 SPEC.md §10:wizard.c 管屏幕,本文件管数据+进度)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "audio_fx.h"
#include "haptics.h"
#include "ledstrip_fx.h"
#include "unit_gesture.h"
#include "unit_rgb.h"

#include "tuning.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const audio_note_t *notes;
    uint8_t              note_count;
    haptic_pattern_t      haptic;
    led_fx_t              led_fx;
    wand_fx_t             wand_fx;
    uint32_t              theme_color;   // 法术书页解锁色 / 图标主题色
} spell_def_t;

// §5 表固定顺序(法术书页序 = 派对回放序),9 个真实手势,不含 WAVE 以外的隐藏项。
extern const gesture_event_t SPELL_ORDER[SPELLBOOK_SIZE];

/** @brief 查一个手势对应的整套法术数据(音/震/底座灯/魔法棒灯 + 主题色)。
 *  @return NULL = g 不是 9 种正式手势之一(如 GESTURE_NONE)。 */
const spell_def_t *spellbook_spell_def(gesture_event_t g);

/** @brief 在屏幕左上角画 9 枚法术书页图标槽(灰底剪影,未解锁)。 */
void spellbook_create(lv_obj_t *parent);

/** @brief 标记一个手势对应的法术书页为"已解锁"(切换图标为主题色)。
 *  @return true = 本轮首次解锁这一页(调用方据此追加"首遇"音效/震动);
 *          false = 已解锁过,重复施放。 */
bool spellbook_unlock(gesture_event_t g);

/** @brief 本轮 9 页是否已全部解锁。 */
bool spellbook_is_complete(void);

/** @brief 清空法术书(派对结束后调用,开新一轮)。 */
void spellbook_reset(void);

/** @brief 连击彩蛋(P3):喂一次手势分类事件。 */
void spellbook_combo_feed(gesture_event_t g, uint32_t now_ms);

/** @brief 连击彩蛋是否刚刚达成(边沿触发,读一次即清)。
 *  判据:COMBO_WINDOW_MS 窗口内出现 COMBO_NEEDED 种不同手势。 */
bool spellbook_combo_check(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
