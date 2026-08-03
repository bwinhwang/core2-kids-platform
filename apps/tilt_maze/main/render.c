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
static lv_obj_t *s_hazard[MAZE_HAZARDS];   // 巡逻怪(该槽位无则 NULL,§14.2026-07-27)

static float s_squash;   // 撞墙挤扁脉冲(0~1),逐帧衰减

// 取消按关卡换皮的多主题配色(原 4 套色相互相冲突、部分与球色几乎同色相/同亮度、
// 肉眼会"融"进地板),统一用单一配色。2026-07-27 改用 meadow 草地色系(球色相 ~46°,
// 地板色相 ~88°,相差 42°——够区分,但不如冷色系(~158°)分得开,肉眼需留意)。
static const uint32_t k_floor_color  = 0xD7ECBF;   // 路面:浅草绿
static const uint32_t k_wall_color   = 0x7FB069;   // 墙/底色:草地绿
static const uint32_t k_home_color   = 0xC68A52;   // 家:鸟窝棕
// 2026-07-27 放弃零失败:踩陷阱/撞怪的泛光色(危险红)。陷阱格与巡逻怪本体已改为
// 程序化烘的尖刺精灵(见上 bake_hazard/trap),不再用纯色块——原温和配色幼儿看不出"要躲"。
static const uint32_t k_hazard_color = 0xD63A2A;   // 失败泛光:危险红

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

// ── 危险物精灵:巡逻怪(红尖刺球+怒眉)/ 陷阱(暗坑+苍白骨刺)────────────────
// 2026-07-27 重画:原"暖珊瑚圆球+萌圆眼 / 暗紫平瓦片"太温和,幼儿看不出"要躲"。
// 尖刺 = 一眼认得的"会痛";红=危险、暗坑=会掉下去。与星星同法程序化烘 ARGB8888,
// 之后当图片贴:陷阱静态贴一次;巡逻怪每帧只挪 20×20 脏矩形,alpha 混合远在 §6.2 预算内。
#define HZ_IMG 20
#define TP_IMG 20
static uint8_t        s_hazard_img[HZ_IMG * HZ_IMG * 4];
static lv_image_dsc_t s_hazard_dsc;
static uint8_t        s_trap_img[TP_IMG * TP_IMG * 4];
static lv_image_dsc_t s_trap_dsc;

// n 角尖刺星多边形(2n 顶点):偶点在外圈 r_out(尖),奇点在内圈 r_in(谷)
static void spike_verts(float cx, float cy, float r_out, float r_in, int n, float rot,
                        float *vx, float *vy)
{
    for (int i = 0; i < 2 * n; i++) {
        float ang = rot + i * (float)M_PI / n;
        float r = (i & 1) ? r_in : r_out;
        vx[i] = cx + r * cosf(ang);
        vy[i] = cy + r * sinf(ang);
    }
}

static bool in_poly_n(float x, float y, const float *vx, const float *vy, int n)
{
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if ((vy[i] > y) != (vy[j] > y) &&
            x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i]) {
            in = !in;
        }
    }
    return in;
}

static void img_desc(lv_image_dsc_t *d, uint8_t *buf, int w, int h)
{
    d->header.magic  = LV_IMAGE_HEADER_MAGIC;
    d->header.cf     = LV_COLOR_FORMAT_ARGB8888;
    d->header.w      = w;
    d->header.h      = h;
    d->header.stride = w * 4;
    d->data_size     = w * h * 4;
    d->data          = buf;
}

// 往精灵缓冲画一个不透明实心块(点睛/描眉用;字节序 B,G,R,A)
static void paint_rect(uint8_t *buf, int w, int x0, int y0, int rw, int rh, const uint8_t *bgr)
{
    for (int y = y0; y < y0 + rh; y++) {
        for (int x = x0; x < x0 + rw; x++) {
            if (x < 0 || y < 0 || x >= w || y >= w) continue;
            uint8_t *o = &buf[(y * w + x) * 4];
            o[0] = bgr[0]; o[1] = bgr[1]; o[2] = bgr[2]; o[3] = 255;
        }
    }
}

