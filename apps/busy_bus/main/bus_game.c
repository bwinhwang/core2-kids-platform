// busy_bus —— 游戏层实现(SPEC.md §4~§10)
//
// 状态机:RS_PLAY(默认)⇄ RS_PARTY(三位全送达后 ~3.5s 全镇亮灯庆祝 → 换新一轮)。
// 巴士:摇杆向量 EMA 平滑成速度(§3,BUS_DRIVE_MODE 一行切换速度型/恒速型),
//      8 朝向预烘精灵按迟滞换图;撞房子/树/喷泉/屏边滑行不粘住。
// 乘客子状态机:WAITING(站牌待机小晃)→ BOARDING(蹦上车,~HOP_MS)→
//      RIDING(隐身,图泡挂车顶跟车)→ ALIGHTING(蹦下车进门,~HOP_MS)→ HOME。
//      逻辑坐标存影子变量(p->x/y),渲染只在 render_all() 一处统一落 LVGL
//      (busy_knobs 小鸟先例:不跨任务碰 LVGL 状态,§5 动画所有权纪律)。
#include "bus_game.h"

#include <math.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "bus_link.h"
#include "feedback.h"
#include "sprites.h"
#include "town.h"

#include "tuning.h"

// ── 乘客子状态机 ─────────────────────────────────────────────────────
typedef enum { PSG_NONE = 0, PSG_WAITING, PSG_BOARDING, PSG_RIDING, PSG_ALIGHTING, PSG_HOME } psg_state_t;

typedef struct {
    psg_state_t state;
    int   state_frames;
    int   stop_idx;
    int   dest_house;
    int   look;
    float x, y;                              // 影子坐标(WAITING/BOARDING/ALIGHTING 显示位置)
    float from_x, from_y, to_x, to_y;         // BOARDING/ALIGHTING 起止点
    int   wiggle_frames;                      // 喇叭回应的小幅摆动
    passenger_sprite_t sprite;
    bubble_sprite_t    bubble;
} passenger_t;

typedef enum { RS_PLAY = 0, RS_PARTY } round_state_t;

// ── 状态 ─────────────────────────────────────────────────────────────
static round_state_t s_round_state;
static int   s_party_frames;
static int   s_delivered_count;

static float s_bus_x, s_bus_y;
static float s_bus_vx, s_bus_vy;
static int   s_heading;
static int   s_carry = -1;                    // -1 = 空车,否则 s_pax 下标
static bool  s_prev_joy_btn;
static int   s_honk_cooldown;
static int   s_bump_cooldown;
static int   s_wrong_door_cooldown;
static float s_squash;                        // 撞障碍挤压脉冲(0~1,衰减)
static float s_pop;                           // 喇叭"弹一下"脉冲(0~1,衰减)

static passenger_t s_pax[PASSENGERS_PER_ROUND];

static lv_obj_t *s_bus_img;
static lv_obj_t *s_hint_card;
static bool      s_hint_shown;

static lv_obj_t *s_confetti[CONFETTI_N];
static float     s_confetti_x[CONFETTI_N], s_confetti_y[CONFETTI_N];
static int       s_confetti_vx[CONFETTI_N];

static void start_round(void);
static void enter_party(void);

// ── 小工具 ───────────────────────────────────────────────────────────
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

