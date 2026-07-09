#include "spellbook.h"

#include "bsp/m5stack_core_2.h"

#define NCOUNT(a) (sizeof(a) / sizeof((a)[0]))

// ── §5 表音序(hz, ms, amp)── 逐条对应 apps/magic_wand/SPEC.md §5 音签列 ──────
static const audio_note_t NOTES_GROW[]       = { {392,60,50}, {523,60,55}, {659,90,60} };
static const audio_note_t NOTES_STARRAIN[]   = { {880,50,45}, {784,50,40}, {659,50,40}, {523,60,45} };
static const audio_note_t NOTES_SPIN_L[]     = { {659,45,50}, {523,45,45} };
static const audio_note_t NOTES_SPIN_R[]     = { {523,45,50}, {659,45,45} };
static const audio_note_t NOTES_ZOOM[]       = { {1046,50,60}, {1318,70,65} };
static const audio_note_t NOTES_SHRINK_POP[] = { {330,60,40}, {659,90,55} };
static const audio_note_t NOTES_WHIRL_CW[]   = { {392,35,40}, {440,35,40}, {523,35,45}, {587,35,45} };
static const audio_note_t NOTES_WHIRL_CCW[]  = { {587,35,45}, {523,35,45}, {440,35,40}, {392,35,40} };
static const audio_note_t NOTES_WAVE[]       = { {659,50,55}, {784,50,55}, {880,60,60}, {1047,80,65} };

const gesture_event_t SPELL_ORDER[SPELLBOOK_SIZE] = {
    GESTURE_UP, GESTURE_DOWN, GESTURE_LEFT, GESTURE_RIGHT,
    GESTURE_FORWARD, GESTURE_BACKWARD,
    GESTURE_CLOCKWISE, GESTURE_COUNTER_CLOCKWISE, GESTURE_WAVE,
};

// 索引 = gesture_event_t 枚举值(0=NONE 不用)。
static const spell_def_t s_defs[10] = {
    [GESTURE_UP]                = { NOTES_GROW,       NCOUNT(NOTES_GROW),       HAPTIC_COLLECT,     LED_FX_GATHER,    WAND_FX_GROW,           0x30E060 },
    [GESTURE_DOWN]              = { NOTES_STARRAIN,   NCOUNT(NOTES_STARRAIN),   HAPTIC_BUMP_LIGHT,  LED_FX_SPREAD,    WAND_FX_STARRAIN,       0xA0C8FF },
    [GESTURE_LEFT]              = { NOTES_SPIN_L,     NCOUNT(NOTES_SPIN_L),     HAPTIC_BUMP_LIGHT,  LED_FX_SWEEP_R2L, WAND_FX_SWEEP_TIP2ROOT, 0xB080E0 },
    [GESTURE_RIGHT]             = { NOTES_SPIN_R,     NCOUNT(NOTES_SPIN_R),     HAPTIC_BUMP_LIGHT,  LED_FX_SWEEP_L2R, WAND_FX_SWEEP_ROOT2TIP, 0xB080E0 },
    [GESTURE_FORWARD]           = { NOTES_ZOOM,       NCOUNT(NOTES_ZOOM),       HAPTIC_BUMP_MED,    LED_FX_FLASH,     WAND_FX_FLASH_WHITE,    0xFFE8B0 },
    [GESTURE_BACKWARD]          = { NOTES_SHRINK_POP, NCOUNT(NOTES_SHRINK_POP), HAPTIC_HELLO,       LED_FX_BUMP,      WAND_FX_DIM_POP,        0xFFB080 },
    [GESTURE_CLOCKWISE]         = { NOTES_WHIRL_CW,   NCOUNT(NOTES_WHIRL_CW),   HAPTIC_COLLECT,     LED_FX_GATHER,    WAND_FX_WHIRL_CW,       0xC060FF },
    [GESTURE_COUNTER_CLOCKWISE] = { NOTES_WHIRL_CCW,  NCOUNT(NOTES_WHIRL_CCW),  HAPTIC_COLLECT,     LED_FX_SPREAD,    WAND_FX_WHIRL_CCW,      0xC060FF },
    [GESTURE_WAVE]              = { NOTES_WAVE,       NCOUNT(NOTES_WAVE),       HAPTIC_WIN,         LED_FX_WIN,       WAND_FX_RAINBOW,        0xFFD030 },
};

