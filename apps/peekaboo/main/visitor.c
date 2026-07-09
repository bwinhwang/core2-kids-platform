// 躲猫猫昼夜屋 v2 —— 访客层实现(见 visitor.h)
#include "visitor.h"

#include <stdint.h>
#include <string.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"

#include "audio_fx.h"
#include "scene.h"
#include "tuning.h"

#define VISITOR_W          44
#define VISITOR_H          40
#define VISITOR_STAND_L_X  56
#define VISITOR_STAND_R_X  220

typedef struct {
    uint32_t     primary;
    int          hide_side;    // 0=左草丛 1=右草丛(RAINBOW 不用)
    uint32_t     eye_color;
    int          eye_w, eye_h;
    int          eye_dy;       // 夜眼相对基准位置的额外上移(FROG 顶泡眼)
    audio_note_t call[5];
    int          call_n;
    int          entrance_ms;
} visitor_def_t;

// id: 0=CAT 1=OWL 2=FROG 3=BUNNY 4=DUCK 5=HEDGE 6=RAINBOW(稀客)
static const visitor_def_t TABLE[VISITOR_RAINBOW + 1] = {
    [0] = { 0xFF9E5C, 0, 0xD8E86B, 6, 10, 0,
            { { 880, 90, 55 }, { 659, 140, 50 } }, 2, 500 },
    [1] = { 0xA9744E, 1, 0xFFD98A, 10, 10, 0,
            { { 392, 120, 50 }, { 392, 140, 45 } }, 2, 700 },
    [2] = { 0x7FB069, 0, 0xEAF5D8, 8, 8, -8,
            { { 262, 70, 50 }, { 262, 70, 45 }, { 330, 110, 50 } }, 3, 660 },
    [3] = { 0xF5F0E8, 1, 0xFFB3C6, 6, 6, 0,
            { { 659, 80, 45 }, { 988, 120, 50 } }, 2, 600 },
    [4] = { 0xF2C744, 0, 0x5A4632, 6, 6, 0,
            { { 587, 90, 55 }, { 523, 90, 50 }, { 587, 110, 50 } }, 3, 800 },
    [5] = { 0x8A6E5A, 1, 0x2A2622, 4, 4, 0,
            { { 330, 60, 40 }, { 294, 60, 40 } }, 2, 900 },
    [VISITOR_RAINBOW] = { 0xFFF3B0, 1, 0, 0, 0, 0,
            { { 523, 60, 60 }, { 659, 60, 60 }, { 784, 60, 60 }, { 1047, 80, 60 }, { 1319, 120, 65 } }, 5, 800 },
};

static const audio_note_t RUSTLE_NOTES[3] = { { 1200, 25, 18 }, { 0, 40, 0 }, { 1400, 25, 15 } };
static const audio_note_t METEOR_NOTE[1]  = { { 1568, 50, 20 } };

// ── 状态 ─────────────────────────────────────────────────────────────
static lv_obj_t *s_body[VISITOR_RAINBOW + 1];      // 每位访客的精灵容器(移动/隐藏整个它)
static lv_obj_t *s_eye1[VISITOR_POOL_N], *s_eye2[VISITOR_POOL_N];   // 夜眼(常驻 6 位才有)

static int  s_stage_id = -1;
static bool s_rustled;
static bool s_eyes_shown;
static int  s_next_call_ms;
static int  s_last_meteor_seg;

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

