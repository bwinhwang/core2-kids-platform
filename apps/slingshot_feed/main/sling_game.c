// slingshot_feed —— 游戏层实现(SPEC.md §4~§10)
//
// 状态机:AIM(蓄力/预览)⇄ FLIGHT(弹道积分)⇄ EAT/MISS(短反馈态,自动回 AIM)⇄
//        GROW(喂饱长大,自动回 AIM 换下一只)⇄ PARTY(喂够一批,~3.5s 后清场换新批)。
// 拉-放检测(§5.1):力度取蓄力历史窗峰值、角度取松手边沿前最后 1~2 个稳定采样的方向,
// 与预览共用同一 compute_v0(),满足"发射弹道 ≡ 松手前一帧预览弧"不变式。
// 动画所有权纪律(仿 busy_bus bus_game.c):状态转换函数只改"影子变量"+ 低频调用精灵
// 的自锁装扮函数(dress/mouth_open/cheeks/set_color,busy_bus"小鸟"先例);逐帧的坐标/
// 缩放/显隐统一在 render_all() 一处落 LVGL(单把锁),不跨任务碰 LVGL 状态。
#include "sling_game.h"

#include <math.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "ledstrip_fx.h"

#include "sling_link.h"
#include "feedback.h"
#include "meadow.h"
#include "sprites.h"

#include "tuning.h"

// ── 状态机 ───────────────────────────────────────────────────────────
typedef enum { SG_AIM = 0, SG_FLIGHT, SG_EAT, SG_MISS, SG_GROW, SG_PARTY } sling_state_t;

static sling_state_t s_state;
static int  s_state_frames;

// 拉-放检测(§5.1)
static bool s_charging;
static int  s_below_aim_frames;   // RELEASE_WINDOW_MS>0 时:mag 落在 [RELEASE_THRESH,AIM_MIN) 的帧数
#if FIRE_MODE == 1
static bool s_prev_z_btn;         // 仅 Z 键降级发射模式用(§5.1 ③)
#endif
static int  s_lockout_frames;     // 发射后短锁(FIRE_LOCKOUT_MS)

typedef struct { float x, y; } pull_sample_t;
static pull_sample_t s_hist[PULL_HIST_LEN];
static int s_hist_count, s_hist_head;

// 预览弧(仅 AIM 态蓄力时有效)
static vec2_t s_preview[TRAJ_DOTS];
static int    s_preview_count;
static bool   s_preview_show;

// 皮筋兜位置:任何状态都跟手(§4 门控期空皮筋仍跟手),每帧纯数学算好,render_all 落地
static vec2_t s_pouch_pos;

// 果子飞行
static float  s_fruit_vx, s_fruit_vy;
static vec2_t s_fruit_pos;
static int    s_flight_ms;

// 动物 / 喂饱 / 批次
static int   s_animal_spot = -1;
static int   s_feed;
static int   s_animal_fed_count;
static float s_animal_scale = 1.0f;
static float s_animal_bounce_dy;
static bool  s_mouth_open_cache;

static int s_miss_cooldown;

// 改进 A:动物"活起来"(眼跟随 / 眨眼 / 呼吸 / 久等催促 / 瞄准锁定 + 落点光圈)。
// 逐帧的眼偏移/眨眼/身体微倾/呼吸小跳统一在 render_all 一处落 LVGL(单把锁,不新增失效区来源)。
static bool  s_aim_locked;          // 预览弧任一点靠近嘴 = 瞄准锁定 → 动物兴奋 + 落点光圈
static int   s_lock_snd_cooldown;   // "来嘛~"锁定音节流(防连续扫瞄机枪式响)
static int   s_blink_ms;            // 眨眼相位(所有态)
static bool  s_blink_on;
static int   s_idle_ms;             // AIM 且未蓄力累计 → 触发"还要~"催促
static int   s_breath_ms;           // 呼吸相位
static int   s_hop_frames;          // "还要~"小跳剩余帧
static float s_life_bob;            // 呼吸 + 催促小跳的 y 偏移(仅 idle-AIM 非零,render 落)

// 精灵句柄
static band_sprite_t   s_band;
static fruit_sprite_t  s_fruit_spr;
static animal_sprite_t s_animal_spr;
static lv_obj_t *s_traj_dot[TRAJ_DOTS];
static lv_obj_t *s_halo;            // 改进 A:瞄准锁定时套在嘴上的落点光圈