// ── 提示卡(没认到摇杆时显示,摇杆 + 插头图,通用形态)──────────────────────
static void make_hint_card(lv_obj_t *scr)
{
    lv_obj_t *card = plain(scr, 132, 76, 0xFFFFFF, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *box = outline(card, 34, 34, 0x9C9AD0, 4, 8);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *stick = plain(box, 10, 10, 0x9C9AD0, LV_RADIUS_CIRCLE);
    lv_obj_align(stick, LV_ALIGN_CENTER, 5, -5);   // 偏心圆点示意"推杆"
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

// ── 派对彩纸 ─────────────────────────────────────────────────────────
static void confetti_show(bool show)
{
    bsp_display_lock(0);
    for (int i = 0; i < CONFETTI_N; i++) {
        if (show) {
            s_confetti_x[i] = (float)(esp_random() % (SCREEN_W - 8));
            s_confetti_y[i] = -8.0f - (float)(esp_random() % 40);
            s_confetti_vx[i] = (int)(esp_random() % 3) - 1;
            uint8_t r, g, b;
            bus_link_hue2rgb((int)(esp_random() % 360), &r, &g, &b);
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

// ── 乘客子状态转换(纯逻辑,不碰 LVGL;渲染统一在 render_all)──────────────
static void pax_enter_waiting(int i, int stop_idx, int dest_house, int look)
{
    passenger_t *p = &s_pax[i];
    p->state        = PSG_WAITING;
    p->state_frames = 0;
    p->stop_idx     = stop_idx;
    p->dest_house   = dest_house;
    p->look         = look;
    p->wiggle_frames = 0;
    vec2_t sp = town_stop_point(stop_idx);
    p->x = sp.x; p->y = sp.y;
    sprites_passenger_dress(&p->sprite, look);
    sprites_bubble_set_color(&p->bubble, town_house_color(dest_house));
}

static void pax_enter_boarding(int i)
{
    passenger_t *p = &s_pax[i];
    vec2_t sp = town_stop_point(p->stop_idx);
    p->from_x = sp.x;   p->from_y = sp.y;
    p->to_x   = s_bus_x; p->to_y  = s_bus_y;   // 快照当前巴士位置(全程 ~350ms,车会移动少许,可接受)
    p->state  = PSG_BOARDING;
    p->state_frames = 0;
    s_carry = i;
    feedback_emit_pickup(town_house_color(p->dest_house));
}

static void pax_enter_riding(passenger_t *p)
{
    p->state = PSG_RIDING;
    p->state_frames = 0;
}

static void pax_enter_alighting(int i)
{
    passenger_t *p = &s_pax[i];
    vec2_t hc = town_house_center(p->dest_house);
    p->from_x = s_bus_x; p->from_y = s_bus_y;
    p->to_x   = hc.x;    p->to_y   = hc.y;
    p->state  = PSG_ALIGHTING;
    p->state_frames = 0;
    feedback_emit_deliver();
}

static void pax_enter_home(int i)
{
    passenger_t *p = &s_pax[i];
    p->state = PSG_HOME;
    town_house_set_lit(p->dest_house, true);
    s_carry = -1;
    s_delivered_count++;
}

// ── 乘客子状态每帧推进 ───────────────────────────────────────────────
static void pax_tick_waiting(passenger_t *p)
{
    p->state_frames++;
    vec2_t sp = town_stop_point(p->stop_idx);
    float wob = ((p->state_frames / 3) & 1) ? 1.5f : 0.0f;   // ~氛围档小晃(§7)
    if (p->wiggle_frames > 0) {
        p->wiggle_frames--;
        wob += (p->wiggle_frames & 1) ? 3.0f : -3.0f;        // 喇叭回应的额外摆动
    }
    p->x = sp.x;
    p->y = sp.y - wob;
}

static void pax_tick_boarding(passenger_t *p)
{
    p->state_frames++;
    int total = HOP_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)p->state_frames / total, 0.f, 1.f);
    p->x = p->from_x + (p->to_x - p->from_x) * t;
    p->y = p->from_y + (p->to_y - p->from_y) * t - 6.0f * sinf(t * (float)M_PI);   // 小跳弧线
    if (p->state_frames >= total) pax_enter_riding(p);
}

static void pax_tick_alighting(passenger_t *p)
{
    p->state_frames++;
    int total = HOP_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)p->state_frames / total, 0.f, 1.f);
    p->x = p->from_x + (p->to_x - p->from_x) * t;
    p->y = p->from_y + (p->to_y - p->from_y) * t - 6.0f * sinf(t * (float)M_PI);
    if (p->state_frames >= total) pax_enter_home((int)(p - s_pax));
}

static void pax_tick(int i)
{
    passenger_t *p = &s_pax[i];
    switch (p->state) {
        case PSG_WAITING:   pax_tick_waiting(p);   break;
        case PSG_BOARDING:  pax_tick_boarding(p);  break;
        case PSG_ALIGHTING: pax_tick_alighting(p); break;
        default: break;   // RIDING/HOME/NONE 无需逐帧数学(RIDING 位置直接读巴士)
    }
}

// ── 一轮:洗牌站牌 + 三栋房各一(不重复)─────────────────────────────────
static void start_round(void)
{
    int order[TOWN_STOPS];
    for (int i = 0; i < TOWN_STOPS; i++) order[i] = i;
    for (int i = TOWN_STOPS - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    int houses[TOWN_HOUSES] = { 0, 1, 2 };
    for (int i = TOWN_HOUSES - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int t = houses[i]; houses[i] = houses[j]; houses[j] = t;
    }

    for (int i = 0; i < TOWN_HOUSES; i++) town_house_set_lit(i, false);
    for (int i = 0; i < PASSENGERS_PER_ROUND; i++) {
        int look = (int)(esp_random() % PASSENGER_LOOKS);
        pax_enter_waiting(i, order[i], houses[i], look);
    }

    s_delivered_count = 0;
    s_carry = -1;
}

// ── 巴士:驱动 / 碰撞滑行 / 朝向迟滞 ─────────────────────────────────────
static void update_drive(void)
{
    float jx = bus_link_joy_attached() ? bus_link_joy_x() : 0.f;
    float jy = bus_link_joy_attached() ? bus_link_joy_y() : 0.f;
    float tvx, tvy;

#if BUS_DRIVE_MODE == 0
    // 死区内不给速度,否则摇杆居中残余偏移(机械回中误差/ADC 噪声)会被直接乘进
    // 目标速度、经 EMA 收敛成一个稳定的非零漂移——静置也会缓慢滑走。
    // 死区外按比例重新映射到 0..BUS_SPEED_MAX,避免死区边界处速度跳变。
    float mag = sqrtf(jx * jx + jy * jy);
    if (mag > JOY_DEADZONE) {
        float s = BUS_SPEED_MAX * (mag - JOY_DEADZONE) / (1.0f - JOY_DEADZONE) / mag;
        tvx = jx * s; tvy = jy * s;
    } else {
        tvx = 0; tvy = 0;
    }
#else
    float mag = sqrtf(jx * jx + jy * jy);
    if (mag > JOY_DEADZONE) {
        float s = BUS_SPEED_MAX * BUS_CONST_SPEED_PCT / 100.0f / mag;
        tvx = jx * s; tvy = jy * s;
    } else {
        tvx = 0; tvy = 0;
    }
#endif

    float k = JOY_EMA_PCT / 100.0f;
    s_bus_vx = s_bus_vx * (1 - k) + tvx * k;
    s_bus_vy = s_bus_vy * (1 - k) + tvy * k;

    float dt = POLL_PERIOD_MS / 1000.0f;
    s_bus_x += s_bus_vx * dt;
    s_bus_y += s_bus_vy * dt;

    float speed = sqrtf(s_bus_vx * s_bus_vx + s_bus_vy * s_bus_vy);
    if (speed > HEADING_MIN_SPEED_PX) {
        float ang = atan2f(s_bus_vy, s_bus_vx) * 180.0f / (float)M_PI;
        if (ang < 0) ang += 360.0f;
        float sector = 360.0f / HEADING_COUNT;
        float center = s_heading * sector;
        float diff = ang - center;
        while (diff > 180.0f)  diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;
        if (fabsf(diff) > sector / 2.0f + HEADING_HYST_DEG) {
            int raw = ((int)floorf(ang / sector + 0.5f)) % HEADING_COUNT;
            if (raw < 0) raw += HEADING_COUNT;
            s_heading = raw;
        }
    }
}

static void resolve_collisions(void)
{
    bool hit = false;

    int n;
    const circle_t *obs = town_obstacles(&n);
    for (int i = 0; i < n; i++) {
        float dx = s_bus_x - obs[i].cx, dy = s_bus_y - obs[i].cy;
        float rr = BUS_R + obs[i].r;
        float d2 = dx * dx + dy * dy;
        if (d2 >= rr * rr) continue;
        float d = sqrtf(d2);
        float nx, ny;
        if (d < 0.001f) { nx = 1; ny = 0; d = 0; } else { nx = dx / d; ny = dy / d; }
        float push = rr - d;
        s_bus_x += nx * push; s_bus_y += ny * push;
        float vn = s_bus_vx * nx + s_bus_vy * ny;
        if (vn < 0) { s_bus_vx -= vn * nx; s_bus_vy -= vn * ny; }
        hit = true;
    }

    for (int i = 0; i < TOWN_HOUSES; i++) {
        rect_t b = town_house_body(i);
        float cx = clampf(s_bus_x, b.x, b.x + b.w);
        float cy = clampf(s_bus_y, b.y, b.y + b.h);
        float dx = s_bus_x - cx, dy = s_bus_y - cy;
        float d2 = dx * dx + dy * dy;
        if (d2 >= BUS_R * BUS_R) continue;
        float d = sqrtf(d2);
        float nx, ny;
        if (d < 0.001f) {
            float left = s_bus_x - b.x, right = (b.x + b.w) - s_bus_x;
            float top  = s_bus_y - b.y, bottom = (b.y + b.h) - s_bus_y;
            float m = fminf(fminf(left, right), fminf(top, bottom));
            if      (m == left)  { nx = -1; ny = 0; }
            else if (m == right) { nx =  1; ny = 0; }
            else if (m == top)   { nx = 0; ny = -1; }
            else                 { nx = 0; ny =  1; }
            d = 0;
        } else { nx = dx / d; ny = dy / d; }
        float push = BUS_R - d;
        s_bus_x += nx * push; s_bus_y += ny * push;
        float vn = s_bus_vx * nx + s_bus_vy * ny;
        if (vn < 0) { s_bus_vx -= vn * nx; s_bus_vy -= vn * ny; }
        hit = true;
    }

    // 树篱围边(屏边界),留 6px 装饰边距
    const float minx = BUS_R + 6, maxx = SCREEN_W - BUS_R - 6;
    const float miny = BUS_R + 6, maxy = SCREEN_H - BUS_R - 6;
    if (s_bus_x < minx) { s_bus_x = minx; if (s_bus_vx < 0) s_bus_vx = 0; hit = true; }
    if (s_bus_x > maxx) { s_bus_x = maxx; if (s_bus_vx > 0) s_bus_vx = 0; hit = true; }
    if (s_bus_y < miny) { s_bus_y = miny; if (s_bus_vy < 0) s_bus_vy = 0; hit = true; }
    if (s_bus_y > maxy) { s_bus_y = maxy; if (s_bus_vy > 0) s_bus_vy = 0; hit = true; }

    if (hit && s_bump_cooldown <= 0) {
        feedback_emit_bump();
        s_squash = 1.0f;
        s_bump_cooldown = BUMP_SND_COOLDOWN_MS / POLL_PERIOD_MS;
    }
}

// ── 接客 / 送达 / 错门(SPEC §5)──────────────────────────────────────
static void check_pickup(void)
{
    if (s_carry != -1) return;
    for (int i = 0; i < PASSENGERS_PER_ROUND; i++) {
        if (s_pax[i].state != PSG_WAITING) continue;
        vec2_t sp = town_stop_point(s_pax[i].stop_idx);
        float dx = s_bus_x - sp.x, dy = s_bus_y - sp.y;
        if (dx * dx + dy * dy < (float)PICKUP_RADIUS_PX * PICKUP_RADIUS_PX) {
            pax_enter_boarding(i);
            return;
        }
    }
}

static void check_delivery(void)
{
    if (s_carry == -1) return;
    passenger_t *p = &s_pax[s_carry];
    if (p->state != PSG_RIDING) return;

    for (int h = 0; h < TOWN_HOUSES; h++) {
        rect_t d = town_house_door(h);
        if (s_bus_x < d.x || s_bus_x > d.x + d.w || s_bus_y < d.y || s_bus_y > d.y + d.h) continue;
        if (h == p->dest_house) {
            pax_enter_alighting(s_carry);
        } else if (s_wrong_door_cooldown <= 0) {
            town_house_blink(h);
            feedback_emit_wrong_door();
            s_wrong_door_cooldown = WRONG_DOOR_SND_COOLDOWN_MS / POLL_PERIOD_MS;
        }
        return;   // 门垫几何互不重叠,命中一家即返回
    }
}

// ── 喇叭(按键上升沿,附近等待中的乘客挥手)───────────────────────────────
static void handle_honk(void)
{
    bool btn = bus_link_joy_attached() && bus_link_joy_button();
    bool edge = btn && !s_prev_joy_btn;
    s_prev_joy_btn = btn;
    if (s_honk_cooldown > 0 || !edge) return;

    s_honk_cooldown = HONK_COOLDOWN_MS / POLL_PERIOD_MS;
    s_pop = 1.0f;

    bool near_someone = false;
    for (int i = 0; i < PASSENGERS_PER_ROUND; i++) {
        if (s_pax[i].state != PSG_WAITING) continue;
        vec2_t sp = town_stop_point(s_pax[i].stop_idx);
        float dx = s_bus_x - sp.x, dy = s_bus_y - sp.y;
        if (dx * dx + dy * dy < (float)HONK_WAVE_RADIUS_PX * HONK_WAVE_RADIUS_PX) {
            near_someone = true;
            s_pax[i].wiggle_frames = 4;
        }
    }
    feedback_emit_honk(near_someone);
}

// ── 派对 ─────────────────────────────────────────────────────────────
static void enter_party(void)
{
    s_round_state  = RS_PARTY;
    s_party_frames = 0;
    for (int i = 0; i < TOWN_HOUSES; i++) town_house_set_lit(i, true);
    feedback_emit_party();
    confetti_show(true);
}

static void tick_party(void)
{
    s_party_frames++;
    int total = PARTY_HOLD_MS / POLL_PERIOD_MS; if (total < 1) total = 1;

    int hue = (s_party_frames * 9) % 360;
    uint8_t r, g, b;
    bus_link_hue2rgb(hue, &r, &g, &b);
    bus_link_joy_rgb(r, g, b);

    tick_confetti_fall();

    if (s_party_frames >= total) {
        confetti_show(false);
        s_round_state = RS_PLAY;
        start_round();
    }
}

// 摇杆节点 RGB 常态基准(§6"行驶"行:空车=暖白微亮 / 载客=乘客目的地色);每帧写一次,
// 顺带把 EV_PICKUP/EV_HONK 等异步事件写的瞬时色(闪白/乘客色)在下一帧自然带回常态。
static void sync_joy_idle_rgb(void)
{
    if (s_carry != -1) {
        uint32_t c = town_house_color(s_pax[s_carry].dest_house);
        bus_link_joy_rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    } else {
        bus_link_joy_rgb(255, 230, 200);
    }
}

// ── 渲染(唯一触碰 LVGL 的地方,单把锁,SPEC §7 预算合规)────────────────────
static void render_all(void)
{
    bsp_display_lock(0);

    int sz = sprites_bus_size();
    lv_image_set_src(s_bus_img, sprites_bus(s_heading));
    lv_obj_set_pos(s_bus_img, (int)(s_bus_x - sz / 2.0f), (int)(s_bus_y - sz / 2.0f));
    float scale = 1.0f + 0.22f * s_pop - 0.18f * s_squash;
    lv_obj_set_style_transform_scale_x(s_bus_img, (int)(LV_SCALE_NONE * scale), 0);
    lv_obj_set_style_transform_scale_y(s_bus_img, (int)(LV_SCALE_NONE * scale), 0);
    s_squash *= 0.8f; if (s_squash < 0.02f) s_squash = 0;
    s_pop    *= 0.8f; if (s_pop    < 0.02f) s_pop    = 0;

    for (int i = 0; i < PASSENGERS_PER_ROUND; i++) {
        passenger_t *p = &s_pax[i];
        bool body_visible   = (p->state == PSG_WAITING || p->state == PSG_BOARDING || p->state == PSG_ALIGHTING);
        bool bubble_visible = (p->state != PSG_NONE && p->state != PSG_HOME);

        if (body_visible) {
            lv_obj_set_pos(p->sprite.container, (int)(p->x - PASSENGER_W / 2.0f), (int)(p->y - PASSENGER_H / 2.0f));
            lv_obj_remove_flag(p->sprite.container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(p->sprite.container, LV_OBJ_FLAG_HIDDEN);
        }

        if (bubble_visible) {
            float bx, by;
            if (p->state == PSG_RIDING) { bx = s_bus_x; by = s_bus_y - sz / 2.0f - BUBBLE_H / 2.0f - 4.0f; }
            else                        { bx = p->x;    by = p->y - PASSENGER_H / 2.0f - BUBBLE_H / 2.0f - 2.0f; }
            lv_obj_set_pos(p->bubble.container, (int)(bx - BUBBLE_W / 2.0f), (int)(by - BUBBLE_H / 2.0f));
            lv_obj_remove_flag(p->bubble.container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(p->bubble.container, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_round_state == RS_PARTY) {
        for (int i = 0; i < CONFETTI_N; i++) {
            lv_obj_set_pos(s_confetti[i], (int)s_confetti_x[i], (int)s_confetti_y[i]);
        }
    }

    bsp_display_unlock();
}

// ── 对外接口 ─────────────────────────────────────────────────────────
void bus_game_create(void)
{
    sprites_bake_bus();   // 纯 CPU,一次性,无需持锁

    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    town_create(scr);

    int sz = sprites_bus_size();
    s_bus_img = lv_image_create(scr);
    lv_image_set_src(s_bus_img, sprites_bus(0));
    lv_obj_set_style_transform_pivot_x(s_bus_img, sz / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_bus_img, sz / 2, 0);

    for (int i = 0; i < PASSENGERS_PER_ROUND; i++) {
        sprites_passenger_create(scr, &s_pax[i].sprite);
        sprites_bubble_create(scr, &s_pax[i].bubble);
    }

    for (int i = 0; i < CONFETTI_N; i++) {
        lv_obj_t *c = plain(scr, 6, 6, 0xFFFFFF, 2);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        s_confetti[i] = c;
    }

    make_hint_card(scr);

    bsp_display_unlock();

    vec2_t g = town_garage_pos();
    s_bus_x = g.x; s_bus_y = g.y;
    s_bus_vx = s_bus_vy = 0;
    s_heading = 0;
    s_round_state = RS_PLAY;

    start_round();
    render_all();
}

void bus_game_tick(void)
{
    if (s_bump_cooldown > 0)       s_bump_cooldown--;
    if (s_honk_cooldown > 0)       s_honk_cooldown--;
    if (s_wrong_door_cooldown > 0) s_wrong_door_cooldown--;

    if (s_round_state == RS_PARTY) {
        tick_party();
    } else {
        handle_honk();
        update_drive();
        resolve_collisions();
        check_pickup();
        check_delivery();
        for (int i = 0; i < PASSENGERS_PER_ROUND; i++) pax_tick(i);
        sync_joy_idle_rgb();
        if (s_delivered_count >= PASSENGERS_PER_ROUND) enter_party();
    }

    render_all();
}

void bus_game_sync_attach(void)
{
    hint_card_apply(!bus_link_joy_attached());
}

void bus_game_reset_position(void)
{
    vec2_t g = town_garage_pos();
    s_bus_x = g.x; s_bus_y = g.y;
    s_bus_vx = s_bus_vy = 0;
    s_heading = 0;
    s_squash = 0; s_pop = 0;
    render_all();
}
