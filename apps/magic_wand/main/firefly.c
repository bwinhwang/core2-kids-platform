// 魔法萤火虫 v2.1 —— 萤火虫精灵实现(见 firefly.h)。
//
// 渲染纪律(CLAUDE.md §6):萤火虫是"动态层",每帧只动它自己这几个小对象(16×16
// 晕块 + 8×8 亮核 + 3×6×6 尾迹 ≈ 428px/帧,SPEC §11 预算内);全部不透明色块,
// 尾迹的"渐隐"是对夜空底色(0x1B1B3D)预混的 3 级暗黄色常量,零 alpha 成本
// (§6.4)。唯一用到 bg_opa 动画的是"慢眨眼"呼吸(16×16 小区域、≤~2fps 档,
// 与 peekaboo scene.c 的星星呼吸同一手法,精度成本一致)。
//
// v2.1 取消了 v2 的"光标连续跟手"(实机否决,见 SPEC §0),改为:
//   · 盘旋舞:8 方位查表(固定坐标,非逐帧三角函数)绕屏幕中心离散步进,
//     由 magic_wand.c 每帧调用 firefly_dance_advance() 推进(权威位置计算,
//     不触屏);CW/CCW 手势复用同一张 8 方位表做"快速整圈"。
//   · 九手势翻滚:位移/缩放的瞬时叠加层(s_tumble_dx/dy/s_scale),渲染时叠加在
//     盘旋权威位置之上,与盘旋各自独立、互不干扰——翻滚播完自动回到盘旋当前位置。
#include "firefly.h"

#include <math.h>

#include "bsp/m5stack_core_2.h"

#include "garden.h"   // SCREEN_W/H
#include "tuning.h"   // DANCE_STEP_MS_L1/2/3 / TUMBLE_MS / HOME_FLY_MS

// ── 外观常量(P1 补充,不在 SPEC §10 tuning.h 列表——纯视觉细节,不影响手感调参)──
#define FIREFLY_HALO_SZ     16
#define FIREFLY_CORE_SZ      8
#define FIREFLY_TRAIL_SZ     6
#define FIREFLY_TRAIL_COUNT  3
#define FIREFLY_TRAIL_LAG    4    // 每颗尾迹点相隔的帧数(30Hz 下 ≈133ms)
#define FIREFLY_HIST_LEN    16    // 环形缓冲长度,须 > TRAIL_COUNT*TRAIL_LAG

#define FIREFLY_HALO_COL  0xFFE9A0   // 暖黄晕块
#define FIREFLY_CORE_COL  0xFFF6D0   // 近白亮核

// 尾迹预混色:对夜空底色 0x1B1B3D 与晕块暖黄 0xFFE9A0 按权重(0.55/0.32/0.15)
// 线性混合算出的固定十六进制值(零 alpha 成本,§6.4)。离头越远权重越低越暗。
static const uint32_t s_trail_col[FIREFLY_TRAIL_COUNT] = { 0x988C73, 0x645D5D, 0x3D3A4C };

#define BLINK_HOME_MS   1400   // SEEK 态:睡着的慢呼吸

// 盘旋几何(P1 补充,不在 SPEC §10 列表——纯视觉细节):贴"玻璃"(屏幕中央偏上,
// 避开地面/月亮)绕圈,半径两档(远=小圈/中近=大圈,SPEC §5.1)。
#define DANCE_CENTER_X   (SCREEN_W / 2)
#define DANCE_CENTER_Y   104
#define DANCE_RADIUS_FAR   26
#define DANCE_RADIUS_NEAR  48

// 翻滚几何(P1 补充,不在 SPEC §10 列表——纯视觉细节)。
#define TUMBLE_OFFSET_PX      34   // 方向翻滚(上下左右)位移量
#define FORWARD_SCALE_DELTA  0.5f  // 凑近放大比例
#define BACKWARD_SCALE_DELTA 0.35f // 缩小比例
#define WAVE_OFFSET_PX        18   // 欢摆摆幅
#define PI_F  3.14159265f

// 8 方位查表(固定坐标,SPEC §5.1"非逐帧三角函数"):idx0=正上,顺时针步进。
static const float OCT_DX[8] = {  0.00f,  0.71f,  1.00f,  0.71f,  0.00f, -0.71f, -1.00f, -0.71f };
static const float OCT_DY[8] = { -1.00f, -0.71f,  0.00f,  0.71f,  1.00f,  0.71f,  0.00f, -0.71f };