#define ICON_SIZE   18
#define ICON_GAP     4
#define ICON_X0      8
#define ICON_Y0      6
#define COLOR_LOCKED 0x505060

static lv_obj_t *s_icons[SPELLBOOK_SIZE];
static bool      s_unlocked[SPELLBOOK_SIZE];

static int order_index(gesture_event_t g)
{
    for (int i = 0; i < SPELLBOOK_SIZE; i++) if (SPELL_ORDER[i] == g) return i;
    return -1;
}

const spell_def_t *spellbook_spell_def(gesture_event_t g)
{
    if (g <= GESTURE_NONE || g > GESTURE_WAVE) return NULL;
    return &s_defs[g];
}

static lv_obj_t *plain(lv_obj_t *parent, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void spellbook_create(lv_obj_t *parent)
{
    bsp_display_lock(0);
    for (int i = 0; i < SPELLBOOK_SIZE; i++) {
        lv_obj_t *icon = plain(parent, ICON_SIZE, ICON_SIZE, COLOR_LOCKED, 5);
        lv_obj_set_pos(icon, ICON_X0 + i * (ICON_SIZE + ICON_GAP), ICON_Y0);
        s_icons[i] = icon;
        s_unlocked[i] = false;
    }
    bsp_display_unlock();
}

bool spellbook_unlock(gesture_event_t g)
{
    int i = order_index(g);
    if (i < 0 || s_unlocked[i]) return false;
    s_unlocked[i] = true;
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_icons[i], lv_color_hex(s_defs[g].theme_color), 0);
    bsp_display_unlock();
    return true;
}

bool spellbook_is_complete(void)
{
    for (int i = 0; i < SPELLBOOK_SIZE; i++) if (!s_unlocked[i]) return false;
    return true;
}

void spellbook_reset(void)
{
    bsp_display_lock(0);
    for (int i = 0; i < SPELLBOOK_SIZE; i++) {
        s_unlocked[i] = false;
        lv_obj_set_style_bg_color(s_icons[i], lv_color_hex(COLOR_LOCKED), 0);
    }
    bsp_display_unlock();
}

// ── 连击彩蛋(P3):滑动窗口记最近几次手势 ─────────────────────────────────
#define COMBO_HISTORY  COMBO_NEEDED
static gesture_event_t s_combo_hist[COMBO_HISTORY];
static uint32_t        s_combo_time[COMBO_HISTORY];
static int              s_combo_head;
static int              s_combo_filled;

void spellbook_combo_feed(gesture_event_t g, uint32_t now_ms)
{
    if (g <= GESTURE_NONE) return;
    s_combo_hist[s_combo_head] = g;
    s_combo_time[s_combo_head] = now_ms;
    s_combo_head = (s_combo_head + 1) % COMBO_HISTORY;
    if (s_combo_filled < COMBO_HISTORY) s_combo_filled++;
}

bool spellbook_combo_check(uint32_t now_ms)
{
    if (s_combo_filled < COMBO_NEEDED) return false;
    // 窗口内(COMBO_WINDOW_MS)+ COMBO_NEEDED 种互不相同的手势
    for (int i = 0; i < COMBO_HISTORY; i++) {
        if (now_ms - s_combo_time[i] > COMBO_WINDOW_MS) return false;
    }
    for (int a = 0; a < COMBO_HISTORY; a++) {
        for (int b = a + 1; b < COMBO_HISTORY; b++) {
            if (s_combo_hist[a] == s_combo_hist[b]) return false;
        }
    }
    s_combo_filled = 0;   // 边沿触发:判定一次即清空,避免同一组合重复触发
    return true;
}
