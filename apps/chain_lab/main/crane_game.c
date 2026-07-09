// 抓娃娃机 —— 游戏层实现(SPEC.md §3~§10)
//
// 状态机:PLAY_IDLE ⇄ DESCENDING → GRABBING → ASCENDING → DEPOSIT → PLAY_IDLE
//        (展示架集满 → PARTY → 重置 → PLAY_IDLE)。
// 输入:摇杆 X 直接映射吊臂横坐标(始终可控,不受状态门控);编码器帧间 delta 只在
//      PLAY_IDLE/DESCENDING 时驱动深度,GRABBING/ASCENDING/DEPOSIT/PARTY 期间吃掉输入。
// 战利品造型本轮用色块占位(SPEC §11④ 留给下一步创作),身份靠颜色区分。
#include "crane_game.h"

#include <math.h>
#include <stdlib.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "chain_lab.h"
#include "haptics.h"
#include "ledstrip_fx.h"

#include "tuning.h"

#define SCREEN_W 320
#define SCREEN_H 240

// ── 配色(暖色家族,同诊断台延续)────────────────────────────────────────
#define BG_COL           0xEAE6DA
#define PANEL_COL        0xF3EEE0
#define RACK_BG_COL      0xFBF7EE
#define RAIL_COL         0xB8AF9C
#define ARM_COL          0x8A5A32
#define CABLE_COL        0x6B6357
#define CLAW_COL         0xFB8B24
#define UNCOLLECTED_BASE   0xCFC6B8
#define UNCOLLECTED_ACCENT 0xB8B0A2

// ── 布局(px,§9 机制常量以外的纯视觉几何;精确值待实机标定)───────────────
#define CAB_Y0        36
#define CAB_Y1        236
#define RAIL_Y        (CAB_Y0 + 10)
#define ARM_X_MIN     (SCREEN_W / 2 - CRANE_X_RANGE_PX / 2)
#define ARM_X_MAX     (SCREEN_W / 2 + CRANE_X_RANGE_PX / 2)
#define ARM_W         30
#define ARM_H         18
#define CABLE_W       4
#define CLAW_OPEN_W   26
#define CLAW_CLOSED_W 12
#define CLAW_H        14
#define PIT_Y         (CAB_Y1 - 30)
#define PRIZE_SZ      30
#define PRIZE_HELD_SZ 26

#define RACK_Y        4
#define RACK_SLOT     28
#define RACK_GAP      6
#define RACK_X0       ((SCREEN_W - (PRIZE_TYPES * RACK_SLOT + (PRIZE_TYPES - 1) * RACK_GAP)) / 2)

#define DEPTH_BANDS   5
#define CONFETTI_N    8
#define WIGGLE_FRAMES 4

// ── 战利品配色表(身份靠颜色区分,造型留待下一步创作,SPEC §11④)───────────────
typedef struct { uint32_t base, accent; } prize_style_t;
static const prize_style_t PRIZE_STYLE[PRIZE_TYPES] = {
    { 0xE6533C, 0xFFD9CE },   // 珊瑚红
    { 0x4FB0D8, 0xDFF3FA },   // 天蓝
    { 0xF5C242, 0xFFF3B0 },   // 暖黄
    { 0x6FBF73, 0xE1F5DE },   // 草绿
    { 0xC77DD1, 0xF3E1F7 },   // 紫粉
};

// 下降深度 → 编码器节点 RGB(浅→深,离散几档,不逐帧渐变,SPEC §4)
typedef struct { uint8_t r, g, b; } rgb8_t;
static const rgb8_t DEPTH_COLOR[DEPTH_BANDS] = {
    { 0x9F, 0xE6, 0xD2 },
    { 0x6F, 0xCE, 0xB2 },
    { 0x3D, 0xAE, 0x93 },
    { 0x22, 0x86, 0x71 },
    { 0x11, 0x5F, 0x50 },
};

typedef enum {
    CRANE_PLAY_IDLE = 0,
    CRANE_DESCENDING,
    CRANE_GRABBING,
    CRANE_ASCENDING,
    CRANE_DEPOSIT,
    CRANE_PARTY,
} crane_state_t;