// ── 对象 ─────────────────────────────────────────────────────────────────
static lv_obj_t *s_halo;
static lv_obj_t *s_core;
static lv_obj_t *s_trail[FIREFLY_TRAIL_COUNT];

// ── 权威位置(浮点像素;盘旋轨道的当前位置,不含翻滚叠加)──────────────────
static float s_disp_x, s_disp_y;

static int s_home_x, s_home_y;

// 尾迹历史环形缓冲(记录"渲染出来的最终位置",即含翻滚叠加,视觉上更连贯)
static float s_hist_x[FIREFLY_HIST_LEN];
static float s_hist_y[FIREFLY_HIST_LEN];
static int   s_hist_head;

// ── 盘旋状态机(§5.1)──────────────────────────────────────────────────────
static bool     s_dance_active;
static int      s_orbit_idx;
static float    s_step_from_x, s_step_from_y, s_step_to_x, s_step_to_y;
static uint32_t s_step_elapsed_ms, s_step_dur_ms;
static bool     s_loop_active;      // CW/CCW 筋斗云(快速整圈)进行中
static int      s_loop_dir;         // +1 顺时针 / -1 逆时针
static int      s_loop_remaining;   // 剩余步数(整圈 = 8)
static int      s_cur_level = 1;    // 最近一次 firefly_dance_advance() 收到的强度档
                                     // (筋斗云半径沿用它,不在触发瞬间跳档,§5.1)

// ── 翻滚叠加层(位移 dx/dy + 缩放 scale,渲染时叠加在盘旋权威位置之上)─────────
typedef enum { TKIND_NONE = 0, TKIND_OFFSET, TKIND_SCALE, TKIND_WAVE } tumble_kind_t;
static tumble_kind_t s_tkind;
static float s_tumble_dx, s_tumble_dy;
static float s_scale = 1.0f;
static float s_tumble_dirx, s_tumble_diry;   // TKIND_OFFSET 用:方向单位向量
static float s_tumble_scale_delta;           // TKIND_SCALE 用:缩放增量(正=放大)
static int   s_tumble_anim_var;              // 仅作 lv_anim var/delete 配对用的哑对象

// 回家动画
static bool  s_going_home;
static bool  s_home_done;
static float s_home_from_x, s_home_from_y;
static int   s_home_anim_var;   // 仅作 lv_anim var/delete 配对用的哑对象

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

static void anim_set_opa(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

// ── 慢眨眼(呼吸式:halo 的 bg_opa 在 110~255 间来回,同 peekaboo 星星手法)──────
static void start_blink(int period_ms)
{
    bsp_display_lock(0);
    lv_anim_delete(s_halo, anim_set_opa);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_halo);
    lv_anim_set_exec_cb(&a, anim_set_opa);
    lv_anim_set_values(&a, 110, 255);
    lv_anim_set_duration(&a, period_ms);
    lv_anim_set_playback_duration(&a, period_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    bsp_display_unlock();
}

static void stop_blink(void)
{
    bsp_display_lock(0);
    lv_anim_delete(s_halo, anim_set_opa);
    lv_obj_set_style_bg_opa(s_halo, LV_OPA_COVER, 0);
    bsp_display_unlock();
}

// ── 盘旋:8 方位查表几何 ────────────────────────────────────────────────────
static float dance_radius(int level) { return (level <= 1) ? DANCE_RADIUS_FAR : DANCE_RADIUS_NEAR; }

static uint32_t dance_step_ms(int level)
{
    switch (level) {
        case 1:  return DANCE_STEP_MS_L1;
        case 2:  return DANCE_STEP_MS_L2;
        default: return DANCE_STEP_MS_L3;
    }
}

static void orbit_point(int idx, float radius, float *out_x, float *out_y)
{
    *out_x = DANCE_CENTER_X + OCT_DX[idx] * radius;
    *out_y = DANCE_CENTER_Y + OCT_DY[idx] * radius;
}

// ── 翻滚叠加层动画 ───────────────────────────────────────────────────────
static void tumble_anim_completed_cb(lv_anim_t *a)
{
    (void)a;
    s_tumble_dx = 0;
    s_tumble_dy = 0;
    s_scale     = 1.0f;
    s_tkind     = TKIND_NONE;
}

static void tumble_anim_exec(void *var, int32_t v)
{
    (void)var;
    switch (s_tkind) {
    case TKIND_OFFSET: {
        float t = v / 1000.0f;
        s_tumble_dx = s_tumble_dirx * t * TUMBLE_OFFSET_PX;
        s_tumble_dy = s_tumble_diry * t * TUMBLE_OFFSET_PX;
        break;
    }
    case TKIND_SCALE: {
        float t = v / 1000.0f;
        s_scale = 1.0f + s_tumble_scale_delta * t;
        break;
    }
    case TKIND_WAVE: {
        // v: 0..1000 映射到两个完整摆动周期的相位,首尾均落在 sin=0(平滑起止)。
        float phase = (v / 1000.0f) * 2.0f * (2.0f * PI_F);
        s_tumble_dx = sinf(phase) * WAVE_OFFSET_PX;
        break;
    }
    default:
        break;
    }
}

/** @brief 掐掉任何正在播的翻滚叠加(方向/缩放/欢摆之一)+ 停掉筋斗云整圈,叠加量
 *         归零回到盘旋轨道本位——SPEC §4.2/§6"不同手势随时打断"的落地点,所有
 *         firefly_tumble_*() 入口统一先调它,调用方(magic_wand.c)不需要关心。 */
static void tumble_cancel(void)
{
    bsp_display_lock(0);
    lv_anim_delete(&s_tumble_anim_var, tumble_anim_exec);
    bsp_display_unlock();
    s_tumble_dx = 0;
    s_tumble_dy = 0;
    s_scale     = 1.0f;
    s_tkind     = TKIND_NONE;
    s_loop_active = false;
}

static void start_offset_tumble(float dirx, float diry)
{
    s_tkind = TKIND_OFFSET;
    s_tumble_dirx = dirx;
    s_tumble_diry = diry;

    bsp_display_lock(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_tumble_anim_var);
    lv_anim_set_exec_cb(&a, tumble_anim_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, TUMBLE_MS / 2);
    lv_anim_set_playback_duration(&a, TUMBLE_MS / 2);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, tumble_anim_completed_cb);
    lv_anim_start(&a);
    bsp_display_unlock();
}

