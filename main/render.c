#include "render.h"
#include "tuning.h"

#include <math.h>
#include "bsp/m5stack_core_2.h"
#include "lvgl.h"
#include "esp_random.h"

#define CELL_PX  ((int)MAZE_CELL)
#define STAR_R   9

static lv_obj_t *s_scr;
static lv_obj_t *s_maze;
static lv_obj_t *s_ball;
static lv_obj_t *s_home;
static lv_obj_t *s_eye_l, *s_eye_r, *s_pupil_l, *s_pupil_r;
static lv_obj_t *s_stars[2];

static float s_squash;   // 撞墙挤扁脉冲(0~1),逐帧衰减

typedef struct { uint32_t floor, wall, home; } palette_t;

static palette_t world_palette(world_t w)
{
    switch (w) {
        case WORLD_MEADOW:  return (palette_t){ 0xD7ECBF, 0x7FB069, 0xC68A52 };
        case WORLD_SEASIDE: return (palette_t){ 0xF2E2B8, 0x4FB0D8, 0xE2B86A };
        case WORLD_STARRY:  return (palette_t){ 0xDCD9F0, 0x4A4A78, 0xF4EDC9 };
        case WORLD_CANDY:   return (palette_t){ 0xF0D9A8, 0x8A5A3C, 0xC98A4A };
    }
    return (palette_t){ 0xD7ECBF, 0x7FB069, 0xC68A52 };
}

static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// ── 动画回调 ─────────────────────────────────────────────────────────
static void cb_scale(void *o, int32_t v)
{
    lv_obj_set_style_transform_scale_x((lv_obj_t *)o, v, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t *)o, v, 0);
}
static void cb_opa(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
static void cb_y(void *o, int32_t v)   { lv_obj_set_y((lv_obj_t *)o, v); }
static void cb_delete(lv_anim_t *a)    { lv_obj_delete((lv_obj_t *)a->var); }

void render_init(void)
{
    bsp_display_lock(0);

    s_scr = lv_screen_active();
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_maze = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_maze);
    lv_obj_set_size(s_maze, (int)PLAY_W, (int)PLAY_H);
    lv_obj_set_pos(s_maze, 0, 0);
    lv_obj_remove_flag(s_maze, LV_OBJ_FLAG_SCROLLABLE);

    // 吉祥物「圆圆」:身体 + 两眼 + 瞳孔(眼随身体一起被变换缩放)
    s_ball = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_ball);
    lv_obj_set_size(s_ball, (int)(BALL_R * 2), (int)(BALL_R * 2));
    lv_obj_set_style_radius(s_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ball, lv_color_hex(0xFFD23F), 0);
    lv_obj_set_style_bg_opa(s_ball, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(s_ball, (int)BALL_R, 0);
    lv_obj_set_style_transform_pivot_y(s_ball, (int)BALL_R, 0);

    s_eye_l   = make_box(s_ball, 6,  7, 8, 8, 0xFFFFFF, 999);
    s_pupil_l = make_box(s_eye_l, 2, 2, 4, 4, 0x3A3A38, 999);
    s_eye_r   = make_box(s_ball, 14, 7, 8, 8, 0xFFFFFF, 999);
    s_pupil_r = make_box(s_eye_r, 2, 2, 4, 4, 0x3A3A38, 999);

    lv_obj_set_pos(s_ball, (int)(PLAY_W / 2 - BALL_R), (int)(PLAY_H / 2 - BALL_R));

    bsp_display_unlock();
}

void render_load_level(const level_t *lvl)
{
    palette_t p = world_palette(lvl->world);

    bsp_display_lock(0);

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(p.wall), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    lv_obj_clean(s_maze);
    s_stars[0] = s_stars[1] = NULL;

    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            if (maze_is_wall(lvl, col, row)) continue;
            make_box(s_maze, col * CELL_PX, row * CELL_PX, CELL_PX, CELL_PX, p.floor, 6);
        }
    }

    // 家:暖色圆盘 + 持续脉动
    vec2_t h = maze_cell_center(lvl->home);
    s_home = make_box(s_maze, (int)(h.x - 15), (int)(h.y - 15), 30, 30, p.home, 999);
    lv_obj_set_style_transform_pivot_x(s_home, 15, 0);
    lv_obj_set_style_transform_pivot_y(s_home, 15, 0);
    render_home_excited(false);

    // 星(收集物):统一金色五角(占位圆),记下对象供拾取动画
    for (int i = 0; i < lvl->n_stars && i < 2; i++) {
        vec2_t st = maze_cell_center(lvl->stars[i]);
        s_stars[i] = make_box(s_maze, (int)(st.x - STAR_R), (int)(st.y - STAR_R),
                              STAR_R * 2, STAR_R * 2, 0xFFD23F, 999);
        lv_obj_set_style_transform_pivot_x(s_stars[i], STAR_R, 0);
        lv_obj_set_style_transform_pivot_y(s_stars[i], STAR_R, 0);
    }

    vec2_t s = maze_cell_center(lvl->start);
    render_ball_set_pos(s.x, s.y);
    lv_obj_move_foreground(s_ball);

    bsp_display_unlock();
}

void render_show_splash(uint32_t bg_hex)
{
    bsp_display_lock(0);
    lv_obj_clean(s_maze);
    s_home = NULL;
    s_stars[0] = s_stars[1] = NULL;
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_move_foreground(s_ball);
    bsp_display_unlock();
}