// ── 状态 ─────────────────────────────────────────────────────────────
static crane_state_t s_state = CRANE_PLAY_IDLE;
static int  s_state_frames;

static int  s_arm_x = SCREEN_W / 2;
static int  s_depth;
static int  s_claw_w = CLAW_OPEN_W;
static int  s_bounce_dy;
static int  s_wiggle_dx;
static int  s_wiggle_frames;

static int  s_grabbed_slot = -1;
static int  s_ascend_from_depth;
static int  s_deposit_from_x, s_deposit_from_y, s_deposit_to_x, s_deposit_to_y;

static bool s_pit_present[PRIZE_TYPES];
static int  s_pit_kind[PRIZE_TYPES];
static int  s_pit_slot_x[PRIZE_TYPES];

static bool s_rack_got[PRIZE_TYPES];
static int  s_rack_slot_x[PRIZE_TYPES];
static int  s_rack_count;

static int16_t s_prev_enc_value;
static bool    s_prev_enc_btn;
static bool    s_prev_joy_btn;
static bool    s_enc_was_attached;
static bool    s_hint_shown;

static int  s_led_base = -1;
static int  s_confetti_vx[CONFETTI_N];

// ── LVGL 对象 ────────────────────────────────────────────────────────
static lv_obj_t *s_arm, *s_cable, *s_claw;
static lv_obj_t *s_pit_body[PRIZE_TYPES], *s_pit_accent[PRIZE_TYPES];
static lv_obj_t *s_rack_body[PRIZE_TYPES], *s_rack_accent[PRIZE_TYPES];
static lv_obj_t *s_held_body, *s_held_accent;
static lv_obj_t *s_confetti[CONFETTI_N];
static lv_obj_t *s_hint_card;

// ── 小工具 ───────────────────────────────────────────────────────────
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

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void led_base_set(led_base_t b)
{
    if ((int)b == s_led_base) return;
    s_led_base = (int)b;
    ledstrip_fx_set_base(b);
}

static void set_depth_led(int depth)
{
    int band = (depth * DEPTH_BANDS) / (DESCEND_MAX_PX + 1);
    if (band < 0) band = 0;
    if (band >= DEPTH_BANDS) band = DEPTH_BANDS - 1;
    chain_lab_enc_rgb(DEPTH_COLOR[band].r, DEPTH_COLOR[band].g, DEPTH_COLOR[band].b);
}

