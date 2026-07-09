#include "wizard.h"

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"

#include "tuning.h"

// ── 几何(静态层进场景画一次;精灵基准矩形供缩放特效复用,见 set_part_scale)──────
typedef struct { int x, y, w, h; } rect_t;

static const rect_t ROBE_R = { 124, 104, 72, 86 };
static const rect_t HEAD_R = { 136, 58, 48, 48 };
static const rect_t HATB_R = { 128, 40, 64, 14 };
static const rect_t HATC_R = { 147, 14, 26, 30 };

#define WIZ_CX  160   // 魔法师视觉中心 x(粒子环绕特效基准)
#define WIZ_CY  110   // 魔法师视觉中心 y

#define COLOR_BG      0x1B1130
#define COLOR_FLOOR   0x3A2550
#define COLOR_RUG     0x2E1C48
#define COLOR_STAR    0xFFE9A0
#define COLOR_ROBE    0x6A3FA0
#define COLOR_HEAD    0xFFD9A8
#define COLOR_HAT     0x4A2A78
#define COLOR_EYE     0x201020
#define COLOR_HATSTAR 0xFFE060

static lv_obj_t *s_robe, *s_head, *s_arm;

#define PARTICLE_COUNT  6
static lv_obj_t *s_particles[PARTICLE_COUNT];
static int        s_particle_next;

static lv_obj_t *s_stem, *s_flower;   // 长高咒专属(脚边豆茎+花)

// ── 小工具 ───────────────────────────────────────────────────────────────
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

static void anim_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, v); }
static void anim_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }
static void anim_h(void *o, int32_t v) { lv_obj_set_height((lv_obj_t *)o, v); }
// 专供 fire_after 当"非阻塞计时器"用的哑 exec_cb——不能借用 anim_x/anim_y,否则会和
// 同一粒子身上真正在跑的位移 tween 抢同一个 (var,exec_cb) 而互相打架(某一帧谁后算
// 谁赢,视觉表现为抖动/瞬移回起点)。
static void anim_noop(void *o, int32_t v) { (void)o; (void)v; }

static void hide_cb(lv_anim_t *a) { lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN); }
static void show_cb(lv_anim_t *a) { lv_obj_remove_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN); }

static void tween(lv_obj_t *o, lv_anim_exec_xcb_t exec, int32_t from, int32_t to,
                   uint32_t dur, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

// 定时"打一枪"(零位移 anim 当非阻塞计时器,全平台唯一非阻塞延时原语):
// delay 后调 cb,不改变 o 的当前状态本身(cb 里才真正动手)。
static void fire_after(lv_obj_t *o, uint32_t delay_ms, lv_anim_completed_cb_t cb)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, anim_noop);
    lv_anim_set_values(&a, 0, 0);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_duration(&a, 1);
    lv_anim_set_completed_cb(&a, cb);
    lv_anim_start(&a);
}

static void hide_after(lv_obj_t *o, uint32_t ms) { fire_after(o, ms, hide_cb); }
static void show_after(lv_obj_t *o, uint32_t ms) { fire_after(o, ms, show_cb); }

// 把 obj 缩到 base 矩形的 permille(千分比)大小,保持同一中心点(SPEC.md §2 红线:
// 体积变化走 set_size/set_pos 改尺寸,不做真旋转/alpha)。
static void set_part_scale(lv_obj_t *o, rect_t base, int32_t permille)
{
    int w = base.w * permille / 1000;
    int h = base.h * permille / 1000;
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    int cx = base.x + base.w / 2;
    int cy = base.y + base.h / 2;
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, cx - w / 2, cy - h / 2);
}

static void anim_scale_robe(void *o, int32_t permille) { set_part_scale((lv_obj_t *)o, ROBE_R, permille); }
static void anim_scale_head(void *o, int32_t permille) { set_part_scale((lv_obj_t *)o, HEAD_R, permille); }

// 挤压假旋转:只变宽,中心 x 不动(见 SPEC.md §2 红线:不做真旋转)。
static void anim_squeeze_w(void *o, int32_t w)
{
    if (w < 2) w = 2;
    int cx = ROBE_R.x + ROBE_R.w / 2;
    lv_obj_set_width((lv_obj_t *)o, w);
    lv_obj_set_x((lv_obj_t *)o, cx - w / 2);
}