// 派对彩纸(仿 busy_bus,≤8 片,SPEC §7)
static lv_obj_t *s_confetti[CONFETTI_N];
static float     s_confetti_x[CONFETTI_N], s_confetti_y[CONFETTI_N];
static int       s_confetti_vx[CONFETTI_N];

// 无摇杆提示卡
static lv_obj_t *s_hint_card;
static bool      s_hint_shown;

// 灯带常态基准缓存(避免每帧重复下发)
static int s_led_base_cache = -1;

// ── 前置声明(状态互相跳转,避免依赖定义顺序)──────────────────────────────
static void reload_same_animal(void);
static void reload_new_animal(void);
static void enter_eat(void);
static void enter_miss(void);
static void enter_grow(void);
static void enter_party(void);
static void render_all(void);
static void sync_led_base(void);
static void pick_new_fruit_hue(void);
static void confetti_show(bool show);
static void tick_confetti_fall(void);
static void update_aim_lock(void);   // 改进 A:预览弧靠近嘴 → 兴奋 + 落点光圈 + 一声"来嘛~"
static void update_life(void);        // 改进 A:眨眼 / 呼吸 / 久等催促(逐帧,所有态)

// ── 小工具 ───────────────────────────────────────────────────────────
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void center_at(lv_obj_t *o, float cx, float cy)
{
    int w = lv_obj_get_width(o), h = lv_obj_get_height(o);
    lv_obj_set_pos(o, (int)(cx - w / 2.0f), (int)(cy - h / 2.0f));
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
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *outline(lv_obj_t *parent, int w, int h, uint32_t border, int bw, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(o, bw, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// ── 拉力历史窗(§5.1:力度取峰值、角度取松手前最后 1~2 个稳定采样)───────────────
static void hist_clear(void) { s_hist_count = 0; s_hist_head = 0; }

static void hist_push(float x, float y)
{
    s_hist[s_hist_head] = (pull_sample_t){ x, y };
    s_hist_head = (s_hist_head + 1) % PULL_HIST_LEN;
    if (s_hist_count < PULL_HIST_LEN) s_hist_count++;
}

static float hist_peak_mag(void)
{
    float peak = 0.f;
    for (int i = 0; i < s_hist_count; i++) {
        float m = sqrtf(s_hist[i].x * s_hist[i].x + s_hist[i].y * s_hist[i].y);
        if (m > peak) peak = m;
    }
    return peak;
}

static void hist_last_dir(float *dx, float *dy)
{
    if (s_hist_count == 0) { *dx = 0.f; *dy = -1.f; return; }
    int n = s_hist_count < 2 ? 1 : 2;
    float sx = 0.f, sy = 0.f;
    int idx = s_hist_head;
    for (int k = 0; k < n; k++) {
        idx = (idx - 1 + PULL_HIST_LEN) % PULL_HIST_LEN;
        sx += s_hist[idx].x; sy += s_hist[idx].y;
    }
    sx /= n; sy /= n;
    float len = sqrtf(sx * sx + sy * sy);
    if (len < 1e-4f) { *dx = 0.f; *dy = -1.f; }
    else { *dx = sx / len; *dy = sy / len; }
}

// ── 弹道:预览与发射共用同一 v0 公式(§5.1 不变式 / §5.2)─────────────────────
static void compute_v0(float px, float py, float *vx, float *vy)
{
    float mag = sqrtf(px * px + py * py);
    float clamped = mag > 1.0f ? 1.0f : mag;
    float scale = (mag > 1e-4f) ? (clamped / mag) : 0.f;
    *vx = -px * scale * LAUNCH_POWER;
    *vy = -py * scale * LAUNCH_POWER;
}

static void compute_preview(float px, float py)
{
    float vx, vy; compute_v0(px, py, &vx, &vy);
    vec2_t a = meadow_sling_anchor();
    s_preview_count = 0;
    const float dt = 0.09f;
    for (int i = 0; i < TRAJ_DOTS; i++) {
        float t = (i + 1) * dt;
        float x = a.x + vx * t;
        float y = a.y + vy * t + 0.5f * GRAVITY * t * t;
        if (x < -8 || x > SCREEN_W + 8 || y > SCREEN_H + 8) break;
        s_preview[s_preview_count++] = (vec2_t){ x, y };
    }
}

static void update_charge_rgb(float mag)
{
    float t = clampf((mag - AIM_MIN) / (1.0f - AIM_MIN), 0.f, 1.f);
    uint8_t r = (uint8_t)(90 + 165 * t);
    uint8_t g = (uint8_t)(60 + 120 * t);
    uint8_t b = (uint8_t)(20 + 40 * t);
    sling_link_joy_rgb(r, g, b);
}

static void sync_joy_idle_rgb(void)
{
    sling_link_joy_rgb(40, 30, 20);   // 暖灰底色(非蓄力/非事件闪光时的常态)
}

// ── 改进 A:瞄准锁定(预览弧任一点距嘴 < AIM_LOCK_TOL_PX)───────────────────────
// 预览点用 dt=0.09 粗采样(点间距最大 ~38px),1.5×命中容差(42px)足以在弧"穿过嘴"时点亮。
// 锁定 = 动物兴奋(眼放大)+ 嘴上落点光圈(render_all 落)+ 一声节流的"来嘛~",强化自教。
static void update_aim_lock(void)
{
    vec2_t mouth = meadow_spot_pos(s_animal_spot);
    float best2 = 1e18f;
    for (int i = 0; i < s_preview_count; i++) {
        float dx = s_preview[i].x - mouth.x, dy = s_preview[i].y - mouth.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best2) best2 = d2;
    }
    bool locked = best2 < (float)AIM_LOCK_TOL_PX * (float)AIM_LOCK_TOL_PX;
    if (locked && !s_aim_locked && s_lock_snd_cooldown <= 0) {
        feedback_emit_lock();
        s_lock_snd_cooldown = LOCK_SND_COOLDOWN_MS / POLL_PERIOD_MS;
    }
    s_aim_locked = locked;
}

// ── 发射 ─────────────────────────────────────────────────────────────
static void fire_with(float dirx, float diry, float power)
{
    float vx, vy;
    compute_v0(dirx * power, diry * power, &vx, &vy);
    s_fruit_vx = vx; s_fruit_vy = vy;
    s_fruit_pos = meadow_sling_anchor();
    s_flight_ms = 0;

    s_state = SG_FLIGHT; s_state_frames = 0;
    s_lockout_frames = FIRE_LOCKOUT_MS / POLL_PERIOD_MS;
    s_charging = false; hist_clear(); s_below_aim_frames = 0;
    s_preview_show = false;

    feedback_emit_fire();
}

static void do_fire_from_hist(void)
{
    float dx, dy; hist_last_dir(&dx, &dy);
    float power = hist_peak_mag(); if (power > 1.0f) power = 1.0f;
    fire_with(dx, dy, power);
}

// ── AIM 态:拉-放检测(§5.1)+ 预览(§5.2)──────────────────────────────────
static void update_band_visual(void)
{
    float px = sling_link_joy_attached() ? sling_link_joy_x() : 0.f;
    float py = sling_link_joy_attached() ? sling_link_joy_y() : 0.f;
    float mag = sqrtf(px * px + py * py);
    float clamped = mag > 1.0f ? 1.0f : mag;
    float scale = (mag > 1e-4f) ? (clamped / mag) : 0.f;
    vec2_t a = meadow_sling_anchor();
    s_pouch_pos.x = a.x + px * scale * PULL_VISUAL_PX;
    s_pouch_pos.y = a.y + py * scale * PULL_VISUAL_PX;
}

static void tick_aim(void)
{
    if (s_lockout_frames > 0) {
        // 发射后短锁:忽略蓄力判定,防回中震荡幻影蓄力(§5.1);皮筋仍跟手(update_band_visual 已处理)
        s_charging = false; hist_clear(); s_below_aim_frames = 0;
        s_preview_show = false;
        s_aim_locked = false;
        return;
    }

    float px = sling_link_joy_attached() ? sling_link_joy_x() : 0.f;
    float py = sling_link_joy_attached() ? sling_link_joy_y() : 0.f;
    float mag = sqrtf(px * px + py * py);

#if FIRE_MODE == 0
    if (s_charging) {
        if (mag < RELEASE_THRESH) {
#if RELEASE_WINDOW_MS > 0
            int window_frames = RELEASE_WINDOW_MS / POLL_PERIOD_MS;
            if (s_below_aim_frames <= window_frames) do_fire_from_hist();
#else
            do_fire_from_hist();
#endif
            s_charging = false; hist_clear(); s_below_aim_frames = 0;
        } else if (mag < AIM_MIN) {
            // 落回 AIM_MIN 以下但还没到 RELEASE_THRESH:自然回弹必经的 [RELEASE_THRESH,AIM_MIN) 带
#if RELEASE_WINDOW_MS > 0
            // 窗口模式:超过窗口才算"慢缩回=蓄力中止";否则继续等它跌破 RELEASE_THRESH 再发
            s_below_aim_frames++;
            int window_frames = RELEASE_WINDOW_MS / POLL_PERIOD_MS;
            if (s_below_aim_frames > window_frames) { s_charging = false; hist_clear(); s_below_aim_frames = 0; }
#else
            // 🔴 跌破即发(默认):任何跌破 AIM_MIN 的回缩都算松手射出(SPEC §5.1"任何回缩都算射出")。
            // 松手回弹从 >AIM_MIN 连续经过本带才到 <RELEASE_THRESH,50Hz 下几乎必有一帧落在此带;
            // 若在此"蓄力中止"清掉 charging,松手边沿永远走不到上面的发射分支 → "松手不发射"。
            // 这里直接发射:力度取历史窗峰值、角度取松手前最后稳定采样(hist 尚未被回弹样本污染,§5.1 不变式成立)。
            do_fire_from_hist();
            s_charging = false; hist_clear();
#endif
        } else {
            s_below_aim_frames = 0;
            hist_push(px, py);
        }
    } else if (mag >= AIM_MIN) {
        hist_clear();
        hist_push(px, py);
        s_charging = true;
        s_below_aim_frames = 0;
    }
#else   // FIRE_MODE == 1:拉住瞄准、按 Z 发射(降级模式,§5.1/§11①)
    s_charging = (mag >= AIM_MIN);
    bool btn  = sling_link_joy_attached() && sling_link_joy_button();
    bool edge = btn && !s_prev_z_btn;
    s_prev_z_btn = btn;
    if (edge && s_charging) {
        float dirx = px / mag, diry = py / mag;
        float power = mag > 1.0f ? 1.0f : mag;
        fire_with(dirx, diry, power);
        return;
    }
#endif

    if (s_charging) {
        compute_preview(px, py);
        s_preview_show = true;
        update_charge_rgb(mag);
        update_aim_lock();
    } else {
        s_preview_show = false;
        s_aim_locked = false;
    }
}

// ── FLIGHT:弹道积分 + 命中判定(§5.2/§5.3)───────────────────────────────
static void tick_flight(void)
{
    float dt = POLL_PERIOD_MS / 1000.0f;
    s_fruit_vy  += GRAVITY * dt;
    s_fruit_pos.x += s_fruit_vx * dt;
    s_fruit_pos.y += s_fruit_vy * dt;
    s_flight_ms   += POLL_PERIOD_MS;

    vec2_t mouth = meadow_spot_pos(s_animal_spot);
    float dx = s_fruit_pos.x - mouth.x, dy = s_fruit_pos.y - mouth.y;
    if (dx * dx + dy * dy < (float)HIT_TOL_PX * HIT_TOL_PX) { enter_eat(); return; }

    bool oob = s_fruit_pos.x < -8 || s_fruit_pos.x > SCREEN_W + 8 ||
               s_fruit_pos.y > GROUND_Y_PX || s_flight_ms > FLIGHT_MAX_MS;
    if (oob) enter_miss();
}

// ── EAT:嚼两下 + 喂饱度(§5.4)────────────────────────────────────────────
static void enter_eat(void)
{
    s_state = SG_EAT; s_state_frames = 0;
    s_feed++;
    sprites_animal_cheeks(&s_animal_spr, true);
    feedback_emit_eat(s_feed, s_animal_spr.species);
}

static void tick_eat(void)
{
    s_state_frames++;
    int total = EAT_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    int phase = (s_state_frames * 4) / total;
    bool want_open = (phase % 2) == 1;
    if (want_open != s_mouth_open_cache) {
        sprites_animal_mouth_open(&s_animal_spr, want_open);
        s_mouth_open_cache = want_open;
    }
    if (s_state_frames >= total) {
        sprites_animal_cheeks(&s_animal_spr, false);
        if (!s_mouth_open_cache) {
            sprites_animal_mouth_open(&s_animal_spr, true);
            s_mouth_open_cache = true;
        }
        if (s_feed >= FEED_PER_ANIMAL) enter_grow();
        else reload_same_animal();
    }
}

// ── MISS:落地变小花(§5.5/§7)──────────────────────────────────────────────
static void enter_miss(void)
{
    s_state = SG_MISS; s_state_frames = 0;

    float fx = clampf(s_fruit_pos.x, 4.0f, (float)SCREEN_W - 4.0f);
    float fy = s_fruit_pos.y;
    if (fy > GROUND_Y_PX || fy < 20.0f) fy = GROUND_Y_PX;
    meadow_flower_add(fx, fy);

    if (s_miss_cooldown <= 0) {
        feedback_emit_miss();
        s_miss_cooldown = MISS_SND_COOLDOWN_MS / POLL_PERIOD_MS;
    }
}

static void tick_miss(void)
{
    s_state_frames++;
    int total = MISS_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    if (s_state_frames >= total) reload_same_animal();
}

// ── GROW:蹦跳长大(§5.4)──────────────────────────────────────────────────
static void enter_grow(void)
{
    s_state = SG_GROW; s_state_frames = 0;
    s_animal_fed_count++;
    feedback_emit_grow(s_animal_spr.species);
}

static void tick_grow(void)
{
    s_state_frames++;
    int total = GROW_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)s_state_frames / total, 0.f, 1.f);
    float bounce = sinf(t * (float)M_PI);
    s_animal_scale = 1.0f + 0.35f * bounce;
    s_animal_bounce_dy = -10.0f * bounce;

    if (s_state_frames >= total) {
        s_animal_scale = 1.0f; s_animal_bounce_dy = 0.0f;
        // 改进 B:这只喂饱了 → 蹦到草地边攒成好朋友(取色须在 reload_new_animal 改物种之前)
        meadow_friend_add(sprites_species_body_color(s_animal_spr.species));
        if (s_animal_fed_count >= ANIMAL_QUOTA) enter_party();
        else reload_new_animal();
    }
}

