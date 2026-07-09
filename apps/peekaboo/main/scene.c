// 躲猫猫昼夜屋 v2 —— 场景层实现(见 scene.h)
#include "scene.h"

#include <math.h>
#include <string.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"

#include "tuning.h"

// ── 配色(§18 家族:大块扁平圆润 + 暖色;夜空用深蓝不用纯黑)───────────────────
#define DAY_SKY       0x8FCBE8
#define NIGHT_SKY     0x33335C
#define DUSK_SKY      0xE8A15C
#define DAY_GROUND    0xD7ECBF
#define NIGHT_GROUND  0x3E4468
#define DUSK_GROUND   0xB07A4A
#define SUN_COL       0xFFC75F
#define DUSK_SUN_COL  0xE8875C
#define MOON_COL      0xF4EDC9
#define STAR_COL      0xFFE89B
#define FIREFLY_COL   0xFFF3B0
#define GRASS_DAY_COL   0x9CC978
#define GRASS_NIGHT_COL 0x2E3454
#define DREAM_COL     0xDDE6F5
#define SHOOTING_COL  0xF4EDC9
#define BUTTERFLY_COL 0xB6D8F2
#define BODY_COL      0xFFD23F
#define CHEEK_COL     0xFFB3C6
#define BEAK_COL      0xFB8B24
#define EYE_WHITE     0xFFFFFF
#define EYE_PUPIL     0x3A3A38

static scene_kind_t s_kind = SCENE_DAY;

// ── LVGL 对象 ─────────────────────────────────────────────────────────
static lv_obj_t *s_sky;
static lv_obj_t *s_day_grp, *s_night_grp, *s_dusk_grp;
static lv_obj_t *s_grass_night_l, *s_grass_night_r;   // 夜草丛(rustle 目标)
static lv_obj_t *s_char;
static lv_obj_t *s_eyes_open, *s_eyes_shut, *s_eyes_squint;
static lv_obj_t *s_pupil_l, *s_pupil_r;
static lv_obj_t *s_dream;                              // 梦泡泡(单个,变大小)
static int       s_dream_tier;
static lv_obj_t *s_fireflies[FIREFLY_MAX];
static int       s_firefly_visible;
static lv_obj_t *s_stars[STAR_COUNT];
static lv_obj_t *s_shooting;
static lv_obj_t *s_butterflies[3];
static lv_obj_t *s_burst[BURST_COUNT];

// ── 小工具(纯 LVGL,调用方持锁)──────────────────────────────────────────
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

