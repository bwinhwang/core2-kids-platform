// bucket —— 木桶/入桶动画/放生派对实现(SPEC.md §6.4)
#include "bucket.h"

#include <math.h>
#include <stdlib.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"

#include "feedback.h"
#include "fish.h"
#include "tuning.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define PARTY_N  6

#define BUCKET_CX (BUCKET_X + 72 / 2)
#define BUCKET_CY (BUCKET_Y + 64 / 2)

typedef enum { BK_IDLE = 0, BK_SPLASH, BK_FLIGHT, BK_PARTY } bk_state_t;

static bk_state_t s_state = BK_IDLE;
static int s_state_ms;
static int s_count;

static fish_species_t s_catch_species;
static int s_from_x, s_from_y;

static lv_obj_t *s_body, *s_rim, *s_tail_poke;
static lv_obj_t *s_dots[BUCKET_FULL_N];
static lv_obj_t *s_flight;

static lv_obj_t *s_party_dot[PARTY_N];
static int s_party_tx[PARTY_N], s_party_ty[PARTY_N];

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static void hue2rgb(int h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h = ((h % 360) + 360) % 360;
    int x = 255 - abs((h % 120) * 255 / 60 - 255);
    if (x < 0) x = 0;
    if      (h < 60)  { *r = 255; *g = x;   *b = 0;   }
    else if (h < 120) { *r = x;   *g = 255; *b = 0;   }
    else if (h < 180) { *r = 0;   *g = 255; *b = x;   }
    else if (h < 240) { *r = 0;   *g = x;   *b = 255; }
    else if (h < 300) { *r = x;   *g = 0;   *b = 255; }
    else              { *r = 255; *g = 0;   *b = x;   }
}

static void redraw_bucket_visual(void)
{
    // 0 条 = 空;1~FULL-1 条 = 有鱼(探出鱼尾);FULL 条 = 鼓胀(桶变形)
    if (s_count <= 0) {
        lv_obj_add_flag(s_tail_poke, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_body, 72, 64);
        lv_obj_set_pos(s_body, BUCKET_X, BUCKET_Y);
    } else if (s_count < BUCKET_FULL_N) {
        lv_obj_remove_flag(s_tail_poke, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_body, 72, 64);
        lv_obj_set_pos(s_body, BUCKET_X, BUCKET_Y);
    } else {
        lv_obj_remove_flag(s_tail_poke, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_body, 82, 70);   // 鼓胀:桶变形,喜剧感
        lv_obj_set_pos(s_body, BUCKET_X - 5, BUCKET_Y - 4);
    }
    for (int i = 0; i < BUCKET_FULL_N; i++) {
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(i < s_count ? COL_DOT_FILLED : COL_DOT_EMPTY), 0);
    }
}