static void start_scale_tumble(float delta, bool with_pause)
{
    s_tkind = TKIND_SCALE;
    s_tumble_scale_delta = delta;

    bsp_display_lock(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_tumble_anim_var);
    lv_anim_set_exec_cb(&a, tumble_anim_exec);
    lv_anim_set_values(&a, 0, 1000);
    uint32_t leg = (uint32_t)(TUMBLE_MS * 2 / 5);   // ~88ms(TUMBLE_MS=220 时)
    lv_anim_set_duration(&a, leg);
    lv_anim_set_playback_duration(&a, leg);
    if (with_pause) lv_anim_set_playback_delay(&a, TUMBLE_MS / 5);   // "顿"(躲猫手感)
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, tumble_anim_completed_cb);
    lv_anim_start(&a);
    bsp_display_unlock();
}

static void start_wave_tumble(void)
{
    s_tkind = TKIND_WAVE;

    bsp_display_lock(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_tumble_anim_var);
    lv_anim_set_exec_cb(&a, tumble_anim_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, TUMBLE_MS * 2);   // 两个完整摆动周期,~440ms
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, tumble_anim_completed_cb);
    lv_anim_start(&a);
    bsp_display_unlock();
}

static void start_loop(int dir, int level)
{
    s_loop_active    = true;
    s_loop_dir       = dir;
    s_loop_remaining = 8;   // 一整圈(SPEC §4.2:CW/CCW 筋斗云一整圈,8 方位查表)

    // 从当前视觉位置(可能仍在两个盘旋点之间)平滑起步,不瞬移。
    s_step_from_x = s_disp_x;
    s_step_from_y = s_disp_y;
    float r = dance_radius(level);
    orbit_point(s_orbit_idx, r, &s_step_to_x, &s_step_to_y);   // 先合拢到当前位所在的整数点
    s_step_elapsed_ms = 0;
    s_step_dur_ms     = DANCE_STEP_MS_L3;   // 筋斗云沿用最快步速(SPEC §4.2)
}

// ── 回家动画(单个 0..1000 进度驱动,exec 只更新权威位置、不碰 LVGL 对象)────────
static void home_anim_exec(void *var, int32_t v)
{
    (void)var;
    float t = v / 1000.0f;
    s_disp_x = s_home_from_x + (s_home_x - s_home_from_x) * t;
    s_disp_y = s_home_from_y + (s_home_y - s_home_from_y) * t;
}