// ── PARTY:喂够一批(§6)────────────────────────────────────────────────────
static void enter_party(void)
{
    s_state = SG_PARTY; s_state_frames = 0;
    feedback_emit_party();
    confetti_show(true);
}

static void tick_party(void)
{
    s_state_frames++;
    int total = PARTY_HOLD_MS / POLL_PERIOD_MS; if (total < 1) total = 1;

    float bt = fmodf((float)s_state_frames / (total / 6.0f > 1.0f ? total / 6.0f : 1.0f), 1.0f);
    s_animal_bounce_dy = -8.0f * sinf(bt * (float)M_PI);   // 当前动物顺带小跳表示欢庆

    // 15~17fps 庆祝档(CLAUDE.md §6.5):彩纸/跑马灯/好朋友群跳每 3 帧才推进一次
    if (s_state_frames % 3 == 0) {
        int hue = (s_state_frames * 6) % 360;
        uint8_t r, g, b;
        sling_link_hue2rgb(hue, &r, &g, &b);
        sling_link_joy_rgb(r, g, b);
        tick_confetti_fall();
        meadow_friends_bob((int)(-6.0f * sinf(bt * (float)M_PI)));   // 改进 B:攒下的好朋友整排群跳
    }

    if (s_state_frames >= total) {
        confetti_show(false);
        meadow_flower_clear();
        meadow_friends_clear();          // 改进 B:群跳结束,挥手告别,清排换新批
        s_animal_fed_count = 0;
        s_animal_bounce_dy = 0.0f;
        reload_new_animal();
    }
}