// 一次性掐掉魔法师身体上所有可能在跑的特效动画(不同法术复用同一批 obj,不同 exec_cb)。
static void kill_body_anims(void)
{
    lv_anim_delete(s_robe, anim_squeeze_w);
    lv_anim_delete(s_robe, anim_scale_robe);
    lv_anim_delete(s_head, anim_scale_head);
    lv_anim_delete(s_arm, anim_x);
}

// 施法后把身体强制归位(避免打断残留在非 100%/非中心态)。
static void snap_body_idle(void)
{
    set_part_scale(s_robe, ROBE_R, 1000);
    set_part_scale(s_head, HEAD_R, 1000);
    lv_obj_set_pos(s_arm, ROBE_R.x + ROBE_R.w - 8, ROBE_R.y + 10);
}

// 8 方位查表(顺时针,索引 0=正上方起)——固定表,非逐帧三角函数(SPEC.md §5 旋风咒备注)。
static const int16_t OCT_DX[8] = {   0,  22,  30,  22,   0, -22, -30, -22 };
static const int16_t OCT_DY[8] = { -30, -22,   0,  22,  30,  22,   0, -22 };

static void anim_whirl_pos(void *o, int32_t step)
{
    int i = (int)step % 8;
    if (i < 0) i += 8;
    lv_obj_set_pos((lv_obj_t *)o, WIZ_CX + OCT_DX[i] - 4, WIZ_CY + OCT_DY[i] - 4);
}

static lv_obj_t *next_particle(void)
{
    lv_obj_t *p = s_particles[s_particle_next];
    s_particle_next = (s_particle_next + 1) % PARTICLE_COUNT;
    lv_anim_delete(p, NULL);   // 掐掉这颗粒子身上任何残留动画(位移/查表步进/计时器)
    return p;
}

static void show_at(lv_obj_t *o, int x, int y, uint32_t color)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// ── 场景创建 ─────────────────────────────────────────────────────────────
// 注:本文件所有对外 API(wizard_scene_create/wizard_cast/…)各自内部完整包住
// bsp_display_lock/unlock,调用方(magic_wand.c 的 game_task)不必也不应再包一层
// (bsp_display_lock 非重入,双重加锁会死锁)。
void wizard_scene_create(lv_obj_t *parent)
{
    bsp_display_lock(0);
    plain(parent, SCREEN_W, SCREEN_H, COLOR_BG, 0);
    lv_obj_t *floor = plain(parent, SCREEN_W, 44, COLOR_FLOOR, 0);
    lv_obj_set_pos(floor, 0, SCREEN_H - 44);
    lv_obj_t *rug = plain(parent, 110, 26, COLOR_RUG, 13);
    lv_obj_set_pos(rug, WIZ_CX - 55, 192);

    // 静态星星(一次性画,零逐帧开销)
    static const int16_t stars[][2] = {
        {20,20},{60,10},{100,30},{40,50},{260,18},{290,40},{240,60},{10,70},{300,80},{270,100}
    };
    for (size_t i = 0; i < sizeof(stars) / sizeof(stars[0]); i++) {
        lv_obj_t *s = plain(parent, 3, 3, COLOR_STAR, 1);
        lv_obj_set_pos(s, stars[i][0], stars[i][1]);
    }

    s_robe = plain(parent, ROBE_R.w, ROBE_R.h, COLOR_ROBE, 24);
    lv_obj_set_pos(s_robe, ROBE_R.x, ROBE_R.y);
    lv_obj_t *hat_brim = plain(parent, HATB_R.w, HATB_R.h, COLOR_HAT, 7);
    lv_obj_set_pos(hat_brim, HATB_R.x, HATB_R.y);
    lv_obj_t *hat_cone = plain(parent, HATC_R.w, HATC_R.h, COLOR_HAT, 8);
    lv_obj_set_pos(hat_cone, HATC_R.x, HATC_R.y);
    s_head = plain(parent, HEAD_R.w, HEAD_R.h, COLOR_HEAD, 24);
    lv_obj_set_pos(s_head, HEAD_R.x, HEAD_R.y);
    lv_obj_t *eye_l = plain(parent, 6, 6, COLOR_EYE, 3);
    lv_obj_set_pos(eye_l, HEAD_R.x + 14, HEAD_R.y + 20);
    lv_obj_t *eye_r = plain(parent, 6, 6, COLOR_EYE, 3);
    lv_obj_set_pos(eye_r, HEAD_R.x + 32, HEAD_R.y + 20);
    lv_obj_t *hat_star = plain(parent, 8, 8, COLOR_HATSTAR, 2);
    lv_obj_set_pos(hat_star, HATC_R.x + HATC_R.w / 2 - 4, HATC_R.y - 6);

    s_arm = plain(parent, 12, 22, COLOR_ROBE, 6);
    lv_obj_set_pos(s_arm, ROBE_R.x + ROBE_R.w - 8, ROBE_R.y + 10);

    s_stem = plain(parent, 6, 1, 0x3FA050, 2);
    lv_obj_set_pos(s_stem, WIZ_CX - 30, 191);
    lv_obj_add_flag(s_stem, LV_OBJ_FLAG_HIDDEN);
    s_flower = plain(parent, 10, 10, 0xFF80C0, 5);
    lv_obj_set_pos(s_flower, WIZ_CX - 33, 170);
    lv_obj_add_flag(s_flower, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < PARTICLE_COUNT; i++) {
        lv_obj_t *p = plain(parent, 8, 8, COLOR_STAR, 2);
        lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
        s_particles[i] = p;
    }
    bsp_display_unlock();
}

