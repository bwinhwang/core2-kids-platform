#include "render.h"
#include "tuning.h"

#include <math.h>
#include "bsp/m5stack_core_2.h"
#include "lvgl.h"
#include "esp_random.h"

#define CELL_PX  ((int)MAZE_CELL)

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

// ── 收集星精灵(五角星双色,SPEC §18.3)────────────────────────────────
// LVGL 无五角形基元,又不想打包素材:init 时程序化烘一张 20×20 ARGB8888 精灵
// (点内测试 + 4×4 超采样抗锯齿,一次性 CPU 开销),之后当普通图片贴,
// 守 §6.4「烘好再贴、不每帧算 alpha」。字节序 B,G,R,A(lv_color32_t)。
#define STAR_IMG_W   20
#define STAR_IMG_H   20
static uint8_t        s_star_px[STAR_IMG_W * STAR_IMG_H * 4];
static lv_image_dsc_t s_star_dsc;

// 点是否在五角星内(10 顶点偶交叉法;尖朝上)
static bool in_star(float x, float y, const float *vx, const float *vy)
{
    bool in = false;
    for (int i = 0, j = 9; i < 10; j = i++) {
        if ((vy[i] > y) != (vy[j] > y) &&
            x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i]) {
            in = !in;
        }
    }
    return in;
}

static void star_vertices(float cx, float cy, float r_out, float r_in, float *vx, float *vy)
{
    for (int i = 0; i < 10; i++) {
        float ang = -(float)M_PI / 2 + i * (float)M_PI / 5;
        float r = (i % 2 == 0) ? r_out : r_in;
        vx[i] = cx + r * cosf(ang);
        vy[i] = cy + r * sinf(ang);
    }
}

static void bake_star_sprite(void)
{
    const float cx = STAR_IMG_W / 2.0f;
    const float cy = STAR_IMG_H / 2.0f + 0.8f;   // 尖朝上重心偏上,下移做光学居中
    const float R = 9.2f;              // 外接半径:尖几乎顶满 20px 格
    const float r = R * 0.47f;         // 凹点半径偏大 → 胖乎乎的幼儿审美
    const uint8_t body[3] = { 0x2E, 0xCB, 0xFF };   // B,G,R:主体金
    const uint8_t core[3] = { 0xA0, 0xF0, 0xFF };   // B,G,R:中心小星高光(双色)
    float bx[10], by[10], kx[10], ky[10];
    star_vertices(cx, cy, R, r, bx, by);
    star_vertices(cx, cy + 0.5f, R * 0.52f, r * 0.52f, kx, ky);  // 高光星略小、微下沉

    for (int y = 0; y < STAR_IMG_H; y++) {
        for (int x = 0; x < STAR_IMG_W; x++) {
            int hit = 0, hit_core = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    if (in_star(px, py, bx, by)) {
                        hit++;
                        if (in_star(px, py, kx, ky)) hit_core++;
                    }
                }
            }
            uint8_t *o = &s_star_px[(y * STAR_IMG_W + x) * 4];
            float t = hit ? (float)hit_core / hit : 0;   // 主体→高光过渡
            for (int c = 0; c < 3; c++) {
                o[c] = (uint8_t)(body[c] + t * ((float)core[c] - body[c]));
            }
            o[3] = (uint8_t)(hit * 255 / 16);
        }
    }

    s_star_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_star_dsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
    s_star_dsc.header.w      = STAR_IMG_W;
    s_star_dsc.header.h      = STAR_IMG_H;
    s_star_dsc.header.stride = STAR_IMG_W * 4;
    s_star_dsc.data_size     = sizeof(s_star_px);
    s_star_dsc.data          = s_star_px;
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
    bake_star_sprite();   // 纯 CPU,一次性,无需持锁

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

    // 球体 14px(BALL_R=7),五官等比缩小
    s_eye_l   = make_box(s_ball, 1, 4, 5, 5, 0xFFFFFF, 999);
    s_pupil_l = make_box(s_eye_l, 1, 1, 3, 3, 0x3A3A38, 999);
    s_eye_r   = make_box(s_ball, 8, 4, 5, 5, 0xFFFFFF, 999);
    s_pupil_r = make_box(s_eye_r, 1, 1, 3, 3, 0x3A3A38, 999);

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
            make_box(s_maze, col * CELL_PX, row * CELL_PX, CELL_PX, CELL_PX, p.floor, 4);
        }
    }

    // 家:暖色圆盘 + 持续脉动(直径 2×GOAL_R,略盖过 20px 家格,像陷进窝里)
    vec2_t h = maze_cell_center(lvl->home);
    s_home = make_box(s_maze, (int)(h.x - GOAL_R), (int)(h.y - GOAL_R),
                      (int)(GOAL_R * 2), (int)(GOAL_R * 2), p.home, 999);
    lv_obj_set_style_transform_pivot_x(s_home, (int)GOAL_R, 0);
    lv_obj_set_style_transform_pivot_y(s_home, (int)GOAL_R, 0);
    render_home_excited(false);

    // 星(收集物):烘焙五角星双色精灵,所有世界统一;记下对象供拾取动画
    for (int i = 0; i < lvl->n_stars && i < 2; i++) {
        vec2_t st = maze_cell_center(lvl->stars[i]);
        s_stars[i] = lv_image_create(s_maze);
        lv_image_set_src(s_stars[i], &s_star_dsc);
        lv_obj_set_pos(s_stars[i], (int)(st.x - STAR_IMG_W / 2), (int)(st.y - STAR_IMG_H / 2));
        lv_obj_set_style_transform_pivot_x(s_stars[i], STAR_IMG_W / 2, 0);
        lv_obj_set_style_transform_pivot_y(s_stars[i], STAR_IMG_H / 2, 0);
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
    lv_obj_set_pos(s_pupil_l, 1, 1);
    lv_obj_set_pos(s_pupil_r, 1, 1);
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

    // 瞳孔朝运动方向偏 ~1px("看着要去的方向",§18.4;5px 眼配 3px 瞳)
    float ox = 0, oy = 0;
    if (sp > 1.0f) { ox = vx / sp * 1.0f; oy = vy / sp * 1.0f; }
    lv_obj_set_pos(s_pupil_l, (int)(1 + ox), (int)(1 + oy));
    lv_obj_set_pos(s_pupil_r, (int)(1 + ox), (int)(1 + oy));

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