static lv_obj_t *group(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void anim_set_x(void *o, int32_t v)   { lv_obj_set_x((lv_obj_t *)o, v); }
static void anim_set_y(void *o, int32_t v)   { lv_obj_set_y((lv_obj_t *)o, v); }
static void anim_set_opa(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

static inline int stand_x(int id)
{
    if (id == VISITOR_RAINBOW) return (SCREEN_W - VISITOR_W) / 2 + 40;
    return TABLE[id].hide_side == 0 ? VISITOR_STAND_L_X : VISITOR_STAND_R_X;
}
static inline int stand_y(int id) { (void)id; return GROUND_Y - VISITOR_H; }
static inline int hide_x(int id)  { return TABLE[id].hide_side == 0 ? GRASS_L_X : GRASS_R_X + GRASS_W - VISITOR_W; }

// ── 精灵构建(§5.1 造型要点,扁平色块拼装)─────────────────────────────────
static void build_body(int id, lv_obj_t *scr)
{
    lv_obj_t *g = group(scr, VISITOR_W, VISITOR_H);
    lv_obj_set_pos(g, stand_x(id), stand_y(id));
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
    s_body[id] = g;

    uint32_t c = TABLE[id].primary;
    switch (id) {
    case 0: {   // CAT:圆脸 + 三角耳(近似圆点)+ 白肚 + 粉鼻
        plain(g, 30, 30, c, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(lv_obj_get_child(g, 0), 7, 8);
        plain(g, 10, 10, c, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 1), 3, 0);
        plain(g, 10, 10, c, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 2), 25, 0);
        plain(g, 14, 12, 0xFFFFFF, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 3), 15, 22);
        plain(g, 4, 4, 0xFFB3C6, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 4), 20, 20);
        break;
    }
    case 1: {   // OWL:圆身 + 大眼白 + 三角喙 + 耳羽
        plain(g, 34, 34, c, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(lv_obj_get_child(g, 0), 5, 4);
        plain(g, 12, 12, 0xFFFFFF, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 1), 8, 10);
        plain(g, 12, 12, 0xFFFFFF, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 2), 22, 10);
        plain(g, 6, 6, 0xFB8B24, 2); lv_obj_set_pos(lv_obj_get_child(g, 3), 19, 20);
        plain(g, 5, 6, c, 2); lv_obj_set_pos(lv_obj_get_child(g, 4), 8, 0);
        plain(g, 5, 6, c, 2); lv_obj_set_pos(lv_obj_get_child(g, 5), 29, 0);
        break;
    }
    case 2: {   // FROG:扁圆身 + 顶泡眼 + 宽嘴线
        plain(g, 34, 20, c, 10);
        lv_obj_set_pos(lv_obj_get_child(g, 0), 5, 16);
        plain(g, 12, 12, c, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 1), 6, 2);
        plain(g, 12, 12, c, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 2), 24, 2);
        plain(g, 5, 5, 0xEAF5D8, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 3), 9, 5);
        plain(g, 5, 5, 0xEAF5D8, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 4), 27, 5);
        plain(g, 16, 3, 0x3A3A38, 2); lv_obj_set_pos(lv_obj_get_child(g, 5), 14, 27);
        break;
    }
    case 3: {   // BUNNY:圆身 + 长耳(内粉) + 粉鼻
        plain(g, 8, 22, c, 4); lv_obj_set_pos(lv_obj_get_child(g, 0), 6, 0);
        plain(g, 8, 22, c, 4); lv_obj_set_pos(lv_obj_get_child(g, 1), 30, 0);
        plain(g, 4, 14, 0xFFB3C6, 3); lv_obj_set_pos(lv_obj_get_child(g, 2), 8, 4);
        plain(g, 4, 14, 0xFFB3C6, 3); lv_obj_set_pos(lv_obj_get_child(g, 3), 32, 4);
        plain(g, 28, 28, c, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 4), 8, 12);
        plain(g, 4, 4, 0xFFB3C6, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 5), 20, 24);
        break;
    }
    case 4: {   // DUCK:圆身 + 橙扁喙 + 小翅
        plain(g, 30, 30, c, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 0), 7, 10);
        plain(g, 10, 14, c, 5); lv_obj_set_pos(lv_obj_get_child(g, 1), 4, 16);
        plain(g, 14, 8, 0xFB8B24, 4); lv_obj_set_pos(lv_obj_get_child(g, 2), 15, 20);
        break;
    }
    case 5: {   // HEDGE:半圆身 + 背刺 4~5 + 尖鼻
        plain(g, 34, 22, c, 12); lv_obj_set_pos(lv_obj_get_child(g, 0), 5, 14);
        static const int spx[5] = { 8, 15, 22, 29, 34 };
        for (int i = 0; i < 5; i++) {
            plain(g, 5, 10, 0x5A4A3E, 2);
            lv_obj_set_pos(lv_obj_get_child(g, 1 + i), spx[i], 6);
        }
        plain(g, 6, 6, 0x2A2622, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 6), 1, 22);
        break;
    }
    case VISITOR_RAINBOW: {   // 彩虹鸟:圆鸟 + 彩虹翅(三色条) + 喙
        plain(g, 26, 26, c, LV_RADIUS_CIRCLE); lv_obj_set_pos(lv_obj_get_child(g, 0), 9, 8);
        plain(g, 20, 5, 0xFF8FB0, 2); lv_obj_set_pos(lv_obj_get_child(g, 1), 10, 16);
        plain(g, 20, 5, 0xFFD23F, 2); lv_obj_set_pos(lv_obj_get_child(g, 2), 10, 21);
        plain(g, 20, 5, 0x7FD0C0, 2); lv_obj_set_pos(lv_obj_get_child(g, 3), 10, 26);
        plain(g, 6, 5, 0xFB8B24, 2); lv_obj_set_pos(lv_obj_get_child(g, 4), 2, 14);
        break;
    }
    default: break;
    }
}