// ── 每个手势的完整法术动画 ───────────────────────────────────────────────

static void cast_grow(void)
{
    lv_anim_delete(s_stem, anim_h);
    lv_anim_delete(s_stem, anim_y);
    lv_obj_remove_flag(s_stem, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_stem, 1);
    lv_obj_set_y(s_stem, 191);
    // 3 级离散长高(每级 120ms,底边固定在 y=192,只是"瞬移到下一级"——非渐变)。
    lv_anim_t a;
    for (int lvl = 0; lvl < 3; lvl++) {
        int h = 8 * (lvl + 1);
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_stem);
        lv_anim_set_exec_cb(&a, anim_h);
        lv_anim_set_values(&a, h, h);
        lv_anim_set_delay(&a, lvl * 120);
        lv_anim_set_duration(&a, 1);
        lv_anim_start(&a);
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_stem);
        lv_anim_set_exec_cb(&a, anim_y);
        lv_anim_set_values(&a, 192 - h, 192 - h);
        lv_anim_set_delay(&a, lvl * 120);
        lv_anim_set_duration(&a, 1);
        lv_anim_start(&a);
    }
    lv_anim_delete(s_flower, NULL);
    lv_obj_add_flag(s_flower, LV_OBJ_FLAG_HIDDEN);
    show_after(s_flower, 360);   // 顶端花瞬时显示
}

static void cast_starrain(void)
{
    for (int i = 0; i < 5; i++) {
        lv_obj_t *p = next_particle();
        int x = WIZ_CX - 90 + i * 40 + (int)(esp_random() % 12);
        show_at(p, x, 16, 0xC8DCFF);
        tween(p, anim_y, 16, 188, 260, lv_anim_path_bounce);
        hide_after(p, 320);
    }
}

static void cast_squeeze(bool cloak_left)
{
    lv_anim_delete(s_robe, anim_squeeze_w);
    lv_anim_delete(s_robe, anim_scale_robe);
    lv_anim_t sq;
    lv_anim_init(&sq);
    lv_anim_set_var(&sq, s_robe);
    lv_anim_set_exec_cb(&sq, anim_squeeze_w);
    lv_anim_set_values(&sq, ROBE_R.w, ROBE_R.w * 4 / 10);
    lv_anim_set_duration(&sq, 100);
    lv_anim_set_playback_duration(&sq, 100);
    lv_anim_set_path_cb(&sq, lv_anim_path_ease_out);
    lv_anim_start(&sq);

    lv_obj_t *tip = next_particle();
    int dir = cloak_left ? -1 : 1;
    int x0 = ROBE_R.x + (cloak_left ? 4 : ROBE_R.w - 12);
    show_at(tip, x0, ROBE_R.y + ROBE_R.h - 16, COLOR_ROBE);
    tween(tip, anim_x, x0, x0 + dir * 16, 120, lv_anim_path_ease_out);
    hide_after(tip, 160);
}