static void home_anim_completed_cb(lv_anim_t *a)
{
    (void)a;
    s_disp_x = s_home_x;
    s_disp_y = s_home_y;
    s_going_home = false;
    s_home_done  = true;
    start_blink(BLINK_HOME_MS);
}

// ── 公开 API ─────────────────────────────────────────────────────────────
void firefly_create(lv_obj_t *parent, int home_x, int home_y)
{
    s_home_x = home_x;
    s_home_y = home_y;
    s_disp_x = home_x;
    s_disp_y = home_y;
    s_going_home  = false;
    s_home_done   = true;
    s_dance_active = false;
    s_loop_active  = false;
    s_tkind        = TKIND_NONE;
    s_tumble_dx = s_tumble_dy = 0;
    s_scale        = 1.0f;

    for (int i = 0; i < FIREFLY_HIST_LEN; i++) {
        s_hist_x[i] = home_x;
        s_hist_y[i] = home_y;
    }
    s_hist_head = 0;

    bsp_display_lock(0);
    // 先建尾迹(最底层),再 halo,再 core(最上层)——LVGL 兄弟对象后建者居顶。
    for (int i = 0; i < FIREFLY_TRAIL_COUNT; i++) {
        lv_obj_t *t = plain(parent, FIREFLY_TRAIL_SZ, FIREFLY_TRAIL_SZ, s_trail_col[i], LV_RADIUS_CIRCLE);
        lv_obj_set_pos(t, home_x - FIREFLY_TRAIL_SZ / 2, home_y - FIREFLY_TRAIL_SZ / 2);
        s_trail[i] = t;
    }
    s_halo = plain(parent, FIREFLY_HALO_SZ, FIREFLY_HALO_SZ, FIREFLY_HALO_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_halo, home_x - FIREFLY_HALO_SZ / 2, home_y - FIREFLY_HALO_SZ / 2);
    s_core = plain(parent, FIREFLY_CORE_SZ, FIREFLY_CORE_SZ, FIREFLY_CORE_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_core, home_x - FIREFLY_CORE_SZ / 2, home_y - FIREFLY_CORE_SZ / 2);
    bsp_display_unlock();

    start_blink(BLINK_HOME_MS);
}

void firefly_tick(void)
{
    float rx = s_disp_x + s_tumble_dx;
    float ry = s_disp_y + s_tumble_dy;

    s_hist_head = (s_hist_head + 1) % FIREFLY_HIST_LEN;
    s_hist_x[s_hist_head] = rx;
    s_hist_y[s_hist_head] = ry;

    int halo_sz = (int)(FIREFLY_HALO_SZ * s_scale + 0.5f);
    int core_sz = (int)(FIREFLY_CORE_SZ * s_scale + 0.5f);
    if (halo_sz < 2) halo_sz = 2;
    if (core_sz < 2) core_sz = 2;

    bsp_display_lock(0);
    lv_obj_set_size(s_halo, halo_sz, halo_sz);
    lv_obj_set_size(s_core, core_sz, core_sz);
    lv_obj_set_pos(s_halo, (int)rx - halo_sz / 2, (int)ry - halo_sz / 2);
    lv_obj_set_pos(s_core, (int)rx - core_sz / 2, (int)ry - core_sz / 2);
    for (int i = 0; i < FIREFLY_TRAIL_COUNT; i++) {
        int lag = (i + 1) * FIREFLY_TRAIL_LAG;
        int idx = ((s_hist_head - lag) % FIREFLY_HIST_LEN + FIREFLY_HIST_LEN) % FIREFLY_HIST_LEN;
        lv_obj_set_pos(s_trail[i], (int)s_hist_x[idx] - FIREFLY_TRAIL_SZ / 2,
                                    (int)s_hist_y[idx] - FIREFLY_TRAIL_SZ / 2);
    }
    bsp_display_unlock();
}

void firefly_enter_dance(void)
{
    stop_blink();
    tumble_cancel();

    s_dance_active = true;
    s_orbit_idx    = 0;   // 起跳位置:正上方
    float r = dance_radius(1);
    s_step_from_x = s_disp_x;
    s_step_from_y = s_disp_y;
    orbit_point(s_orbit_idx, r, &s_step_to_x, &s_step_to_y);
    s_step_elapsed_ms = 0;
    s_step_dur_ms     = dance_step_ms(1);
}