// 巡逻怪:8 尖刺红球 + 下压怒眉 + 内视小凶眼(会动的"怪")
static void bake_hazard_sprite(void)
{
    const float cx = HZ_IMG / 2.0f, cy = HZ_IMG / 2.0f;
    const float rout = 9.6f, rin = 5.8f;
    float vx[16], vy[16];
    spike_verts(cx, cy, rout, rin, 8, -(float)M_PI / 2, vx, vy);
    const uint8_t spike[3] = { 0x1C, 0x22, 0xAE };   // 深红尖刺
    const uint8_t body [3] = { 0x2E, 0x3C, 0xEA };   // 亮红身体
    const uint8_t hi   [3] = { 0x62, 0x74, 0xF4 };   // 中心高光

    for (int y = 0; y < HZ_IMG; y++) {
        for (int x = 0; x < HZ_IMG; x++) {
            int cov = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    if (in_poly_n(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4, vx, vy, 16)) cov++;
                }
            }
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            const uint8_t *c = (d < rin * 0.45f) ? hi : (d < rin) ? body : spike;
            uint8_t *o = &s_hazard_img[(y * HZ_IMG + x) * 4];
            o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
            o[3] = (uint8_t)(cov * 255 / 16);
        }
    }
    // 怒相:下压浓眉(斜向中心)+ 小凶眼(眼白压小免得变萌,黑瞳偏内下=瞪着你)
    const uint8_t brow [3] = { 0x12, 0x14, 0x36 };
    const uint8_t white[3] = { 0xEC, 0xF0, 0xFF };
    const uint8_t pup  [3] = { 0x1A, 0x18, 0x22 };
    paint_rect(s_hazard_img, HZ_IMG, 6, 9, 2, 2, white);
    paint_rect(s_hazard_img, HZ_IMG, 12, 9, 2, 2, white);
    paint_rect(s_hazard_img, HZ_IMG, 7, 10, 1, 1, pup);
    paint_rect(s_hazard_img, HZ_IMG, 12, 10, 1, 1, pup);
    paint_rect(s_hazard_img, HZ_IMG, 4, 7, 2, 1, brow);    // 左眉 \ 外高内低
    paint_rect(s_hazard_img, HZ_IMG, 6, 8, 2, 1, brow);
    paint_rect(s_hazard_img, HZ_IMG, 14, 7, 2, 1, brow);   // 右眉 / 外高内低
    paint_rect(s_hazard_img, HZ_IMG, 12, 8, 2, 1, brow);

    img_desc(&s_hazard_dsc, s_hazard_img, HZ_IMG, HZ_IMG);
}