static void cast_zoom(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_robe);
    lv_anim_set_exec_cb(&a, anim_scale_robe);
    lv_anim_set_values(&a, 1000, 1300);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_playback_duration(&a, 110);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, s_head);
    lv_anim_set_exec_cb(&b, anim_scale_head);
    lv_anim_set_values(&b, 1000, 1300);
    lv_anim_set_duration(&b, 90);
    lv_anim_set_playback_duration(&b, 110);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_start(&b);

    static const int16_t ray_dx[4] = { 34, -34, 34, -34 };
    static const int16_t ray_dy[4] = { -34, -34, 34, 34 };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *p = next_particle();
        show_at(p, WIZ_CX + ray_dx[i] - 4, WIZ_CY + ray_dy[i] - 4, 0xFFF4C0);
        hide_after(p, 150);
    }
}

static void cast_shrink_pop(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_robe);
    lv_anim_set_exec_cb(&a, anim_scale_robe);
    lv_anim_set_values(&a, 1000, 650);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_playback_duration(&a, 100);
    lv_anim_set_playback_delay(&a, PEEK_HOLD_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, s_head);
    lv_anim_set_exec_cb(&b, anim_scale_head);
    lv_anim_set_values(&b, 1000, 650);
    lv_anim_set_duration(&b, 120);
    lv_anim_set_playback_duration(&b, 100);
    lv_anim_set_playback_delay(&b, PEEK_HOLD_MS);
    lv_anim_set_path_cb(&b, lv_anim_path_overshoot);
    lv_anim_start(&b);
}

// 顺/逆时针各 8 步离散跳转(非连续三角函数逐帧算,查 OCT_DX/DY 表)——每步用一个
// 零区间+固定 delay 的 anim 直接摆到位,不靠 path_cb 插值(lv_anim_path_step 只在
// 动画结束瞬间跳一次,不适合"沿途每步都要停一下"的查表步进)。
static void cast_whirl(bool cw)
{
    for (int i = 0; i < 4; i++) {
        lv_obj_t *p = next_particle();
        int start_idx = i * 2;
        show_at(p, WIZ_CX + OCT_DX[start_idx % 8] - 4, WIZ_CY + OCT_DY[start_idx % 8] - 4, 0xC080FF);
        for (int step = 1; step <= 8; step++) {
            int idx = cw ? (start_idx + step) : (start_idx - step);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, p);
            lv_anim_set_exec_cb(&a, anim_whirl_pos);
            lv_anim_set_values(&a, idx, idx);
            lv_anim_set_delay(&a, step * WHIRL_STEP_MS);
            lv_anim_set_duration(&a, 1);
            lv_anim_start(&a);
        }
        hide_after(p, 8 * WHIRL_STEP_MS + 20);
    }
}

static void cast_wave(void)
{
    lv_anim_delete(s_arm, anim_x);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_arm);
    lv_anim_set_exec_cb(&a, anim_x);
    int base_x = ROBE_R.x + ROBE_R.w - 8;
    lv_anim_set_values(&a, base_x, base_x + 16);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_playback_duration(&a, 90);
    lv_anim_set_repeat_count(&a, 2);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    static const int16_t confetti_dx[6] = { -40, -24, -8, 8, 24, 40 };
    static const int16_t confetti_dy[6] = { -50, -60, -68, -68, -60, -50 };
    static const uint32_t confetti_col[6] = { 0xFF6B6B, 0xFFD166, 0x6BCB77, 0x4D96FF, 0xC77DFF, 0xFF9F1C };
    for (int i = 0; i < 6; i++) {
        lv_obj_t *p = next_particle();
        show_at(p, WIZ_CX - 4, HEAD_R.y - 4, confetti_col[i]);
        tween(p, anim_x, WIZ_CX - 4, WIZ_CX - 4 + confetti_dx[i], 320, lv_anim_path_ease_out);
        tween(p, anim_y, HEAD_R.y - 4, HEAD_R.y - 4 + confetti_dy[i], 320, lv_anim_path_ease_out);
        hide_after(p, 340);
    }
}