void firefly_dance_advance(uint32_t dt_ms, int level)
{
    if (!s_dance_active) return;
    s_cur_level = level;

    s_step_elapsed_ms += dt_ms;
    while (s_step_dur_ms > 0 && s_step_elapsed_ms >= s_step_dur_ms) {
        s_step_elapsed_ms -= s_step_dur_ms;

        int dir = s_loop_active ? s_loop_dir : 1;   // 常态盘旋固定顺时针步进
        s_orbit_idx = ((s_orbit_idx + dir) % 8 + 8) % 8;

        float r = dance_radius(level);
        s_step_from_x = s_step_to_x;
        s_step_from_y = s_step_to_y;
        orbit_point(s_orbit_idx, r, &s_step_to_x, &s_step_to_y);

        if (s_loop_active) {
            s_step_dur_ms = DANCE_STEP_MS_L3;
            if (--s_loop_remaining <= 0) s_loop_active = false;
        } else {
            s_step_dur_ms = dance_step_ms(level);
        }
    }

    float t = s_step_dur_ms ? (float)s_step_elapsed_ms / (float)s_step_dur_ms : 1.0f;
    if (t > 1.0f) t = 1.0f;
    s_disp_x = s_step_from_x + (s_step_to_x - s_step_from_x) * t;
    s_disp_y = s_step_from_y + (s_step_to_y - s_step_from_y) * t;
}

void firefly_tumble_left(void)     { tumble_cancel(); start_offset_tumble(-1.0f, 0.0f); }
void firefly_tumble_right(void)    { tumble_cancel(); start_offset_tumble(1.0f, 0.0f); }
void firefly_tumble_up(void)       { tumble_cancel(); start_offset_tumble(0.0f, -1.0f); }
void firefly_tumble_down(void)     { tumble_cancel(); start_offset_tumble(0.0f, 1.0f); }
void firefly_tumble_forward(void)  { tumble_cancel(); start_scale_tumble(FORWARD_SCALE_DELTA, false); }
void firefly_tumble_backward(void) { tumble_cancel(); start_scale_tumble(-BACKWARD_SCALE_DELTA, true); }
void firefly_tumble_wave(void)     { tumble_cancel(); start_wave_tumble(); }
void firefly_tumble_cw(void)       { tumble_cancel(); start_loop(+1, s_cur_level); }
void firefly_tumble_ccw(void)      { tumble_cancel(); start_loop(-1, s_cur_level); }

void firefly_go_home_start(void)
{
    s_dance_active = false;
    s_loop_active  = false;
    s_home_from_x = s_disp_x;
    s_home_from_y = s_disp_y;
    s_going_home  = true;
    s_home_done   = false;

    bsp_display_lock(0);
    lv_anim_delete(&s_home_anim_var, home_anim_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_home_anim_var);
    lv_anim_set_exec_cb(&a, home_anim_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, HOME_FLY_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, home_anim_completed_cb);
    lv_anim_start(&a);
    bsp_display_unlock();
}

bool firefly_go_home_is_done(void) { return s_home_done; }

void firefly_go_home_interrupt(void)
{
    if (!s_going_home) return;
    bsp_display_lock(0);
    lv_anim_delete(&s_home_anim_var, home_anim_exec);
    bsp_display_unlock();
    s_going_home = false;
}

void firefly_freeze(void)
{
    stop_blink();
    tumble_cancel();
    s_dance_active = false;
    if (s_going_home) {
        bsp_display_lock(0);
        lv_anim_delete(&s_home_anim_var, home_anim_exec);
        bsp_display_unlock();
        // s_going_home 保持 true;权威位置留在中断处。唤醒后 firefly_enter_seek()
        // 会强制回家位置 + 重开慢眨眼,不续播剩余的回家动画(与"每次唤醒回 SEEK"
        // 的设计一致,见 magic_wand.c game_task)。
    }
}

void firefly_enter_seek(void)
{
    tumble_cancel();
    s_dance_active = false;
    if (s_going_home) {
        bsp_display_lock(0);
        lv_anim_delete(&s_home_anim_var, home_anim_exec);
        bsp_display_unlock();
        s_going_home = false;
    }
    s_disp_x   = s_home_x;
    s_disp_y   = s_home_y;
    s_home_done = true;
    for (int i = 0; i < FIREFLY_HIST_LEN; i++) {
        s_hist_x[i] = s_home_x;
        s_hist_y[i] = s_home_y;
    }
    start_blink(BLINK_HOME_MS);
}