// 陷阱:国际"禁止"标记 —— 红圈 + ↘斜杠,盖在冷调近白底盘上(静止,幼儿一眼认得"不许去")
static void bake_trap_sprite(void)
{
    const float cx = TP_IMG / 2.0f, cy = TP_IMG / 2.0f;
    const float r_out   = 9.3f;   // 标记外缘(=轮廓,圆外透明露地板)
    const float r_in    = 6.5f;   // 红圈内缘 → 圈厚 ~2.8
    const float barhalf = 1.75f;  // ↘斜杠半宽(全宽 ~3.5)
    const uint8_t red [3] = { 0x2A, 0x3A, 0xD6 };   // 危险红 #D63A2A(B,G,R)
    const uint8_t face[3] = { 0xEE, 0xF2, 0xF6 };   // 标记底盘:冷调近白(衬托红)

    for (int y = 0; y < TP_IMG; y++) {
        for (int x = 0; x < TP_IMG; x++) {
            int cov = 0, red_cov = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float sxp = x + (sx + 0.5f) / 4 - cx;
                    float syp = y + (sy + 0.5f) / 4 - cy;
                    if (sxp * sxp + syp * syp > r_out * r_out) continue;  // 圆外 = 透明
                    cov++;
                    bool on_ring = sxp * sxp + syp * syp >= r_in * r_in;
                    bool on_bar  = fabsf(syp - sxp) * 0.70710678f <= barhalf;  // ↘ 斜杠
                    if (on_ring || on_bar) red_cov++;
                }
            }
            uint8_t *o = &s_trap_img[(y * TP_IMG + x) * 4];
            if (!cov) { o[0] = o[1] = o[2] = o[3] = 0; continue; }   // 露地板
            float f = (float)red_cov / cov;                          // 红占比 → 混红/底盘
            o[0] = (uint8_t)(red[0] * f + face[0] * (1 - f));
            o[1] = (uint8_t)(red[1] * f + face[1] * (1 - f));
            o[2] = (uint8_t)(red[2] * f + face[2] * (1 - f));
            o[3] = (uint8_t)(cov * 255 / 16);                        // 外缘抗锯齿
        }
    }
    img_desc(&s_trap_dsc, s_trap_img, TP_IMG, TP_IMG);
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
    bake_star_sprite();     // 纯 CPU,一次性,无需持锁
    bake_hazard_sprite();   // 巡逻怪红尖刺球
    bake_trap_sprite();     // 陷阱禁止标记(红圈斜杠)

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
    bsp_display_lock(0);

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(k_wall_color), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

    lv_obj_clean(s_maze);
    s_stars[0] = s_stars[1] = NULL;
    for (int i = 0; i < MAZE_HAZARDS; i++) s_hazard[i] = NULL;

    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            if (maze_is_wall(lvl, col, row)) continue;
            make_box(s_maze, col * CELL_PX, row * CELL_PX, CELL_PX, CELL_PX, k_floor_color, 4);
        }
    }

    // 陷阱:暗坑骨刺精灵盖在地板上(静态,进关贴一次)
    if (lvl->trap.col >= 0) {
        lv_obj_t *tr = lv_image_create(s_maze);
        lv_image_set_src(tr, &s_trap_dsc);
        vec2_t tc = maze_cell_center(lvl->trap);
        lv_obj_set_pos(tr, (int)(tc.x - TP_IMG / 2), (int)(tc.y - TP_IMG / 2));
    }

    // 家:暖色圆盘 + 持续脉动(直径 2×GOAL_R,略盖过 20px 家格,像陷进窝里)
    vec2_t h = maze_cell_center(lvl->home);
    s_home = make_box(s_maze, (int)(h.x - GOAL_R), (int)(h.y - GOAL_R),
                      (int)(GOAL_R * 2), (int)(GOAL_R * 2), k_home_color, 999);
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

    // 巡逻怪(本关有几只建几只):红尖刺球精灵,不随行进方向翻转(尖刺球本就各向同性)
    for (int i = 0; i < lvl->n_hazards && i < MAZE_HAZARDS; i++) {
        if (lvl->hazards[i].n_pts < 2) continue;
        s_hazard[i] = lv_image_create(s_maze);
        lv_image_set_src(s_hazard[i], &s_hazard_dsc);
        vec2_t ha = maze_cell_center(lvl->hazards[i].pts[0]);
        render_hazard_update(i, ha.x, ha.y);
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

void render_hint_stars(void)
{
    bsp_display_lock(0);
    for (int i = 0; i < 2; i++) {
        if (!s_stars[i]) continue;   // 已收的星是 NULL,只闪还没收的那些
        lv_anim_t a;                 // 放大脉冲闪几下,把幼儿的眼睛引到"还得去拿的星"
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_stars[i]);
        lv_anim_set_exec_cb(&a, cb_scale);
        lv_anim_set_values(&a, LV_SCALE_NONE, 330);
        lv_anim_set_duration(&a, 200);
        lv_anim_set_reverse_duration(&a, 200);
        lv_anim_set_repeat_count(&a, 3);
        lv_anim_start(&a);           // 同 var+cb 覆盖旧动画;3 个来回后停在原大小
    }
    bsp_display_unlock();
}

void render_hazard_update(int idx, float cx, float cy)
{
    if (idx < 0 || idx >= MAZE_HAZARDS || !s_hazard[idx]) return;
    bsp_display_lock(0);
    lv_obj_set_pos(s_hazard[idx], (int)(cx - HZ_IMG / 2), (int)(cy - HZ_IMG / 2));
    bsp_display_unlock();
}

void render_fail_flash(float cx, float cy)
{
    bsp_display_lock(0);
    // 挂在 s_scr(不挂 s_maze):trigger_fail 之后紧接着 render_load_level 会
    // lv_obj_clean(s_maze) 重置本关,若挂 s_maze 这个特效会被瞬间删掉(参考
    // render_win_celebrate 同样挂 s_scr 的理由)。
    lv_obj_t *f = make_box(s_scr, (int)(cx - 22), (int)(cy - 22), 44, 44, k_hazard_color, 10);
    lv_obj_set_style_opa(f, 170, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, f);
    lv_anim_set_exec_cb(&a, cb_opa);
    lv_anim_set_values(&a, 170, 0);
    lv_anim_set_duration(&a, 320);
    lv_anim_set_completed_cb(&a, cb_delete);
    lv_anim_start(&a);
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