static void build_eyes(int id, lv_obj_t *scr)
{
    const visitor_def_t *v = &TABLE[id];
    int ex = v->hide_side == 0 ? EYE_L_X : EYE_R_X;
    int ey = v->hide_side == 0 ? EYE_L_Y : EYE_R_Y;
    ey += v->eye_dy;

    lv_obj_t *e1 = plain(scr, v->eye_w, v->eye_h, v->eye_color, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(e1, ex - v->eye_w - 3, ey);
    lv_obj_add_flag(e1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *e2 = plain(scr, v->eye_w, v->eye_h, v->eye_color, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(e2, ex + 3, ey);
    lv_obj_add_flag(e2, LV_OBJ_FLAG_HIDDEN);

    s_eye1[id] = e1;
    s_eye2[id] = e2;
}

void visitor_create(lv_obj_t *scr)
{
    for (int i = 0; i <= VISITOR_RAINBOW; i++) build_body(i, scr);
    for (int i = 0; i < VISITOR_POOL_N; i++)   build_eyes(i, scr);
    s_stage_id = -1;
    s_last_meteor_seg = -1;
}

uint32_t visitor_color(int id)
{
    if (id < 0 || id > VISITOR_RAINBOW) return 0;
    return TABLE[id].primary;
}

// ── 抽签 ─────────────────────────────────────────────────────────────
int visitor_draw_for_night(uint8_t collected_mask, int last_id)
{
    int pool[VISITOR_POOL_N];
    int n = 0;
    for (int i = 0; i < VISITOR_POOL_N; i++)
        if (!(collected_mask & (1u << i))) pool[n++] = i;
    if (n == 0)   // 兜底(理论上游行已重置 mask,不会到这)
        for (int i = 0; i < VISITOR_POOL_N; i++) pool[n++] = i;

    int pick;
    int guard = 0;
    do {
        pick = pool[esp_random() % n];
    } while (n > 1 && pick == last_id && ++guard < 8);

    if (RARE_PCT > 0 && (int)(esp_random() % 100) < RARE_PCT) pick = VISITOR_RAINBOW;

    s_stage_id = pick;
    return pick;
}

// ── 悬念 ─────────────────────────────────────────────────────────────
void visitor_tease_start(int visitor_id)
{
    (void)visitor_id;
    s_rustled = false;
    s_eyes_shown = false;
    s_next_call_ms = TEASE_EYES_MS + TEASE_CALL_PERIOD_MS;
    s_last_meteor_seg = -1;
}

int visitor_tease_wake_resume(int visitor_id)
{
    (void)visitor_id;
    s_rustled = s_eyes_shown;
    int base_ms = s_eyes_shown ? TEASE_EYES_MS : 0;
    s_next_call_ms = base_ms + TEASE_CALL_PERIOD_MS;
    s_last_meteor_seg = -1;
    return base_ms;
}

static void eyes_show(int id)
{
    bsp_display_lock(0);
    lv_obj_t *e1 = s_eye1[id], *e2 = s_eye2[id];
    lv_obj_remove_flag(e1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(e2, LV_OBJ_FLAG_HIDDEN);
    for (int k = 0; k < 2; k++) {
        lv_obj_t *e = k == 0 ? e1 : e2;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, e);
        lv_anim_set_exec_cb(&a, anim_set_opa);
        lv_anim_set_values(&a, 90, 255);
        lv_anim_set_duration(&a, 700);
        lv_anim_set_playback_duration(&a, 700);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
    bsp_display_unlock();
}

void visitor_tease_tick(int visitor_id, int elapsed_ms)
{
    if (visitor_id == VISITOR_RAINBOW) {
        int seg = elapsed_ms / 1200;
        if (seg != s_last_meteor_seg) {
            s_last_meteor_seg = seg;
            scene_shooting_star_trigger();
            audio_fx_play_notes(METEOR_NOTE, 1);
        }
        return;
    }
    if (visitor_id < 0 || visitor_id >= VISITOR_POOL_N) return;

    if (!s_rustled && elapsed_ms >= TEASE_RUSTLE_MS) {
        s_rustled = true;
        scene_grass_rustle(TABLE[visitor_id].hide_side);
        audio_fx_play_notes(RUSTLE_NOTES, 3);
    }
    if (!s_eyes_shown && elapsed_ms >= TEASE_EYES_MS) {
        s_eyes_shown = true;
        eyes_show(visitor_id);
    }
    if (s_eyes_shown && elapsed_ms >= s_next_call_ms) {
        s_next_call_ms += TEASE_CALL_PERIOD_MS;
        const visitor_def_t *v = &TABLE[visitor_id];
        audio_note_t notes[5];
        for (int i = 0; i < v->call_n; i++) {
            uint16_t f = (uint16_t)(v->call[i].freq_hz * TEASE_CALL_FREQ_PCT / 100);
            if (v->call[i].freq_hz > 0 && f < 200) f = 200;
            notes[i].freq_hz = f;
            notes[i].ms = (uint16_t)(v->call[i].ms * 12 / 10);
            notes[i].amp = (uint8_t)(v->call[i].amp * TEASE_CALL_AMP_PCT / 100);
        }
        audio_fx_play_notes(notes, v->call_n);
    }
}

// ── 出场编排 ──────────────────────────────────────────────────────────
static void idle_bob_start(int id)
{
    lv_obj_t *o = s_body[id];
    int y = stand_y(id);
    lv_anim_delete(o, anim_set_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, anim_set_y);
    lv_anim_set_values(&a, y, y - 2);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_playback_duration(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void entrance_settle_cb(lv_anim_t *a)
{
    int id = (int)(intptr_t)lv_anim_get_user_data(a);
    lv_obj_t *o = s_body[id];
    lv_obj_set_x(o, stand_x(id));
    lv_obj_set_y(o, stand_y(id));
    idle_bob_start(id);
}

static void shake_start_cb(lv_anim_t *a)
{
    int id = (int)(intptr_t)lv_anim_get_user_data(a);
    lv_obj_t *o = s_body[id];
    int x = stand_x(id);
    lv_obj_set_x(o, x);
    lv_anim_t sh;
    lv_anim_init(&sh);
    lv_anim_set_var(&sh, o);
    lv_anim_set_exec_cb(&sh, anim_set_x);
    lv_anim_set_values(&sh, x - 2, x + 2);
    lv_anim_set_duration(&sh, 60);
    lv_anim_set_playback_duration(&sh, 60);
    lv_anim_set_repeat_count(&sh, 3);
    lv_anim_set_path_cb(&sh, lv_anim_path_ease_in_out);
    lv_anim_set_user_data(&sh, (void *)(intptr_t)id);
    lv_anim_set_completed_cb(&sh, entrance_settle_cb);
    lv_anim_start(&sh);
}

// 慢掀两段出场:from→mid(dur*0.4)→停 ENTR_PEEK_HOLD_MS→mid→to(dur*0.6);其余速度档单段。
static void tween(lv_obj_t *o, lv_anim_exec_xcb_t exec, int from, int to, int dur_ms,
                   lv_anim_path_cb_t path, dawn_speed_t speed, lv_anim_completed_cb_t done, int id)
{
    if (speed == DAWN_SLOW) {
        int mid = from + (to - from) / 2;
        int d1 = dur_ms * 4 / 10;
        int d2 = dur_ms - d1;

        lv_anim_t a1;
        lv_anim_init(&a1);
        lv_anim_set_var(&a1, o);
        lv_anim_set_exec_cb(&a1, exec);
        lv_anim_set_values(&a1, from, mid);
        lv_anim_set_duration(&a1, d1);
        lv_anim_set_path_cb(&a1, path);
        lv_anim_start(&a1);

        lv_anim_t a2;
        lv_anim_init(&a2);
        lv_anim_set_var(&a2, o);
        lv_anim_set_exec_cb(&a2, exec);
        lv_anim_set_values(&a2, mid, to);
        lv_anim_set_duration(&a2, d2);
        lv_anim_set_delay(&a2, d1 + ENTR_PEEK_HOLD_MS);
        lv_anim_set_path_cb(&a2, path);
        lv_anim_set_user_data(&a2, (void *)(intptr_t)id);
        if (done) lv_anim_set_completed_cb(&a2, done);
        lv_anim_start(&a2);
    } else {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, o);
        lv_anim_set_exec_cb(&a, exec);
        lv_anim_set_values(&a, from, to);
        lv_anim_set_duration(&a, dur_ms);
        lv_anim_set_path_cb(&a, path);
        lv_anim_set_user_data(&a, (void *)(intptr_t)id);
        if (done) lv_anim_set_completed_cb(&a, done);
        lv_anim_start(&a);
    }
}

static int start_entrance(int id, dawn_speed_t speed)
{
    lv_obj_t *o = s_body[id];
    lv_anim_delete(o, anim_set_x);
    lv_anim_delete(o, anim_set_y);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);

    int sx = stand_x(id), sy = stand_y(id);
    int base_ms = TABLE[id].entrance_ms;
    float scale = speed == DAWN_FAST ? ENTR_FAST_SCALE : speed == DAWN_SLOW ? ENTR_PEEK_SCALE : 1.0f;
    int dur = (int)(base_ms * scale);
    int total = dur + (speed == DAWN_SLOW ? ENTR_PEEK_HOLD_MS : 0);

    switch (id) {
    case 0:   // CAT:蹦出(y overshoot)
        lv_obj_set_pos(o, sx, sy + 30);
        tween(o, anim_set_y, sy + 30, sy, dur, lv_anim_path_overshoot, speed, entrance_settle_cb, id);
        break;
    case 1:   // OWL:竖直升起
        lv_obj_set_pos(o, sx, sy + 36);
        tween(o, anim_set_y, sy + 36, sy, dur, lv_anim_path_ease_out, speed, entrance_settle_cb, id);
        break;
    case 2: {   // FROG:三连小蹦(简化:不做慢掀分段,直接按 dur 缩放跳三次)
        lv_obj_set_pos(o, sx, sy);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, o);
        lv_anim_set_exec_cb(&a, anim_set_y);
        lv_anim_set_values(&a, sy, sy - 10);
        lv_anim_set_duration(&a, dur / 6);
        lv_anim_set_playback_duration(&a, dur / 6);
        lv_anim_set_repeat_count(&a, 3);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_user_data(&a, (void *)(intptr_t)id);
        lv_anim_set_completed_cb(&a, entrance_settle_cb);
        lv_anim_start(&a);
        total = dur;
        break;
    }
    case 3: {   // BUNNY:高跳抛物线
        int hx = hide_x(id);
        lv_obj_set_pos(o, hx, sy);
        lv_anim_t ax;
        lv_anim_init(&ax);
        lv_anim_set_var(&ax, o);
        lv_anim_set_exec_cb(&ax, anim_set_x);
        lv_anim_set_values(&ax, hx, sx);
        lv_anim_set_duration(&ax, dur);
        lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
        lv_anim_start(&ax);
        tween(o, anim_set_y, sy + 26, sy, dur, lv_anim_path_overshoot, speed, entrance_settle_cb, id);
        break;
    }
    case 4: {   // DUCK:摇摆走入(x 主轴 + y 小幅摆动)
        int hx = hide_x(id);
        lv_obj_set_pos(o, hx, sy);
        tween(o, anim_set_x, hx, sx, dur, lv_anim_path_ease_in_out, speed, entrance_settle_cb, id);
        lv_anim_t ay;
        lv_anim_init(&ay);
        lv_anim_set_var(&ay, o);
        lv_anim_set_exec_cb(&ay, anim_set_y);
        lv_anim_set_values(&ay, sy - 3, sy + 3);
        lv_anim_set_duration(&ay, 200);
        lv_anim_set_playback_duration(&ay, 200);
        lv_anim_set_repeat_count(&ay, dur / 400 + 1);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_in_out);
        lv_anim_start(&ay);
        break;
    }
    case 5: {   // HEDGE:慢挪入 + 抖刺收尾
        int hx = hide_x(id);
        lv_obj_set_pos(o, hx, sy);
        tween(o, anim_set_x, hx, sx, dur, lv_anim_path_ease_in_out, speed, shake_start_cb, id);
        break;
    }
    case VISITOR_RAINBOW:   // 彩虹鸟:从天而降
        lv_obj_set_pos(o, sx, -30);
        tween(o, anim_set_y, -30, sy, dur, lv_anim_path_ease_out, speed, entrance_settle_cb, id);
        break;
    default: break;
    }
    return total;
}

static void hide_eyes(int id)
{
    if (id < 0 || id >= VISITOR_POOL_N) return;
    if (s_eye1[id]) { lv_anim_delete(s_eye1[id], anim_set_opa); lv_obj_add_flag(s_eye1[id], LV_OBJ_FLAG_HIDDEN); }
    if (s_eye2[id]) { lv_anim_delete(s_eye2[id], anim_set_opa); lv_obj_add_flag(s_eye2[id], LV_OBJ_FLAG_HIDDEN); }
}

static void play_reveal_sound(int id, dawn_speed_t speed)
{
    if (id == VISITOR_RAINBOW) {
        audio_fx_play_notes(TABLE[VISITOR_RAINBOW].call, TABLE[VISITOR_RAINBOW].call_n);
        return;
    }
    const visitor_def_t *v = &TABLE[id];
    audio_note_t notes[8];
    int n = 0;
    if (speed == DAWN_FAST) {
        notes[n++] = (audio_note_t){ 1046, 50, 65 };
        notes[n++] = (audio_note_t){ 784, 90, 55 };
    }
    for (int i = 0; i < v->call_n && n < 8; i++) {
        audio_note_t note = v->call[i];
        if (speed == DAWN_SLOW) note.amp = (uint8_t)(note.amp * 7 / 10);
        notes[n++] = note;
    }
    audio_fx_play_notes(notes, n);
}

int visitor_reveal(int visitor_id, dawn_speed_t speed, bool first_time)
{
    (void)first_time;   // 入册反馈由调用方按返回时长延时触发(album 归它管)

    bsp_display_lock(0);
    hide_eyes(visitor_id);
    play_reveal_sound(visitor_id, speed);
    int total = start_entrance(visitor_id, speed);
    int dir = (visitor_id == VISITOR_RAINBOW) ? 1 : (TABLE[visitor_id].hide_side == 0 ? -1 : 1);
    scene_char_gaze(dir * GAZE_DX);
    bsp_display_unlock();

    s_stage_id = visitor_id;
    return total;
}

void visitor_hide_all_instant(void)
{
    bsp_display_lock(0);
    for (int i = 0; i <= VISITOR_RAINBOW; i++) {
        lv_obj_t *o = s_body[i];
        if (!o) continue;
        lv_anim_delete(o, anim_set_x);
        lv_anim_delete(o, anim_set_y);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < VISITOR_POOL_N; i++) hide_eyes(i);
    bsp_display_unlock();
    s_stage_id = -1;
}

int visitor_parade_start(void)
{
    bsp_display_lock(0);
    for (int i = 0; i < VISITOR_POOL_N; i++) {
        lv_obj_t *o = s_body[i];
        lv_anim_delete(o, anim_set_x);
        lv_anim_delete(o, anim_set_y);
        int y = stand_y(i);
        lv_obj_set_pos(o, -VISITOR_W, y);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);

        lv_anim_t ax;
        lv_anim_init(&ax);
        lv_anim_set_var(&ax, o);
        lv_anim_set_exec_cb(&ax, anim_set_x);
        lv_anim_set_values(&ax, -VISITOR_W, SCREEN_W + VISITOR_W);
        lv_anim_set_duration(&ax, PARADE_WALK_MS);
        lv_anim_set_delay(&ax, i * PARADE_STAGGER_MS);
        lv_anim_set_path_cb(&ax, lv_anim_path_linear);
        lv_anim_start(&ax);

        lv_anim_t ay;
        lv_anim_init(&ay);
        lv_anim_set_var(&ay, o);
        lv_anim_set_exec_cb(&ay, anim_set_y);
        lv_anim_set_values(&ay, y - 3, y + 3);
        lv_anim_set_duration(&ay, 400);
        lv_anim_set_playback_duration(&ay, 400);
        lv_anim_set_delay(&ay, i * PARADE_STAGGER_MS);
        lv_anim_set_repeat_count(&ay, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_in_out);
        lv_anim_start(&ay);
    }
    bsp_display_unlock();
    return (VISITOR_POOL_N - 1) * PARADE_STAGGER_MS + PARADE_WALK_MS;
}

void visitor_parade_reset(void)
{
    bsp_display_lock(0);
    for (int i = 0; i <= VISITOR_RAINBOW; i++) {
        lv_obj_t *o = s_body[i];
        lv_anim_delete(o, anim_set_x);
        lv_anim_delete(o, anim_set_y);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
    s_stage_id = -1;
}