// ── 重装填 / 换新动物 ─────────────────────────────────────────────────────
static void pick_new_fruit_hue(void)
{
    int hue = (int)(esp_random() % 360);
    uint8_t r, g, b;
    sling_link_hue2rgb(hue, &r, &g, &b);
    sprites_fruit_set_color(&s_fruit_spr, r, g, b);
}

static void reload_same_animal(void)
{
    s_state = SG_AIM; s_state_frames = 0;
    s_charging = false; hist_clear(); s_below_aim_frames = 0;
    s_preview_show = false;
    pick_new_fruit_hue();
    sync_led_base();
}

static void reload_new_animal(void)
{
    int spot    = meadow_pick_spot(s_animal_spot);
    int species = (int)(esp_random() % ANIMAL_SPECIES);
    s_animal_spot = spot;
    s_feed = 0;

    sprites_animal_dress(&s_animal_spr, species);
    sprites_animal_mouth_open(&s_animal_spr, true);
    s_mouth_open_cache = true;
    sprites_animal_cheeks(&s_animal_spr, false);
    s_animal_scale = 1.0f; s_animal_bounce_dy = 0.0f;

    reload_same_animal();
}

// ── 底座灯带常态基准(近喂饱加亮,SPEC §6"近喂饱基色加亮")─────────────────────
static void sync_led_base(void)
{
    led_base_t b = (s_feed == FEED_PER_ANIMAL - 1) ? LED_BASE_NEAR : LED_BASE_AMBIENT;
    if ((int)b == s_led_base_cache) return;
    s_led_base_cache = (int)b;
    ledstrip_fx_set_base(b);
}