static lv_obj_t *layer(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void anim_set_x(void *o, int32_t v)   { lv_obj_set_x((lv_obj_t *)o, v); }
static void anim_set_y(void *o, int32_t v)   { lv_obj_set_y((lv_obj_t *)o, v); }
static void anim_set_opa(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

static void grass_tuft(lv_obj_t *parent, int x, uint32_t color)
{
    plain(parent, GRASS_W, GRASS_H * 6 / 10, color, GRASS_H / 3);
    lv_obj_t *b = lv_obj_get_child(parent, lv_obj_get_child_count(parent) - 1);
    lv_obj_set_pos(b, x, GRASS_Y + GRASS_H - GRASS_H * 6 / 10);
    lv_obj_t *b2 = plain(parent, GRASS_W * 7 / 10, GRASS_H * 8 / 10, color, GRASS_H / 3);
    lv_obj_set_pos(b2, x + GRASS_W * 3 / 20, GRASS_Y + GRASS_H - GRASS_H * 8 / 10);
}

// ── 昼/夜/黄昏静态层 ─────────────────────────────────────────────────────
static void make_day(lv_obj_t *parent)
{
    s_day_grp = layer(parent);
    lv_obj_t *ground = plain(s_day_grp, SCREEN_W, GROUND_H, DAY_GROUND, 0);
    lv_obj_set_pos(ground, 0, GROUND_Y);
    lv_obj_t *sun = plain(s_day_grp, 46, 46, SUN_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(sun, SCREEN_W - 62, 14);
    grass_tuft(s_day_grp, GRASS_L_X, GRASS_DAY_COL);
    grass_tuft(s_day_grp, GRASS_R_X, GRASS_DAY_COL);
}

static void make_night(lv_obj_t *parent)
{
    s_night_grp = layer(parent);
    lv_obj_t *ground = plain(s_night_grp, SCREEN_W, GROUND_H, NIGHT_GROUND, 0);
    lv_obj_set_pos(ground, 0, GROUND_Y);
    lv_obj_t *moon = plain(s_night_grp, 42, 42, MOON_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(moon, SCREEN_W - 60, 14);

    lv_obj_t *gl = layer(s_night_grp);
    grass_tuft(gl, GRASS_L_X, GRASS_NIGHT_COL);
    s_grass_night_l = gl;
    lv_obj_t *gr = layer(s_night_grp);
    grass_tuft(gr, GRASS_R_X, GRASS_NIGHT_COL);
    s_grass_night_r = gr;

    static const int spos[STAR_COUNT][2] = {
        { 40, 36 }, { 96, 18 }, { 150, 44 }, { 206, 26 }, { 258, 58 }, { 122, 70 },
    };
    for (int i = 0; i < STAR_COUNT; i++) {
        lv_obj_t *st = plain(s_night_grp, 8, 8, STAR_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(st, spos[i][0], spos[i][1]);
        s_stars[i] = st;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, st);
        lv_anim_set_exec_cb(&a, anim_set_opa);
        lv_anim_set_values(&a, 90, 255);
        lv_anim_set_duration(&a, 640 + i * 130);
        lv_anim_set_playback_duration(&a, 640 + i * 130);
        lv_anim_set_delay(&a, i * 150);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    // 萤火虫(预建 FIREFLY_MAX 只,起步只显示 FIREFLY_START 只;§4.3 累积)
    static const int fpos[FIREFLY_MAX][2] = {
        { 66, 150 }, { 190, 138 }, { 240, 168 }, { 128, 176 }, { 90, 128 }, { 220, 120 },
    };
    for (int i = 0; i < FIREFLY_MAX; i++) {
        lv_obj_t *ff = plain(s_night_grp, 9, 9, FIREFLY_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(ff, fpos[i][0], fpos[i][1]);
        lv_obj_add_flag(ff, LV_OBJ_FLAG_HIDDEN);
        s_fireflies[i] = ff;
        lv_anim_t ax;
        lv_anim_init(&ax);
        lv_anim_set_var(&ax, ff);
        lv_anim_set_exec_cb(&ax, anim_set_x);
        lv_anim_set_values(&ax, fpos[i][0] - 16, fpos[i][0] + 16);
        lv_anim_set_duration(&ax, 1700 + i * 240);
        lv_anim_set_playback_duration(&ax, 1700 + i * 240);
        lv_anim_set_repeat_count(&ax, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&ax, lv_anim_path_ease_in_out);
        lv_anim_start(&ax);
        lv_anim_t ay;
        lv_anim_init(&ay);
        lv_anim_set_var(&ay, ff);
        lv_anim_set_exec_cb(&ay, anim_set_y);
        lv_anim_set_values(&ay, fpos[i][1] - 10, fpos[i][1] + 10);
        lv_anim_set_duration(&ay, 1300 + i * 190);
        lv_anim_set_playback_duration(&ay, 1300 + i * 190);
        lv_anim_set_repeat_count(&ay, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_in_out);
        lv_anim_start(&ay);
    }
    s_firefly_visible = 0;

    // 梦泡泡(圆圆头顶,3 档离散长大;初始隐藏)
    s_dream = plain(s_night_grp, 10, 10, DREAM_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_dream, BODY_CX - 5, BODY_Y - 18);
    lv_obj_add_flag(s_dream, LV_OBJ_FLAG_HIDDEN);
    s_dream_tier = 0;

    // 流星(彩虹鸟之夜;初始隐藏,一次性 600ms 位移)
    s_shooting = plain(s_night_grp, 12, 4, SHOOTING_COL, 2);
    lv_obj_set_pos(s_shooting, -20, 40);
    lv_obj_add_flag(s_shooting, LV_OBJ_FLAG_HIDDEN);
}

static void make_dusk(lv_obj_t *parent)
{
    s_dusk_grp = layer(parent);
    lv_obj_t *ground = plain(s_dusk_grp, SCREEN_W, GROUND_H, DUSK_GROUND, 0);
    lv_obj_set_pos(ground, 0, GROUND_Y);
    lv_obj_t *sun = plain(s_dusk_grp, 46, 26, DUSK_SUN_COL, 10);   // 低垂半轮太阳
    lv_obj_set_pos(sun, SCREEN_W - 62, GROUND_Y - 30);
    grass_tuft(s_dusk_grp, GRASS_L_X, DUSK_GROUND);
    grass_tuft(s_dusk_grp, GRASS_R_X, DUSK_GROUND);
}

static void make_char(lv_obj_t *parent)
{
    lv_obj_t *body = plain(parent, BODY_D, BODY_D, BODY_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(body, BODY_X, BODY_Y);
    s_char = body;

    s_eyes_open = layer(body);
    lv_obj_set_size(s_eyes_open, BODY_D, BODY_D);
    lv_obj_t *el = plain(s_eyes_open, 26, 26, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(el, LV_ALIGN_TOP_MID, -22, 26);
    lv_obj_t *er = plain(s_eyes_open, 26, 26, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(er, LV_ALIGN_TOP_MID, 22, 26);
    s_pupil_l = plain(el, 12, 12, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_align(s_pupil_l, LV_ALIGN_CENTER, 0, 3);
    s_pupil_r = plain(er, 12, 12, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_align(s_pupil_r, LV_ALIGN_CENTER, 0, 3);

    s_eyes_shut = layer(body);
    lv_obj_set_size(s_eyes_shut, BODY_D, BODY_D);
    lv_obj_t *sl = plain(s_eyes_shut, 24, 6, EYE_PUPIL, 3);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, -22, 36);
    lv_obj_t *sr = plain(s_eyes_shut, 24, 6, EYE_PUPIL, 3);
    lv_obj_align(sr, LV_ALIGN_TOP_MID, 22, 36);
    lv_obj_add_flag(s_eyes_shut, LV_OBJ_FLAG_HIDDEN);

    // 眯眼(黄昏,睡眼线;比闭眼略短,视觉上区分"半睡")
    s_eyes_squint = layer(body);
    lv_obj_set_size(s_eyes_squint, BODY_D, BODY_D);
    lv_obj_t *ql = plain(s_eyes_squint, 18, 5, EYE_PUPIL, 3);
    lv_obj_align(ql, LV_ALIGN_TOP_MID, -22, 34);
    lv_obj_t *qr = plain(s_eyes_squint, 18, 5, EYE_PUPIL, 3);
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 22, 34);
    lv_obj_add_flag(s_eyes_squint, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *cl = plain(body, 18, 12, CHEEK_COL, LV_RADIUS_CIRCLE);
    lv_obj_align(cl, LV_ALIGN_TOP_MID, -34, 58);
    lv_obj_t *cr = plain(body, 18, 12, CHEEK_COL, LV_RADIUS_CIRCLE);
    lv_obj_align(cr, LV_ALIGN_TOP_MID, 34, 58);
    lv_obj_t *beak = plain(body, 14, 10, BEAK_COL, 3);
    lv_obj_align(beak, LV_ALIGN_TOP_MID, 0, 56);
}

// ── 建全部静态层 ─────────────────────────────────────────────────────────
void scene_create(lv_obj_t *scr)
{
    bsp_display_lock(0);

    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_sky = plain(scr, SCREEN_W, SCREEN_H, DAY_SKY, 0);
    lv_obj_set_pos(s_sky, 0, 0);

    make_day(scr);
    make_night(scr);
    make_dusk(scr);
    lv_obj_add_flag(s_night_grp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dusk_grp, LV_OBJ_FLAG_HIDDEN);

    make_char(scr);

    // 长夜蝴蝶(预建 3 只,初始隐藏,一次性从圆圆头顶飘出)
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = plain(scr, 10, 8, BUTTERFLY_COL, 3);
        lv_obj_set_pos(b, BODY_CX - 5 + i * 6, BODY_Y - 10);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        s_butterflies[i] = b;
    }

    s_kind = SCENE_DAY;
    bsp_display_unlock();
}

scene_kind_t scene_current(void) { return s_kind; }

void scene_apply(scene_kind_t kind)
{
    if (kind == s_kind) return;
    s_kind = kind;

    bsp_display_lock(0);
    uint32_t sky = kind == SCENE_DAY ? DAY_SKY : kind == SCENE_NIGHT ? NIGHT_SKY : DUSK_SKY;
    lv_obj_set_style_bg_color(s_sky, lv_color_hex(sky), 0);

    if (kind == SCENE_DAY) {
        lv_obj_remove_flag(s_day_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_night_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dusk_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_eyes_open, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_shut, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_squint, LV_OBJ_FLAG_HIDDEN);
    } else if (kind == SCENE_NIGHT) {
        lv_obj_add_flag(s_day_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_night_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_dusk_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_open, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_eyes_shut, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_squint, LV_OBJ_FLAG_HIDDEN);
    } else {   // SCENE_DUSK
        lv_obj_add_flag(s_day_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_night_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_dusk_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_open, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_shut, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_eyes_squint, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

void scene_char_bounce(int lift, int up_ms, int down_ms)
{
    bsp_display_lock(0);
    lv_anim_delete(s_char, anim_set_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_char);
    lv_anim_set_exec_cb(&a, anim_set_y);
    lv_anim_set_values(&a, BODY_Y, BODY_Y - lift);
    lv_anim_set_duration(&a, up_ms);
    lv_anim_set_playback_duration(&a, down_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
    bsp_display_unlock();
}

void scene_char_gaze(int dx)
{
    bsp_display_lock(0);
    lv_obj_align(s_pupil_l, LV_ALIGN_CENTER, dx, 3);
    lv_obj_align(s_pupil_r, LV_ALIGN_CENTER, dx, 3);
    bsp_display_unlock();
}

void scene_night_ambience_reset(void)
{
    bsp_display_lock(0);
    s_dream_tier = 0;
    lv_obj_add_flag(s_dream, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_dream, 10, 10);
    lv_obj_set_pos(s_dream, BODY_CX - 5, BODY_Y - 18);
    for (int i = 0; i < FIREFLY_MAX; i++) {
        if (i < FIREFLY_START) lv_obj_remove_flag(s_fireflies[i], LV_OBJ_FLAG_HIDDEN);
        else                   lv_obj_add_flag(s_fireflies[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_firefly_visible = FIREFLY_START;
    bsp_display_unlock();
}

void scene_night_ambience_tick(int night_elapsed_ms)
{
    int tier = night_elapsed_ms / (DREAM_STEP_S * 1000);
    if (tier > 3) tier = 3;
    if (tier != s_dream_tier) {
        s_dream_tier = tier;
        bsp_display_lock(0);
        if (tier == 0) {
            lv_obj_add_flag(s_dream, LV_OBJ_FLAG_HIDDEN);
        } else {
            static const int sizes[4] = { 0, 10, 16, 22 };
            int sz = sizes[tier];
            lv_obj_remove_flag(s_dream, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(s_dream, sz, sz);
            lv_obj_set_pos(s_dream, BODY_CX - sz / 2, BODY_Y - 12 - sz);
        }
        bsp_display_unlock();
    }

    int want_fireflies = FIREFLY_START + night_elapsed_ms / (FIREFLY_ADD_S * 1000);
    if (want_fireflies > FIREFLY_MAX) want_fireflies = FIREFLY_MAX;
    if (want_fireflies != s_firefly_visible) {
        bsp_display_lock(0);
        for (int i = 0; i < FIREFLY_MAX; i++) {
            if (i < want_fireflies) lv_obj_remove_flag(s_fireflies[i], LV_OBJ_FLAG_HIDDEN);
            else                    lv_obj_add_flag(s_fireflies[i], LV_OBJ_FLAG_HIDDEN);
        }
        bsp_display_unlock();
        s_firefly_visible = want_fireflies;
    }
}

static void rustle_completed_cb(lv_anim_t *a)
{
    // 往返摆动结束后可能停在偏移端(视 repeat 奇偶而定),强制归零避免容器留下 ±2px 残留漂移
    lv_obj_t *o = (lv_obj_t *)lv_anim_get_user_data(a);
    lv_obj_set_x(o, 0);
}

void scene_grass_rustle(int side)
{
    lv_obj_t *g = side == 0 ? s_grass_night_l : s_grass_night_r;
    if (!g) return;
    int x0 = 0;
    bsp_display_lock(0);
    lv_anim_delete(g, anim_set_x);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g);
    lv_anim_set_exec_cb(&a, anim_set_x);
    lv_anim_set_user_data(&a, g);
    lv_anim_set_values(&a, x0 - 2, x0 + 2);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_playback_duration(&a, 90);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_set_completed_cb(&a, rustle_completed_cb);
    lv_anim_start(&a);
    bsp_display_unlock();
}

void scene_shooting_star_trigger(void)
{
    bsp_display_lock(0);
    lv_anim_delete(s_shooting, anim_set_x);
    lv_anim_delete(s_shooting, anim_set_y);
    lv_obj_set_pos(s_shooting, -20, 30 + (int)(esp_random() % 40));
    lv_obj_remove_flag(s_shooting, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, s_shooting);
    lv_anim_set_exec_cb(&ax, anim_set_x);
    lv_anim_set_values(&ax, -20, SCREEN_W + 20);
    lv_anim_set_duration(&ax, 600);
    lv_anim_set_path_cb(&ax, lv_anim_path_linear);
    lv_anim_start(&ax);

    lv_anim_t ay;
    lv_anim_init(&ay);
    lv_anim_set_var(&ay, s_shooting);
    lv_anim_set_exec_cb(&ay, anim_set_y);
    lv_anim_set_values(&ay, lv_obj_get_y(s_shooting), lv_obj_get_y(s_shooting) + 30);
    lv_anim_set_duration(&ay, 600);
    lv_anim_set_path_cb(&ay, lv_anim_path_linear);
    lv_anim_start(&ay);
    bsp_display_unlock();
}

static void butterfly_completed_cb(lv_anim_t *a)
{
    lv_obj_t *o = (lv_obj_t *)a->var;
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void scene_dream_butterflies_trigger(void)
{
    bsp_display_lock(0);
    lv_obj_add_flag(s_dream, LV_OBJ_FLAG_HIDDEN);   // "啵"消失
    s_dream_tier = 0;

    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = s_butterflies[i];
        lv_anim_delete(b, anim_set_x);
        lv_anim_delete(b, anim_set_y);
        lv_obj_set_pos(b, BODY_CX - 5 + i * 6, BODY_Y - 10);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);

        lv_anim_t ay;
        lv_anim_init(&ay);
        lv_anim_set_var(&ay, b);
        lv_anim_set_exec_cb(&ay, anim_set_y);
        lv_anim_set_values(&ay, BODY_Y - 10, -20);
        lv_anim_set_duration(&ay, 1200);
        lv_anim_set_delay(&ay, i * 120);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
        lv_anim_set_completed_cb(&ay, butterfly_completed_cb);
        lv_anim_start(&ay);

        lv_anim_t ax;
        lv_anim_init(&ax);
        lv_anim_set_var(&ax, b);
        lv_anim_set_exec_cb(&ax, anim_set_x);
        lv_anim_set_values(&ax, BODY_CX - 5 + i * 6, BODY_CX - 5 + i * 6 + (i - 1) * 22);
        lv_anim_set_duration(&ax, 1200);
        lv_anim_set_delay(&ax, i * 120);
        lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
        lv_anim_start(&ax);
    }
    bsp_display_unlock();
}

static void burst_completed_cb(lv_anim_t *a)
{
    lv_obj_t *o = (lv_obj_t *)a->var;
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void scene_burst(uint32_t color, int count)
{
    if (count > BURST_COUNT) count = BURST_COUNT;
    bsp_display_lock(0);
    for (int i = 0; i < count; i++) {
        lv_obj_t *s = s_burst[i];
        if (!s) {
            s = plain(lv_screen_active(), 12, 12, color, LV_RADIUS_CIRCLE);
            s_burst[i] = s;
        } else {
            lv_anim_delete(s, anim_set_x);
            lv_anim_delete(s, anim_set_y);
            lv_obj_set_style_bg_color(s, lv_color_hex(color), 0);
        }
        lv_obj_set_pos(s, BODY_CX - 6, BODY_CY - 6);
        lv_obj_remove_flag(s, LV_OBJ_FLAG_HIDDEN);
        int ang = i * (360 / count);
        int dx = (int)(96 * cosf(ang * 3.14159f / 180));
        int dy = (int)(70 * sinf(ang * 3.14159f / 180));
        lv_anim_t ax;
        lv_anim_init(&ax);
        lv_anim_set_var(&ax, s);
        lv_anim_set_exec_cb(&ax, anim_set_x);
        lv_anim_set_values(&ax, BODY_CX - 6, BODY_CX - 6 + dx);
        lv_anim_set_duration(&ax, 520);
        lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
        lv_anim_start(&ax);
        lv_anim_t ay;
        lv_anim_init(&ay);
        lv_anim_set_var(&ay, s);
        lv_anim_set_exec_cb(&ay, anim_set_y);
        lv_anim_set_values(&ay, BODY_CY - 6, BODY_CY - 6 + dy);
        lv_anim_set_duration(&ay, 520);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
        lv_anim_set_completed_cb(&ay, burst_completed_cb);
        lv_anim_start(&ay);
    }
    bsp_display_unlock();
}