void bucket_create(lv_obj_t *scr)
{
    bsp_display_lock(0);
    s_body = fpond_box(scr, 72, 64, COL_BUCKET, 10);
    lv_obj_set_pos(s_body, BUCKET_X, BUCKET_Y);
    s_rim = fpond_box(scr, 72, 10, COL_BUCKET_RIM, 6);
    lv_obj_set_pos(s_rim, BUCKET_X, BUCKET_Y);
    s_tail_poke = fpond_box(scr, 14, 16, COL_FAT_BODY, 6);
    lv_obj_set_pos(s_tail_poke, BUCKET_X + 72 / 2 - 7, BUCKET_Y - 8);
    lv_obj_add_flag(s_tail_poke, LV_OBJ_FLAG_HIDDEN);

    int dot_d = 10, gap = 5;
    int total_w = BUCKET_FULL_N * dot_d + (BUCKET_FULL_N - 1) * gap;
    int start_x = BUCKET_X + (72 - total_w) / 2;
    for (int i = 0; i < BUCKET_FULL_N; i++) {
        s_dots[i] = fpond_box(scr, dot_d, dot_d, COL_DOT_EMPTY, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s_dots[i], start_x + i * (dot_d + gap), BUCKET_Y + 68);
    }

    s_flight = fpond_box(scr, 40, 28, COL_FAT_BODY, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(s_flight, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < PARTY_N; i++) {
        s_party_dot[i] = fpond_box(scr, 10, 10, COL_FAT_BODY, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(s_party_dot[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_count = 0;
    s_state = BK_IDLE;
    redraw_bucket_visual();
    bsp_display_unlock();
}

bool bucket_is_busy(void) { return s_state != BK_IDLE; }

void bucket_catch_start(fish_species_t species, int from_x, int from_y)
{
    if (s_state != BK_IDLE) return;   // fish.c 已按 bucket_is_busy() 门控,双保险
    s_catch_species = species;
    s_from_x = from_x;
    s_from_y = from_y;
    s_state = BK_SPLASH;
    s_state_ms = 0;
}

static void enter_flight(void)
{
    s_state = BK_FLIGHT;
    s_state_ms = 0;
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_flight,
        lv_color_hex(s_catch_species == FISH_SPECIES_LAZY ? COL_LAZY_BODY : COL_FAT_BODY), 0);
    lv_obj_set_pos(s_flight, s_from_x - 20, s_from_y - 14);
    lv_obj_remove_flag(s_flight, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

static void enter_party(void)
{
    s_state = BK_PARTY;
    s_state_ms = 0;
    feedback_party();
    bsp_display_lock(0);
    for (int i = 0; i < PARTY_N; i++) {
        s_party_tx[i] = 16 + (int)(esp_random() % (SCREEN_W - 32));
        s_party_ty[i] = WATERLINE_Y + 10 + (int)(esp_random() % (SCREEN_H - WATERLINE_Y - 30));
        lv_obj_set_pos(s_party_dot[i], BUCKET_CX - 5, BUCKET_CY - 5);
        lv_obj_remove_flag(s_party_dot[i], LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

static void tick_splash(int dt_ms)
{
    s_state_ms += dt_ms;
    if (s_state_ms >= SPLASH_HOLD_MS) enter_flight();
}

static void tick_flight(int dt_ms)
{
    s_state_ms += dt_ms;
    float t = clampf((float)s_state_ms / CATCH_FLIGHT_MS, 0.f, 1.f);
    float x = lerpf((float)s_from_x, (float)BUCKET_CX, t);
    float y = lerpf((float)s_from_y, (float)BUCKET_CY, t) - 40.0f * sinf((float)M_PI * t);   // 抛物线弧

    bsp_display_lock(0);
    lv_obj_set_pos(s_flight, (int)x - 20, (int)y - 14);
    bsp_display_unlock();

    if (t >= 1.0f) {
        s_count++;
        bsp_display_lock(0);
        lv_obj_add_flag(s_flight, LV_OBJ_FLAG_HIDDEN);
        redraw_bucket_visual();
        bsp_display_unlock();
        feedback_bucket_add();
        if (s_count >= BUCKET_FULL_N) enter_party();
        else s_state = BK_IDLE;
    }
}

static void tick_party(int dt_ms)
{
    s_state_ms += dt_ms;
    float t = clampf((float)s_state_ms / PARTY_HOLD_MS, 0.f, 1.f);

    bsp_display_lock(0);
    for (int i = 0; i < PARTY_N; i++) {
        float x = lerpf((float)BUCKET_CX, (float)s_party_tx[i], t);
        float y = lerpf((float)BUCKET_CY, (float)s_party_ty[i], t)
                  - 10.0f * sinf((float)s_state_ms * 0.006f + i * 1.1f);
        lv_obj_set_pos(s_party_dot[i], (int)x - 5, (int)y - 5);
        uint8_t r, g, b;
        hue2rgb((s_state_ms * 6 + i * 60) % 360, &r, &g, &b);
        lv_obj_set_style_bg_color(s_party_dot[i], lv_color_make(r, g, b), 0);
    }
    bsp_display_unlock();

    if (t >= 1.0f) {
        bsp_display_lock(0);
        for (int i = 0; i < PARTY_N; i++) lv_obj_add_flag(s_party_dot[i], LV_OBJ_FLAG_HIDDEN);
        s_count = 0;
        redraw_bucket_visual();
        bsp_display_unlock();
        s_state = BK_IDLE;
        fish_round_setup();   // 放生派对结束 → 新回合(SPEC §5)
    }
}

void bucket_tick(int dt_ms)
{
    switch (s_state) {
    case BK_SPLASH: tick_splash(dt_ms); break;
    case BK_FLIGHT: tick_flight(dt_ms); break;
    case BK_PARTY:  tick_party(dt_ms);  break;
    default: break;
    }
}