// ── 派对彩纸(仿 busy_bus,≤8 片,SPEC §7)───────────────────────────────────
static void confetti_show(bool show)
{
    bsp_display_lock(0);
    for (int i = 0; i < CONFETTI_N; i++) {
        if (show) {
            s_confetti_x[i] = (float)(esp_random() % (SCREEN_W - 8));
            s_confetti_y[i] = -8.0f - (float)(esp_random() % 40);
            s_confetti_vx[i] = (int)(esp_random() % 3) - 1;
            uint8_t r, g, b;
            sling_link_hue2rgb((int)(esp_random() % 360), &r, &g, &b);
            lv_obj_set_style_bg_color(s_confetti[i], lv_color_make(r, g, b), 0);
            lv_obj_set_pos(s_confetti[i], (int)s_confetti_x[i], (int)s_confetti_y[i]);
            lv_obj_remove_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    bsp_display_unlock();
}

static void tick_confetti_fall(void)
{
    for (int i = 0; i < CONFETTI_N; i++) {
        s_confetti_y[i] += 3;
        if (s_confetti_y[i] > SCREEN_H) s_confetti_y[i] = -8;
        s_confetti_x[i] += s_confetti_vx[i];
        if (s_confetti_x[i] < 0 || s_confetti_x[i] > SCREEN_W - 8) s_confetti_vx[i] = -s_confetti_vx[i];
    }
}

// ── 提示卡(没认到摇杆时显示,SPEC §1 通用形态)──────────────────────────────
static void make_hint_card(lv_obj_t *scr)
{
    lv_obj_t *card = plain(scr, 132, 76, 0xFFFFFF, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *box = outline(card, 34, 34, 0x9C9AD0, 4, 8);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *stick = plain(box, 10, 10, 0x9C9AD0, LV_RADIUS_CIRCLE);
    lv_obj_align(stick, LV_ALIGN_CENTER, 5, -5);
    lv_obj_t *wire = plain(card, 34, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 50, 0);
    lv_obj_t *plug = plain(card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 84, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    s_hint_card = card;
    s_hint_shown = false;
}

static void hint_card_apply(bool show)
{
    if (show == s_hint_shown) return;
    s_hint_shown = show;
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

// ── 渲染(唯一触碰 LVGL 坐标/缩放/显隐的地方,单把锁,SPEC §7 预算合规)──────────
static void render_all(void)
{
    bsp_display_lock(0);

    // 皮筋:兜 + 两股示意点(任何状态都跟手,§4 门控期空皮筋仍跟手)
    center_at(s_band.pouch, s_pouch_pos.x, s_pouch_pos.y);
    for (int i = 0; i < BAND_DOTS; i++) {
        float t = (float)(i + 1) / (BAND_DOTS + 1);
        center_at(s_band.dot_l[i], FORK_TIP_L_X + (s_pouch_pos.x - FORK_TIP_L_X) * t,
                                    FORK_TIP_L_Y + (s_pouch_pos.y - FORK_TIP_L_Y) * t);
        center_at(s_band.dot_r[i], FORK_TIP_R_X + (s_pouch_pos.x - FORK_TIP_R_X) * t,
                                    FORK_TIP_R_Y + (s_pouch_pos.y - FORK_TIP_R_Y) * t);
    }

    // 果子:AIM(兜位)/ FLIGHT(弹道)可见;门控期(未装填)隐藏
    bool fruit_visible = (s_state == SG_AIM || s_state == SG_FLIGHT);
    if (fruit_visible) {
        vec2_t fp = (s_state == SG_FLIGHT) ? s_fruit_pos : s_pouch_pos;
        center_at(s_fruit_spr.body, fp.x, fp.y);
        center_at(s_fruit_spr.leaf, fp.x, fp.y - FRUIT_SZ / 2.0f);
        lv_obj_remove_flag(s_fruit_spr.body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_fruit_spr.leaf, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_fruit_spr.body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_fruit_spr.leaf, LV_OBJ_FLAG_HIDDEN);
    }

    // 预览弧(仅 AIM 态蓄力时)
    bool show_traj = (s_state == SG_AIM) && s_preview_show;
    for (int i = 0; i < TRAJ_DOTS; i++) {
        if (show_traj && i < s_preview_count) {
            center_at(s_traj_dot[i], s_preview[i].x, s_preview[i].y);
            lv_obj_remove_flag(s_traj_dot[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_traj_dot[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 动物:位置(GROW/PARTY 弹跳 + 改进 A 呼吸/催促小跳 + 朝瞄准点微倾)+ 缩放
    vec2_t mouth = meadow_spot_pos(s_animal_spot);
    int aw = lv_obj_get_width(s_animal_spr.container), ah = lv_obj_get_height(s_animal_spr.container);

    // 改进 A:看向兜(AIM)/飞行果子(FLIGHT)—— 算眼睛跟随偏移 + 身体水平微倾;其余态归零看正前
    int eox = 0, eoy = 0; float lean = 0.0f;
    if (s_state == SG_AIM || s_state == SG_FLIGHT) {
        vec2_t look = (s_state == SG_FLIGHT) ? s_fruit_pos : s_pouch_pos;
        float ldx = look.x - mouth.x, ldy = look.y - mouth.y;
        float ll = sqrtf(ldx * ldx + ldy * ldy);
        if (ll > 1.0f) {
            eox  = (int)lroundf(ldx / ll * EYE_TRACK_PX);
            eoy  = (int)lroundf(ldy / ll * EYE_TRACK_PX);
            lean = ldx / ll * LEAN_PX;
        }
    }
    lv_obj_set_pos(s_animal_spr.container,
                    (int)(mouth.x - aw / 2.0f + lean),
                    (int)(mouth.y - ah * 0.8f + s_animal_bounce_dy + s_life_bob));
    lv_obj_set_style_transform_scale_x(s_animal_spr.container, (int32_t)(LV_SCALE_NONE * s_animal_scale), 0);
    lv_obj_set_style_transform_scale_y(s_animal_spr.container, (int32_t)(LV_SCALE_NONE * s_animal_scale), 0);

    // 改进 A:眼睛尺寸(眨眼压成一条线 / 锁定时放大表兴奋)+ 跟随偏移(局部坐标,平移不变)
    int ew = ANIMAL_EYE_SZ, eh = ANIMAL_EYE_SZ;
    if (s_blink_on)                              eh = 1;
    else if (s_state == SG_AIM && s_aim_locked) { ew = ANIMAL_EYE_SZ + 2; eh = ANIMAL_EYE_SZ + 2; }
    lv_obj_set_size(s_animal_spr.eye_l, ew, eh);
    lv_obj_set_size(s_animal_spr.eye_r, ew, eh);
    lv_obj_set_pos(s_animal_spr.eye_l, s_animal_spr.eye_lx + eox, s_animal_spr.eye_ly + eoy);
    lv_obj_set_pos(s_animal_spr.eye_r, s_animal_spr.eye_rx + eox, s_animal_spr.eye_ry + eoy);

    // 改进 A:落点光圈 —— 仅 AIM 且瞄准锁定时套在嘴上(呼应命中容差,自教"对准了就能喂进")
    if (s_state == SG_AIM && s_aim_locked) {
        center_at(s_halo, mouth.x, mouth.y);
        lv_obj_remove_flag(s_halo, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_halo, LV_OBJ_FLAG_HIDDEN);
    }

    // 派对彩纸
    if (s_state == SG_PARTY) {
        for (int i = 0; i < CONFETTI_N; i++) {
            lv_obj_set_pos(s_confetti[i], (int)s_confetti_x[i], (int)s_confetti_y[i]);
        }
    }

    bsp_display_unlock();
}

// ── 对外接口 ─────────────────────────────────────────────────────────
void sling_game_create(void)
{
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    meadow_create(scr);

    sprites_band_create(scr, &s_band);
    for (int i = BAND_DOTS; i < BAND_DOTS_MAX; i++) {   // 数组留了余量,超出 tuning.h 用量的隐藏且不再碰
        lv_obj_add_flag(s_band.dot_l[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_band.dot_r[i], LV_OBJ_FLAG_HIDDEN);
    }

    sprites_fruit_create(scr, &s_fruit_spr);
    sprites_animal_create(scr, &s_animal_spr);

    for (int i = 0; i < TRAJ_DOTS; i++) {
        s_traj_dot[i] = plain(scr, 4, 4, 0xFFE58A, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(s_traj_dot[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 改进 A:落点光圈(瞄准锁定时套在嘴上),暖黄描边圈,初始隐藏
    s_halo = outline(scr, HALO_SZ, HALO_SZ, 0xFFE58A, 3, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(s_halo, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < CONFETTI_N; i++) {
        lv_obj_t *c = plain(scr, 6, 6, 0xFFFFFF, 2);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        s_confetti[i] = c;
    }

    make_hint_card(scr);

    bsp_display_unlock();

    s_animal_spot = -1;   // 首轮 meadow_pick_spot(-1) 不排除任何位置
    s_animal_fed_count = 0;
    reload_new_animal();

    render_all();
}

// 改进 A:眨眼(所有态)+ 呼吸/久等催促(仅 AIM 未蓄力)。逐帧算好 y 偏移,render_all 落地。
static void update_life(void)
{
    s_blink_ms += POLL_PERIOD_MS;
    if (s_blink_on) {
        if (s_blink_ms >= BLINK_MS) { s_blink_on = false; s_blink_ms = 0; }
    } else if (s_blink_ms >= BLINK_PERIOD_MS) {
        s_blink_on = true; s_blink_ms = 0;
    }

    bool idle_aim = (s_state == SG_AIM) && !s_charging && s_lockout_frames <= 0;
    if (idle_aim) {
        s_breath_ms += POLL_PERIOD_MS;
        float breath = 1.5f * sinf((float)s_breath_ms / 900.0f * 2.0f * (float)M_PI);   // 慢呼吸 ±1.5px
        s_idle_ms += POLL_PERIOD_MS;
        if (s_idle_ms >= IMPATIENT_AFTER_MS) {   // 久等无人喂 → "还要~"催促 + 起跳(改进 A)
            s_idle_ms = 0;
            s_hop_frames = IMPATIENT_HOP_MS / POLL_PERIOD_MS;
            feedback_emit_call();
        }
        float hop = 0.0f;
        if (s_hop_frames > 0) {
            s_hop_frames--;
            int htot = IMPATIENT_HOP_MS / POLL_PERIOD_MS; if (htot < 1) htot = 1;
            float t = 1.0f - (float)s_hop_frames / htot;
            hop = -9.0f * sinf(t * (float)M_PI);
        }
        s_life_bob = breath + hop;
    } else {
        s_idle_ms = 0;
        s_hop_frames = 0;
        s_life_bob = 0.0f;
    }
}

void sling_game_tick(void)
{
    update_band_visual();

    switch (s_state) {
        case SG_AIM:    tick_aim();    break;
        case SG_FLIGHT: tick_flight(); break;
        case SG_EAT:    tick_eat();    break;
        case SG_MISS:   tick_miss();   break;
        case SG_GROW:   tick_grow();   break;
        case SG_PARTY:  tick_party();  break;
    }

    update_life();

    bool charging_now = (s_state == SG_AIM) && s_charging;
    if (!charging_now && s_state != SG_PARTY) sync_joy_idle_rgb();

    if (s_lockout_frames   > 0) s_lockout_frames--;
    if (s_miss_cooldown    > 0) s_miss_cooldown--;
    if (s_lock_snd_cooldown > 0) s_lock_snd_cooldown--;

    render_all();
}

void sling_game_sync_attach(void)
{
    hint_card_apply(!sling_link_joy_attached());
}

void sling_game_reset_after_wake(void)
{
    // 弹弓复位待命:不管醒来前处在哪个态,一律强制回 AIM 重装填(飞行中果子作废);
    // 动物(位置/物种/已喂饱进度)保留不清(SPEC §10"复位安全位置、不吞进度")。
    s_state = SG_AIM; s_state_frames = 0;
    s_charging = false; hist_clear(); s_below_aim_frames = 0;
    s_lockout_frames = 0;
    s_preview_show = false;
    s_animal_scale = 1.0f; s_animal_bounce_dy = 0.0f;
    // 改进 A 瞬态复位(好朋友排 = 批次进度,不清,守 SPEC §10"复位安全态、不吞进度")
    s_aim_locked = false; s_lock_snd_cooldown = 0;
    s_idle_ms = 0; s_hop_frames = 0; s_life_bob = 0.0f;
    s_blink_on = false; s_blink_ms = 0;

    sprites_animal_mouth_open(&s_animal_spr, true);
    s_mouth_open_cache = true;
    sprites_animal_cheeks(&s_animal_spr, false);

    pick_new_fruit_hue();
    sync_led_base();
    render_all();
}