void wizard_cast(gesture_event_t g)
{
    // 打断上一个法术的残留动画,身体强制归位,新法术再从干净的基准态起播
    // (SPEC.md §2:动画打断先 lv_anim_delete 配对掐残留,再瞬移到终态)。
    bsp_display_lock(0);
    kill_body_anims();
    snap_body_idle();
    switch (g) {
        case GESTURE_UP:                cast_grow();        break;
        case GESTURE_DOWN:              cast_starrain();    break;
        case GESTURE_LEFT:              cast_squeeze(true);  break;
        case GESTURE_RIGHT:             cast_squeeze(false); break;
        case GESTURE_FORWARD:           cast_zoom();        break;
        case GESTURE_BACKWARD:          cast_shrink_pop();  break;
        case GESTURE_CLOCKWISE:         cast_whirl(true);   break;
        case GESTURE_COUNTER_CLOCKWISE: cast_whirl(false);  break;
        case GESTURE_WAVE:              cast_wave();        break;
        default: break;
    }
    bsp_display_unlock();
}

void wizard_cast_light(gesture_event_t g)
{
    // 冷却窗口内的重复触发:只给魔法师头部一个小弹跳,不重放整套大动画。
    (void)g;
    bsp_display_lock(0);
    lv_anim_delete(s_head, anim_scale_head);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_head);
    lv_anim_set_exec_cb(&a, anim_scale_head);
    lv_anim_set_values(&a, 1000, 1080);
    lv_anim_set_duration(&a, 60);
    lv_anim_set_playback_duration(&a, 60);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    bsp_display_unlock();
}

void wizard_shimmer(void)
{
    bsp_display_lock(0);
    lv_obj_t *p = next_particle();
    show_at(p, ROBE_R.x + ROBE_R.w + 4, HEAD_R.y + 30, COLOR_STAR);
    hide_after(p, 140);
    bsp_display_unlock();
}

void wizard_hidden_spell(void)
{
    static const int16_t dx[6] = { -50, -30, -10, 10, 30, 50 };
    static const int16_t dy[6] = { -70, -90, -100, -100, -90, -70 };
    static const uint32_t col[6] = { 0xFFD166, 0xFF6B6B, 0xC77DFF, 0x4D96FF, 0x6BCB77, 0xFFD166 };
    bsp_display_lock(0);
    for (int i = 0; i < 6; i++) {
        lv_obj_t *p = next_particle();
        show_at(p, WIZ_CX - 4, HEAD_R.y - 10, col[i]);
        tween(p, anim_x, WIZ_CX - 4, WIZ_CX - 4 + dx[i], 260, lv_anim_path_ease_out);
        tween(p, anim_y, HEAD_R.y - 10, HEAD_R.y - 10 + dy[i], 260, lv_anim_path_ease_out);
        hide_after(p, 280);
    }
    bsp_display_unlock();
}

// 注:直接调 wizard_cast(g),不额外加锁(避免与 wizard_cast 内部的锁重入死锁)。
void wizard_party_step(gesture_event_t g)
{
    wizard_cast(g);
}

void wizard_party_begin(void)
{
    bsp_display_lock(0);
    kill_body_anims();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_robe);
    lv_anim_set_exec_cb(&a, anim_scale_robe);
    lv_anim_set_values(&a, 1000, 1120);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_playback_duration(&a, 200);
    lv_anim_set_repeat_count(&a, 3);   // 开场欢跳:有限 3 跳,之后交给逐步法术回放接管
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    bsp_display_unlock();
}

void wizard_party_end(void)
{
    bsp_display_lock(0);
    kill_body_anims();
    snap_body_idle();
    bsp_display_unlock();
}