// ── 战利品坑 / 展示架 / 跟随精灵 ─────────────────────────────────────────
static void pit_slot_paint(int i)
{
    int kind = s_pit_kind[i];
    lv_obj_set_style_bg_color(s_pit_body[i], lv_color_hex(PRIZE_STYLE[kind].base), 0);
    lv_obj_set_style_bg_color(s_pit_accent[i], lv_color_hex(PRIZE_STYLE[kind].accent), 0);
    lv_obj_remove_flag(s_pit_body[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_pit_accent[i], LV_OBJ_FLAG_HIDDEN);
}

static void pit_slot_show(int i, bool show)
{
    bsp_display_lock(0);
    if (show) {
        lv_obj_remove_flag(s_pit_body[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_pit_accent[i], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pit_body[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pit_accent[i], LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

static void held_prize_show(int kind, bool show)
{
    bsp_display_lock(0);
    if (show) {
        lv_obj_set_style_bg_color(s_held_body, lv_color_hex(PRIZE_STYLE[kind].base), 0);
        lv_obj_set_style_bg_color(s_held_accent, lv_color_hex(PRIZE_STYLE[kind].accent), 0);
        lv_obj_remove_flag(s_held_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_held_accent, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_held_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_held_accent, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

// cx/cy = 目标中心坐标(供上升跟随 / 落架飞行插值复用)
static void position_held_prize_at(int cx, int cy)
{
    bsp_display_lock(0);
    lv_obj_set_pos(s_held_body, cx - PRIZE_HELD_SZ / 2, cy - PRIZE_HELD_SZ / 2);
    lv_obj_set_pos(s_held_accent, cx - PRIZE_HELD_SZ / 2 + 7, cy - PRIZE_HELD_SZ / 2 + 3);
    bsp_display_unlock();
}

static void mark_rack_collected(int kind)
{
    if (kind < 0 || kind >= PRIZE_TYPES || s_rack_got[kind]) return;
    s_rack_got[kind] = true;
    s_rack_count++;
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_rack_body[kind], lv_color_hex(PRIZE_STYLE[kind].base), 0);
    lv_obj_set_style_bg_color(s_rack_accent[kind], lv_color_hex(PRIZE_STYLE[kind].accent), 0);
    bsp_display_unlock();
}

// Fisher-Yates 洗一批新战利品坑位(5 种各一,顺序随机)
static void shuffle_pit(void)
{
    int order[PRIZE_TYPES];
    for (int i = 0; i < PRIZE_TYPES; i++) order[i] = i;
    for (int i = PRIZE_TYPES - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
    for (int i = 0; i < PRIZE_TYPES; i++) {
        s_pit_kind[i]    = order[i];
        s_pit_present[i] = true;
    }
}

static void reset_round(void)
{
    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) {
        s_rack_got[i] = false;
        lv_obj_set_style_bg_color(s_rack_body[i], lv_color_hex(UNCOLLECTED_BASE), 0);
        lv_obj_set_style_bg_color(s_rack_accent[i], lv_color_hex(UNCOLLECTED_ACCENT), 0);
        lv_obj_set_y(s_rack_body[i], RACK_Y);
        lv_obj_set_y(s_rack_accent[i], RACK_Y + 3);
    }
    bsp_display_unlock();
    s_rack_count = 0;

    shuffle_pit();
    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) pit_slot_paint(i);
    bsp_display_unlock();
}

// ── 无字提示卡(没认到任何 Chain 节点时显示,SPEC §3 "沿用现状逻辑")───────────
static void hint_card_apply(bool show)
{
    if (show == s_hint_shown) return;
    s_hint_shown = show;
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

// ── 派对彩纸 ─────────────────────────────────────────────────────────
static void confetti_show(bool show)
{
    bsp_display_lock(0);
    for (int i = 0; i < CONFETTI_N; i++) {
        if (show) {
            int x = (int)(esp_random() % (SCREEN_W - 8));
            int y = RACK_Y + RACK_SLOT + 4 + (int)(esp_random() % 60);
            uint8_t r, g, b;
            chain_lab_hue2rgb((int)(esp_random() % 360), &r, &g, &b);
            lv_obj_set_pos(s_confetti[i], x, y);
            lv_obj_set_style_bg_color(s_confetti[i], lv_color_make(r, g, b), 0);
            s_confetti_vx[i] = (int)(esp_random() % 3) - 1;   // -1..1
            lv_obj_remove_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    bsp_display_unlock();
}

// ── 摇杆彩蛋:抖一下爪子(不影响进度)──────────────────────────────────────
static void trigger_wiggle(void)
{
    s_wiggle_frames = WIGGLE_FRAMES;
    audio_fx_play(SND_BUMP_LIGHT);
}

static void tick_wiggle(void)
{
    if (s_wiggle_frames <= 0) { s_wiggle_dx = 0; return; }
    s_wiggle_frames--;
    s_wiggle_dx = (s_wiggle_frames & 1) ? 3 : -3;
    if (s_wiggle_frames == 0) s_wiggle_dx = 0;
}

// ── 坑位命中测试(中心点容差带,SPEC §5)───────────────────────────────────
static int find_pit_slot_near(int x)
{
    int best = -1, best_d = GRAB_ALIGN_TOL_PX + 1;
    for (int i = 0; i < PRIZE_TYPES; i++) {
        if (!s_pit_present[i]) continue;
        int d = abs(x - s_pit_slot_x[i]);
        if (d <= GRAB_ALIGN_TOL_PX && d < best_d) { best = i; best_d = d; }
    }
    return best;
}

// ── 状态转换 ─────────────────────────────────────────────────────────
static void enter_grabbing(void)
{
    s_state = CRANE_GRABBING;
    s_state_frames = 0;
    s_grabbed_slot = find_pit_slot_near(s_arm_x);

    led_base_set(LED_BASE_AMBIENT);
    chain_lab_enc_rgb(255, 255, 255);   // 闪白(复用现状按下逻辑,SPEC §4)
    audio_fx_play(SND_COLLECT);
    haptics_play(HAPTIC_BUMP_MED);
    ledstrip_fx_trigger(LED_FX_FLASH);

    if (s_grabbed_slot >= 0) {
        s_pit_present[s_grabbed_slot] = false;
        pit_slot_show(s_grabbed_slot, false);
        held_prize_show(s_pit_kind[s_grabbed_slot], true);
        position_held_prize_at(s_arm_x, RAIL_Y + ARM_H + s_depth + CLAW_H / 2);
    }
}

static void enter_ascending(void)
{
    s_state = CRANE_ASCENDING;
    s_state_frames = 0;
    s_ascend_from_depth = s_depth;

    if (s_grabbed_slot >= 0) {
        chain_lab_enc_rgb(20, 200, 90);   // 成功色:绿
        audio_fx_play_notes((audio_note_t[]){ { 659, 90, 45 }, { 880, 110, 50 } }, 2);
        haptics_play(HAPTIC_COLLECT);
        ledstrip_fx_trigger(LED_FX_COLLECT);
    } else {
        chain_lab_enc_rgb(255, 170, 40);   // 中性琥珀,不用红色(零失败,SPEC §5)
        audio_fx_play(SND_BUMP_LIGHT);
        haptics_play(HAPTIC_BUMP_LIGHT);
        ledstrip_fx_trigger(LED_FX_BUMP);
    }
}

static void enter_deposit(void)
{
    s_state = CRANE_DEPOSIT;
    s_state_frames = 0;

    if (s_grabbed_slot >= 0) {
        s_deposit_from_x = s_arm_x;
        s_deposit_from_y = RAIL_Y + ARM_H + CLAW_H / 2;
        s_deposit_to_x   = s_rack_slot_x[s_pit_kind[s_grabbed_slot]] + RACK_SLOT / 2;
        s_deposit_to_y   = RACK_Y + RACK_SLOT / 2;
    }
}

static void enter_party(void)
{
    s_state = CRANE_PARTY;
    s_state_frames = 0;
    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
    confetti_show(true);
}

// 编码器每格增量应用到深度;顺时针+/逆时针-;接近底部自动到底并直接抓取(SPEC §5/§9)
static void apply_descend_delta(int delta)
{
    int nd = s_depth + delta * DESCEND_PER_TICK;
    if (nd < 0) nd = 0;
    if (nd > DESCEND_MAX_PX) nd = DESCEND_MAX_PX;
    s_depth = nd;

    if (s_depth >= DESCEND_MAX_PX - DESCEND_SNAP_TOL) {
        s_depth = DESCEND_MAX_PX;
        enter_grabbing();
        return;
    }

    s_state = (s_depth > 0) ? CRANE_DESCENDING : CRANE_PLAY_IDLE;
    led_base_set(s_depth > 0 ? LED_BASE_NEAR : LED_BASE_AMBIENT);
    set_depth_led(s_depth);
}

// ── 每状态的每帧推进 ─────────────────────────────────────────────────────
static void tick_grabbing(void)
{
    s_state_frames++;
    int total = GRAB_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)s_state_frames / total, 0.f, 1.f);
    s_claw_w = CLAW_OPEN_W + (int)((CLAW_CLOSED_W - CLAW_OPEN_W) * t);

    if (s_grabbed_slot >= 0) {
        position_held_prize_at(s_arm_x, RAIL_Y + ARM_H + s_depth + CLAW_H / 2);
    }

    if (s_state_frames >= total) enter_ascending();
}

static void tick_ascending(void)
{
    s_state_frames++;
    int total = ASCEND_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)s_state_frames / total, 0.f, 1.f);
    s_depth = (int)(s_ascend_from_depth * (1.0f - t));

    if (s_grabbed_slot >= 0) {
        position_held_prize_at(s_arm_x, RAIL_Y + ARM_H + s_depth + CLAW_H / 2);
    }

    if (s_state_frames >= total) {
        s_depth  = 0;
        s_claw_w = CLAW_OPEN_W;
        enter_deposit();
    }
}

static void tick_deposit(void)
{
    s_state_frames++;
    int total = DEPOSIT_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)s_state_frames / total, 0.f, 1.f);

    if (s_grabbed_slot >= 0) {
        int hx = s_deposit_from_x + (int)((s_deposit_to_x - s_deposit_from_x) * t);
        int hy = s_deposit_from_y + (int)((s_deposit_to_y - s_deposit_from_y) * t);
        position_held_prize_at(hx, hy);
    } else {
        float phase = (t < 0.5f) ? (t / 0.5f) : (1.0f - (t - 0.5f) / 0.5f);
        s_bounce_dy = -(int)(6 * phase);   // 空爪"诶?"弹一下,不是负面反馈(SPEC §5)
    }

    if (s_state_frames >= total) {
        s_bounce_dy = 0;
        if (s_grabbed_slot >= 0) {
            int kind = s_pit_kind[s_grabbed_slot];
            held_prize_show(kind, false);
            mark_rack_collected(kind);
            s_grabbed_slot = -1;
        }
        set_depth_led(0);
        if (s_rack_count >= PRIZE_TYPES) enter_party();
        else                              s_state = CRANE_PLAY_IDLE;
    }
}

static void tick_party(void)
{
    s_state_frames++;
    int total = PARTY_HOLD_MS / POLL_PERIOD_MS; if (total < 1) total = 1;

    int hue = (s_state_frames * 9) % 360;
    uint8_t r, g, b;
    chain_lab_hue2rgb(hue, &r, &g, &b);
    chain_lab_enc_rgb(r, g, b);
    chain_lab_joy_rgb(r, g, b);   // 两节点同步跑彩虹(SPEC §4)

    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) {
        float phase = (float)s_state_frames * 0.35f + i * 1.3f;
        int dy = -(int)(5 + 5 * sinf(phase));
        lv_obj_set_y(s_rack_body[i], RACK_Y + dy);
        lv_obj_set_y(s_rack_accent[i], RACK_Y + dy + 3);
    }
    for (int i = 0; i < CONFETTI_N; i++) {
        int y = lv_obj_get_y(s_confetti[i]) + 3;
        if (y > CAB_Y1) y = RACK_Y + RACK_SLOT + 4;
        int x = lv_obj_get_x(s_confetti[i]) + s_confetti_vx[i];
        if (x < 0 || x > SCREEN_W - 8) s_confetti_vx[i] = -s_confetti_vx[i];
        lv_obj_set_pos(s_confetti[i], x, y);
    }
    bsp_display_unlock();

    if (s_state_frames >= total) {
        confetti_show(false);
        reset_round();
        s_state = CRANE_PLAY_IDLE;
    }
}

// ── 输入处理(每帧,SPEC §2/§3)────────────────────────────────────────────
static void handle_joystick(void)
{
    if (!chain_lab_joy_attached()) return;

    float nx = chain_lab_joy_x();
    float ny = chain_lab_joy_y();

    int target = SCREEN_W / 2 + (int)(nx * (CRANE_X_RANGE_PX / 2));
    if (target < ARM_X_MIN) target = ARM_X_MIN;
    if (target > ARM_X_MAX) target = ARM_X_MAX;
    s_arm_x = target;   // 摇杆随时可移吊臂,不受深度状态门控(SPEC §3)

    if (s_state != CRANE_PARTY) {   // 沿用现状既有映射(nx/ny→r/b);派对期间被彩虹覆盖
        uint8_t r = (uint8_t)(clampf((nx + 1) * 0.5f, 0.f, 1.f) * 255);
        uint8_t b = (uint8_t)(clampf((ny + 1) * 0.5f, 0.f, 1.f) * 255);
        chain_lab_joy_rgb(r, 60, b);
    }

    bool btn = chain_lab_joy_button();
    if (btn && !s_prev_joy_btn) trigger_wiggle();
    s_prev_joy_btn = btn;
}

static void handle_encoder(void)
{
    int16_t now = chain_lab_enc_attached() ? chain_lab_enc_value() : s_prev_enc_value;
    int delta = (int)now - (int)s_prev_enc_value;
    s_prev_enc_value = now;

    bool btn = chain_lab_enc_attached() && chain_lab_enc_button();
    bool press_edge = btn && !s_prev_enc_btn;
    s_prev_enc_btn = btn;

    if (s_state == CRANE_PLAY_IDLE || s_state == CRANE_DESCENDING) {
        if (delta != 0 && chain_lab_enc_attached()) apply_descend_delta(delta);
        // 深度门控只在 IDLE/DESCENDING 接受抓取键;GRABBING/ASCENDING/DEPOSIT/PARTY 期间吃掉(SPEC §3/§5)
        if ((s_state == CRANE_PLAY_IDLE || s_state == CRANE_DESCENDING) && press_edge) enter_grabbing();
    }
}

// ── 渲染(动态层:吊臂/缆绳/爪子,每帧小脏矩形,SPEC §10)───────────────────
static void render_frame(void)
{
    bsp_display_lock(0);

    int arm_y  = RAIL_Y + s_bounce_dy;
    int claw_y = RAIL_Y + ARM_H + s_depth + s_bounce_dy;

    lv_obj_set_pos(s_arm, s_arm_x - ARM_W / 2, arm_y);
    lv_obj_set_pos(s_cable, s_arm_x - CABLE_W / 2, arm_y + ARM_H);
    lv_obj_set_size(s_cable, CABLE_W, s_depth > 0 ? s_depth : 1);
    lv_obj_set_size(s_claw, s_claw_w, CLAW_H);
    lv_obj_set_pos(s_claw, s_arm_x - s_claw_w / 2 + s_wiggle_dx, claw_y);

    bsp_display_unlock();
}

// ── 对外接口 ─────────────────────────────────────────────────────────
void crane_game_create(void)
{
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COL), 0);

    // 静态层:展示架背板 + 机身面板 + 轨道(载入时画一次,SPEC §10)
    lv_obj_t *rack_bg = plain(scr, SCREEN_W - 12, RACK_SLOT + 8, RACK_BG_COL, 10);
    lv_obj_set_pos(rack_bg, 6, 0);

    lv_obj_t *cabinet = plain(scr, SCREEN_W - 12, CAB_Y1 - CAB_Y0, PANEL_COL, 14);
    lv_obj_set_pos(cabinet, 6, CAB_Y0);

    lv_obj_t *rail = plain(scr, CRANE_X_RANGE_PX + ARM_W, 6, RAIL_COL, 3);
    lv_obj_set_pos(rail, ARM_X_MIN - ARM_W / 2, RAIL_Y - 3);

    // 展示架:PRIZE_TYPES 槽,初始灰底"未收集"(同 peekaboo 相册的占位手法)
    for (int i = 0; i < PRIZE_TYPES; i++) {
        int x = RACK_X0 + i * (RACK_SLOT + RACK_GAP);
        s_rack_slot_x[i] = x;
        lv_obj_t *body = plain(scr, RACK_SLOT, RACK_SLOT, UNCOLLECTED_BASE, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(body, x, RACK_Y);
        lv_obj_t *accent = plain(scr, 10, 10, UNCOLLECTED_ACCENT, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(accent, x + 9, RACK_Y + 3);
        s_rack_body[i]   = body;
        s_rack_accent[i] = accent;
    }

    // 战利品坑:横向铺满 CRANE_X_RANGE_PX,与吊臂可达范围一一对齐
    for (int i = 0; i < PRIZE_TYPES; i++) {
        s_pit_slot_x[i] = (PRIZE_TYPES > 1)
            ? ARM_X_MIN + i * (CRANE_X_RANGE_PX / (PRIZE_TYPES - 1))
            : SCREEN_W / 2;
        lv_obj_t *body = plain(scr, PRIZE_SZ, PRIZE_SZ, UNCOLLECTED_BASE, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(body, s_pit_slot_x[i] - PRIZE_SZ / 2, PIT_Y);
        lv_obj_t *accent = plain(scr, 10, 10, 0xFFFFFF, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(accent, s_pit_slot_x[i] - PRIZE_SZ / 2 + 8, PIT_Y + 4);
        s_pit_body[i]   = body;
        s_pit_accent[i] = accent;
    }

    // 被抓中的战利品跟随精灵(初始隐藏)
    s_held_body   = plain(scr, PRIZE_HELD_SZ, PRIZE_HELD_SZ, 0xFFFFFF, LV_RADIUS_CIRCLE);
    s_held_accent = plain(scr, 8, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(s_held_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_held_accent, LV_OBJ_FLAG_HIDDEN);

    // 动态层:吊臂(小车)/ 缆绳 / 爪子
    s_arm   = plain(scr, ARM_W, ARM_H, ARM_COL, 6);
    s_cable = plain(scr, CABLE_W, 1, CABLE_COL, 2);
    s_claw  = plain(scr, CLAW_OPEN_W, CLAW_H, CLAW_COL, 4);

    // 派对彩纸(初始隐藏)
    for (int i = 0; i < CONFETTI_N; i++) {
        lv_obj_t *c = plain(scr, 6, 6, 0xFFFFFF, 2);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        s_confetti[i] = c;
    }

    // 无字提示卡(没认到任何 Chain 节点时显示;仿 peekaboo 插头图标,不用文字)
    s_hint_card = plain(scr, 132, 76, 0xFFFFFF, 14);
    lv_obj_align(s_hint_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *socket = plain(s_hint_card, 30, 30, 0xE0DACB, LV_RADIUS_CIRCLE);
    lv_obj_align(socket, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *hole = plain(socket, 12, 12, 0x8A8272, LV_RADIUS_CIRCLE);
    lv_obj_center(hole);
    lv_obj_t *wire = plain(s_hint_card, 34, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 48, 0);
    lv_obj_t *plug = plain(s_hint_card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 82, 0);
    lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    s_hint_shown = false;

    bsp_display_unlock();

    shuffle_pit();
    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) pit_slot_paint(i);
    bsp_display_unlock();

    s_arm_x = SCREEN_W / 2;
    s_depth = 0;
    s_state = CRANE_PLAY_IDLE;
    set_depth_led(0);
    led_base_set(LED_BASE_AMBIENT);

    render_frame();
}

void crane_game_tick(void)
{
    handle_joystick();
    handle_encoder();

    switch (s_state) {
    case CRANE_GRABBING:  tick_grabbing();  break;
    case CRANE_ASCENDING: tick_ascending(); break;
    case CRANE_DEPOSIT:   tick_deposit();   break;
    case CRANE_PARTY:     tick_party();     break;
    default: break;
    }

    tick_wiggle();
    render_frame();
}

void crane_game_sync_attach(void)
{
    bool enc_on = chain_lab_enc_attached();
    bool joy_on = chain_lab_joy_attached();

    // 节点刚接上(含深度省电重扫后):重同步基准,避免节点计数复位被误读成一次大幅转动
    if (enc_on && !s_enc_was_attached) s_prev_enc_value = chain_lab_enc_value();
    s_enc_was_attached = enc_on;

    hint_card_apply(!enc_on && !joy_on);
}

void crane_game_reset_position(void)
{
    s_state         = CRANE_PLAY_IDLE;
    s_state_frames  = 0;
    s_depth         = 0;
    s_claw_w        = CLAW_OPEN_W;
    s_bounce_dy     = 0;
    s_wiggle_dx     = 0;
    s_wiggle_frames = 0;
    s_arm_x         = SCREEN_W / 2;

    if (s_grabbed_slot >= 0) {   // 深度省电打断了正在进行的抓取:战利品还给坑,不计入收集
        held_prize_show(s_pit_kind[s_grabbed_slot], false);
        s_pit_present[s_grabbed_slot] = true;
        pit_slot_show(s_grabbed_slot, true);
        s_grabbed_slot = -1;
    }

    s_prev_enc_value = chain_lab_enc_attached() ? chain_lab_enc_value() : 0;
    s_prev_enc_btn   = false;
    s_prev_joy_btn   = false;

    set_depth_led(0);
    led_base_set(LED_BASE_AMBIENT);
    render_frame();
}