// 复位球的形变与瞳孔(标题/校准用)
void render_ball_set_pos(float cx, float cy)
{
    if (!s_ball) return;
    bsp_display_lock(0);
    lv_obj_set_pos(s_ball, (int)(cx - BALL_R), (int)(cy - BALL_R));
    lv_obj_set_style_transform_scale_x(s_ball, LV_SCALE_NONE, 0);
    lv_obj_set_style_transform_scale_y(s_ball, LV_SCALE_NONE, 0);
    lv_obj_set_pos(s_pupil_l, 2, 2);
    lv_obj_set_pos(s_pupil_r, 2, 2);
    s_squash = 0;
    bsp_display_unlock();
}

// 游戏中:移动 + 眼睛朝向 + 速度拉伸 + 撞墙挤扁(统一在这里管缩放)
void render_ball_update(float cx, float cy, float vx, float vy)
{
    if (!s_ball) return;
    bsp_display_lock(0);

    lv_obj_set_pos(s_ball, (int)(cx - BALL_R), (int)(cy - BALL_R));

    float sp = sqrtf(vx * vx + vy * vy);

    // 瞳孔朝运动方向偏 ~2px("看着要去的方向",§18.4)
    float ox = 0, oy = 0;
    if (sp > 1.0f) { ox = vx / sp * 2.0f; oy = vy / sp * 2.0f; }
    lv_obj_set_pos(s_pupil_l, (int)(2 + ox), (int)(2 + oy));
    lv_obj_set_pos(s_pupil_r, (int)(2 + ox), (int)(2 + oy));

    // 速度方向拉伸 + 撞墙挤扁脉冲(纵压横展)
    float t = sp / VEL_MAX; if (t > 1) t = 1;
    float ex, ey;
    if (fabsf(vx) >= fabsf(vy)) { ex = 1 + 0.12f * t; ey = 1 - 0.06f * t; }
    else                        { ex = 1 - 0.06f * t; ey = 1 + 0.12f * t; }
    ex += 0.30f * s_squash;
    ey -= 0.30f * s_squash;
    lv_obj_set_style_transform_scale_x(s_ball, (int)(LV_SCALE_NONE * ex), 0);
    lv_obj_set_style_transform_scale_y(s_ball, (int)(LV_SCALE_NONE * ey), 0);

    s_squash *= 0.82f;
    if (s_squash < 0.02f) s_squash = 0;

    bsp_display_unlock();
}

void render_ball_squash(void)
{
    s_squash = 1.0f;   // 下一帧 render_ball_update 起效并衰减
}

void render_home_excited(bool fast)
{
    if (!s_home) return;
    bsp_display_lock(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_home);
    lv_anim_set_exec_cb(&a, cb_scale);
    lv_anim_set_values(&a, LV_SCALE_NONE, fast ? 205 : 228);
    lv_anim_set_duration(&a, fast ? 320 : 760);
    lv_anim_set_reverse_duration(&a, fast ? 320 : 760);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);     // 同 var+cb 会替换旧动画
    bsp_display_unlock();
}

void render_collect_star(int idx)
{
    if (idx < 0 || idx > 1 || !s_stars[idx]) return;
    bsp_display_lock(0);
    lv_obj_t *st = s_stars[idx];
    s_stars[idx] = NULL;

    lv_anim_t a;                       // 放大
    lv_anim_init(&a);
    lv_anim_set_var(&a, st);
    lv_anim_set_exec_cb(&a, cb_scale);
    lv_anim_set_values(&a, LV_SCALE_NONE, 430);
    lv_anim_set_duration(&a, 260);
    lv_anim_start(&a);

    lv_anim_t b;                       // 淡出 + 完成即删
    lv_anim_init(&b);
    lv_anim_set_var(&b, st);
    lv_anim_set_exec_cb(&b, cb_opa);
    lv_anim_set_values(&b, 255, 0);
    lv_anim_set_duration(&b, 260);
    lv_anim_set_completed_cb(&b, cb_delete);
    lv_anim_start(&b);
    bsp_display_unlock();
}

void render_wall_flash(float cx, float cy)
{
    bsp_display_lock(0);
    lv_obj_t *f = make_box(s_maze, (int)(cx - 16), (int)(cy - 16), 32, 32, 0xFFFFFF, 8);
    lv_obj_set_style_opa(f, 150, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, f);
    lv_anim_set_exec_cb(&a, cb_opa);
    lv_anim_set_values(&a, 150, 0);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_completed_cb(&a, cb_delete);
    lv_anim_start(&a);
    bsp_display_unlock();
}

void render_win_celebrate(void)
{
    static const uint32_t cols[5] = { 0xFF8FB0, 0xFFD23F, 0x7FD0C0, 0x9FD06A, 0xFF9E80 };
    bsp_display_lock(0);
    for (int i = 0; i < 10; i++) {
        int x  = 10 + (esp_random() % 300);
        int sz = 8 + (esp_random() % 8);
        lv_obj_t *d = make_box(s_scr, x, -12, sz, sz, cols[i % 5], 999);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, d);
        lv_anim_set_exec_cb(&a, cb_y);
        lv_anim_set_values(&a, -12, (int)PLAY_H + 12);
        lv_anim_set_duration(&a, 700 + (esp_random() % 500));
        lv_anim_set_delay(&a, i * 45);
        lv_anim_set_completed_cb(&a, cb_delete);
        lv_anim_start(&a);
    }
    bsp_display_unlock();
}
