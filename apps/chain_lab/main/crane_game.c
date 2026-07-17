// 抓娃娃机 —— 游戏层实现(SPEC.md §3~§10;v2.1 深度分层迭代,SPEC §11②)
//
// 状态机:PLAY_IDLE ⇄ DESCENDING → GRABBING → ASCENDING → DEPOSIT → PLAY_IDLE
//        (展示架集满 → PARTY → 重置 → PLAY_IDLE)。
// 输入:摇杆 X 直接映射吊臂横坐标(始终可控,不受状态门控);编码器帧间 delta 只在
//      PLAY_IDLE/DESCENDING 时驱动深度,GRABBING/ASCENDING/DEPOSIT/PARTY 期间吃掉输入。
// 深度=选择器:战利品分住 PIT_LAYERS 个深度层(高低底座),抓取成败 = 横向对齐 + 深度层
//      匹配双条件;爪子碰到战利品(双条件都满足)有多通道"对上了"提示,此时按抓取键收爪,
//      或碰住不再转曲柄 TOUCH_DWELL_MS 自动收爪(不按键兜底);转到底照旧自动收爪。
// v2.2 趣味批:战利品升级为有脸玩偶(PRIZE_LOOK 表,SPEC §11④ 落地);爪子两指开合造型;
//      拧曲柄每格轻"咔"(音调随深度下沉);金星彩蛋(正常收获后有概率在空坑最深层冒出,
//      抓到小庆祝、不占展示架——给"转到最深"一个主动追求的理由)。
#include "crane_game.h"

#include <math.h>
#include <stdlib.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "chain_lab.h"
#include "haptics.h"
#include "ledstrip_fx.h"

#include "tuning.h"

#define SCREEN_W 320
#define SCREEN_H 240

// ── 配色(暖色家族,同诊断台延续)────────────────────────────────────────
#define BG_COL           0xEAE6DA
#define PANEL_COL        0xF3EEE0
#define RACK_BG_COL      0xFBF7EE
#define RAIL_COL         0xB8AF9C
#define PEDESTAL_COL     0xDCD3C0
#define ARM_COL          0x8A5A32
#define CABLE_COL        0x6B6357
#define CLAW_COL         0xFB8B24
#define UNCOLLECTED_BASE   0xCFC6B8
#define UNCOLLECTED_ACCENT 0xB8B0A2

// ── 布局(px,§9 机制常量以外的纯视觉几何;精确值待实机标定)───────────────
#define CAB_Y0        36
#define CAB_Y1        236
#define RAIL_Y        (CAB_Y0 + 10)
#define ARM_X_MIN     (SCREEN_W / 2 - CRANE_X_RANGE_PX / 2)
#define ARM_X_MAX     (SCREEN_W / 2 + CRANE_X_RANGE_PX / 2)
#define ARM_W         30
#define ARM_H         18
#define CABLE_W       4
#define CLAW_OPEN_W   26
#define CLAW_CLOSED_W 12
#define CLAW_H        14
#define PRIZE_SZ      30
#define PRIZE_HELD_SZ 26
#define PEDESTAL_W    18
#define PEDESTAL_MIN_H 6              // 底座矮于此就不画(最深层贴地)
#define DOLL_HEADROOM 14              // 玩偶容器在身体上方留的头饰空间(耳朵/呆毛)
#define DOLL_PAD_X    2               // 容器左右内边距(熊耳/金星光球略宽于身体,防裁切)
#define EYE_SZ        5
#define EYE_COL       0x453A2C
#define CLAW_BAR_H    5               // 爪子横梁高度(两指挂在横梁下)
#define PRONG_W       6               // 爪指宽度

// 层 → 爪子恰好咬合该层战利品的目标深度(最深层 = DESCEND_MAX_PX,与"转到底自动抓"吻合)
#define LAYER_DEPTH(L) (DESCEND_MAX_PX - (PIT_LAYERS - 1 - (L)) * PIT_LAYER_STEP_PX)
// 层 → 战利品顶部 y(命中时爪底 = RAIL_Y+ARM_H+CLAW_H+深度,压入战利品 GRAB_OVERLAP_PX)
#define PRIZE_TOP_Y(L) (RAIL_Y + ARM_H + CLAW_H - GRAB_OVERLAP_PX + LAYER_DEPTH(L))

#define RACK_Y        4
#define RACK_SLOT     28
#define RACK_GAP      6
#define RACK_X0       ((SCREEN_W - (PRIZE_TYPES * RACK_SLOT + (PRIZE_TYPES - 1) * RACK_GAP)) / 2)

#define DEPTH_BANDS   5
#define CONFETTI_N    8
#define WIGGLE_FRAMES 4

// ── 战利品配色 + 玩偶造型表(SPEC §11④ 落地;第 6 项 = 金星彩蛋)──────────────
#define KIND_GOLDEN PRIZE_TYPES   // 金星彩蛋的 kind(不占展示架,不参与集满判定)

typedef struct { uint32_t base, accent; } prize_style_t;
static const prize_style_t PRIZE_STYLE[PRIZE_TYPES + 1] = {
    { 0xE6533C, 0xFFD9CE },   // 珊瑚红·小熊
    { 0x4FB0D8, 0xDFF3FA },   // 天蓝·呆毛
    { 0xF5C242, 0xFFF3B0 },   // 暖黄·小鸡
    { 0x6FBF73, 0xE1F5DE },   // 草绿·青蛙
    { 0xC77DD1, 0xF3E1F7 },   // 紫粉·兔子
    { 0xF6C445, 0xFFF3C2 },   // 金星(彩蛋)
};

// 玩偶造型:身体圆 + 特征件 A/B(耳/呆毛/凸眼底)+ 双眼 + 嘴/喙,全部 plain 色块。
// 坐标相对身体左上角(可为负,容器统一加 DOLL_HEADROOM 偏移);w=0 表示该件不显示。
typedef struct {
    int8_t   aw, ah, ax, ay;    // 特征件 A
    int8_t   bw, bh, bx, by;    // 特征件 B
    uint32_t part_col;          // A/B 颜色
    int8_t   elx, erx, ey;      // 双眼左/右 x 与共同 y(EYE_SZ 圆点)
    int8_t   mw, mh, mx, my;    // 嘴/喙
    uint32_t mouth_col;
} prize_look_t;

static const prize_look_t PRIZE_LOOK[PRIZE_TYPES + 1] = {
    { 10,10,-2,-6,  10,10,22,-6,  0xC93A26,  7,18,12,  8,4,11,19,  0x8A2B1C },  // 熊:圆耳×2
    {  9, 9,10,-6,   0, 0, 0, 0,  0x2E86B0,  7,18,12,  7,3,11,20,  0x1F6E96 },  // 呆毛:头顶单圆
    {  7, 7,11,-5,   0, 0, 0, 0,  0xE6533C,  7,18,11,  9,5,10,16,  0xF08A24 },  // 小鸡:红冠+橙喙
    { 11,11, 1,-7,  11,11,18,-7,  0xFFFFFF,  4,21,-4, 10,3,10,17,  0x3F7A44 },  // 青蛙:白凸眼×2
    {  8,16, 4,-13,  8,16,18,-13, 0xE3BCEB,  7,18,12,  6,3,12,19,  0x7A4184 },  // 兔:长耳×2
    {  8, 8,-1,-4,   8, 8,23,-4,  0xFFE58A,  7,18,12,  6,3,12,19,  0xC08A20 },  // 金星:侧光球×2
};

// 下降深度 → 编码器节点 RGB(浅→深,离散几档,不逐帧渐变,SPEC §4)
typedef struct { uint8_t r, g, b; } rgb8_t;
static const rgb8_t DEPTH_COLOR[DEPTH_BANDS] = {
    { 0x9F, 0xE6, 0xD2 },
    { 0x6F, 0xCE, 0xB2 },
    { 0x3D, 0xAE, 0x93 },
    { 0x22, 0x86, 0x71 },
    { 0x11, 0x5F, 0x50 },
};

typedef enum {
    CRANE_PLAY_IDLE = 0,
    CRANE_DESCENDING,
    CRANE_GRABBING,
    CRANE_ASCENDING,
    CRANE_DEPOSIT,
    CRANE_PARTY,
} crane_state_t;

// ── 状态 ─────────────────────────────────────────────────────────────
static crane_state_t s_state = CRANE_PLAY_IDLE;
static int  s_state_frames;

static int  s_arm_x = SCREEN_W / 2;
static int  s_depth;
static int  s_claw_w = CLAW_OPEN_W;
static int  s_bounce_dy;
static int  s_wiggle_dx;
static int  s_wiggle_frames;

static int  s_grabbed_slot = -1;
static int  s_ascend_from_depth;
static int  s_deposit_from_x, s_deposit_from_y, s_deposit_to_x, s_deposit_to_y;

static bool s_pit_present[PRIZE_TYPES];
static int  s_pit_kind[PRIZE_TYPES];
static int  s_pit_layer[PRIZE_TYPES];
static int  s_pit_slot_x[PRIZE_TYPES];

static int  s_touch_slot = -1;   // 爪子当前碰到的坑位(横向对齐 + 深度层匹配),-1 = 没碰到
static int  s_touch_frames;      // 碰住且不转曲柄的持续帧数(dwell 自动收爪用)
static int  s_crank_cool;        // 曲柄"咔"声节流倒计时(帧)

static bool s_rack_got[PRIZE_TYPES];
static int  s_rack_slot_x[PRIZE_TYPES];
static int  s_rack_count;

static int16_t s_prev_enc_value;
static bool    s_prev_enc_btn;
static bool    s_prev_joy_btn;
static bool    s_enc_was_attached;
static bool    s_hint_shown;

static int  s_led_base = -1;
static int  s_confetti_vx[CONFETTI_N];

// ── LVGL 对象 ────────────────────────────────────────────────────────
static lv_obj_t *s_arm, *s_cable;
static lv_obj_t *s_claw_bar, *s_claw_l, *s_claw_r;   // 爪子:横梁 + 左右两指
static lv_obj_t *s_pit_pedestal[PRIZE_TYPES];
static lv_obj_t *s_pit_doll[PRIZE_TYPES];            // 玩偶容器(整只一起移动/隐藏)
static lv_obj_t *s_pit_body[PRIZE_TYPES], *s_pit_parta[PRIZE_TYPES], *s_pit_partb[PRIZE_TYPES];
static lv_obj_t *s_pit_eyel[PRIZE_TYPES], *s_pit_eyer[PRIZE_TYPES], *s_pit_mouth[PRIZE_TYPES];
static lv_obj_t *s_rack_body[PRIZE_TYPES], *s_rack_accent[PRIZE_TYPES];
static lv_obj_t *s_held_body;                        // 被抓玩偶(身体 + 双眼子对象)
static lv_obj_t *s_confetti[CONFETTI_N];
static lv_obj_t *s_hint_card;

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

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void led_base_set(led_base_t b)
{
    if ((int)b == s_led_base) return;
    s_led_base = (int)b;
    ledstrip_fx_set_base(b);
}

static void set_depth_led(int depth)
{
    int band = (depth * DEPTH_BANDS) / (DESCEND_MAX_PX + 1);
    if (band < 0) band = 0;
    if (band >= DEPTH_BANDS) band = DEPTH_BANDS - 1;
    chain_lab_enc_rgb(DEPTH_COLOR[band].r, DEPTH_COLOR[band].g, DEPTH_COLOR[band].b);
}

// ── 战利品坑 / 展示架 / 跟随精灵 ─────────────────────────────────────────
// 玩偶零件通用装扮:w<=0 隐藏;圆件(w==h)用圆角圆,长件(兔耳)用小圆角
static void doll_part(lv_obj_t *o, int w, int h, int x, int y, uint32_t col)
{
    if (w <= 0) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x + DOLL_PAD_X, y + DOLL_HEADROOM);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_radius(o, (w == h) ? LV_RADIUS_CIRCLE : 3, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// 按 kind 装扮玩偶(身体+特征件+眼+嘴)+ 按 layer 摆位(玩偶 + 底座);调用方持有 bsp_display_lock
static void pit_slot_paint(int i)
{
    int kind = s_pit_kind[i];
    int top  = PRIZE_TOP_Y(s_pit_layer[i]);
    const prize_look_t *lk = &PRIZE_LOOK[kind];

    lv_obj_set_style_bg_color(s_pit_body[i], lv_color_hex(PRIZE_STYLE[kind].base), 0);
    doll_part(s_pit_parta[i], lk->aw, lk->ah, lk->ax, lk->ay, lk->part_col);
    doll_part(s_pit_partb[i], lk->bw, lk->bh, lk->bx, lk->by, lk->part_col);
    doll_part(s_pit_eyel[i], EYE_SZ, EYE_SZ, lk->elx, lk->ey, EYE_COL);
    doll_part(s_pit_eyer[i], EYE_SZ, EYE_SZ, lk->erx, lk->ey, EYE_COL);
    doll_part(s_pit_mouth[i], lk->mw, lk->mh, lk->mx, lk->my, lk->mouth_col);
    lv_obj_set_pos(s_pit_doll[i], s_pit_slot_x[i] - PRIZE_SZ / 2 - DOLL_PAD_X, top - DOLL_HEADROOM);
    lv_obj_remove_flag(s_pit_doll[i], LV_OBJ_FLAG_HIDDEN);

    int ped_top = top + PRIZE_SZ - 2;   // 与玩偶底重叠 2px 防缝
    int ped_h   = CAB_Y1 - ped_top;
    if (ped_h >= PEDESTAL_MIN_H) {
        lv_obj_set_size(s_pit_pedestal[i], PEDESTAL_W, ped_h);
        lv_obj_set_pos(s_pit_pedestal[i], s_pit_slot_x[i] - PEDESTAL_W / 2, ped_top);
        lv_obj_remove_flag(s_pit_pedestal[i], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pit_pedestal[i], LV_OBJ_FLAG_HIDDEN);   // 最深层贴地,无底座
    }
}

static void pit_slot_show(int i, bool show)
{
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_pit_doll[i], LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_pit_doll[i], LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

static void held_prize_show(int kind, bool show)
{
    bsp_display_lock(0);
    if (show) {
        lv_obj_set_style_bg_color(s_held_body, lv_color_hex(PRIZE_STYLE[kind].base), 0);
        lv_obj_remove_flag(s_held_body, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_held_body, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

// cx/cy = 目标中心坐标(供上升跟随 / 落架飞行插值复用);双眼是子对象,随身体走
static void position_held_prize_at(int cx, int cy)
{
    bsp_display_lock(0);
    lv_obj_set_pos(s_held_body, cx - PRIZE_HELD_SZ / 2, cy - PRIZE_HELD_SZ / 2);
    bsp_display_unlock();
}

static void mark_rack_collected(int kind)
{
    if (kind < 0 || kind >= PRIZE_TYPES || s_rack_got[kind]) return;
    s_rack_got[kind] = true;
    s_rack_count++;
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_rack_body[kind], lv_color_hex(PRIZE_STYLE[kind].base), 0);
    lv_obj_set_style_bg_color(s_rack_accent[kind], lv_color_hex(PRIZE_STYLE[kind].accent), 0);
    bsp_display_unlock();
}

// Fisher-Yates 洗一批新战利品坑位(5 种各一,顺序随机);层分配保证浅/中/深都有
static void shuffle_pit(void)
{
    int order[PRIZE_TYPES], layer[PRIZE_TYPES];
    for (int i = 0; i < PRIZE_TYPES; i++) {
        order[i] = i;
        layer[i] = (i < PIT_LAYERS) ? i : (int)(esp_random() % PIT_LAYERS);
    }
    for (int i = PRIZE_TYPES - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
        j = (int)(esp_random() % (uint32_t)(i + 1));
        t = layer[i]; layer[i] = layer[j]; layer[j] = t;
    }
    for (int i = 0; i < PRIZE_TYPES; i++) {
        s_pit_kind[i]    = order[i];
        s_pit_layer[i]   = layer[i];
        s_pit_present[i] = true;
    }
}

static void reset_round(void)
{
    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) {
        s_rack_got[i] = false;
        lv_obj_set_style_bg_color(s_rack_body[i], lv_color_hex(UNCOLLECTED_BASE), 0);
        lv_obj_set_style_bg_color(s_rack_accent[i], lv_color_hex(UNCOLLECTED_ACCENT), 0);
        lv_obj_set_y(s_rack_body[i], RACK_Y);
        lv_obj_set_y(s_rack_accent[i], RACK_Y + 3);
    }
    bsp_display_unlock();
    s_rack_count = 0;

    shuffle_pit();
    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) pit_slot_paint(i);
    bsp_display_unlock();
}

// ── 无字提示卡(没认到任何 Chain 节点时显示,SPEC §3 "沿用现状逻辑")───────────
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
            int x = (int)(esp_random() % (SCREEN_W - 8));
            int y = RACK_Y + RACK_SLOT + 4 + (int)(esp_random() % 60);
            uint8_t r, g, b;
            chain_lab_hue2rgb((int)(esp_random() % 360), &r, &g, &b);
            lv_obj_set_pos(s_confetti[i], x, y);
            lv_obj_set_style_bg_color(s_confetti[i], lv_color_make(r, g, b), 0);
            s_confetti_vx[i] = (int)(esp_random() % 3) - 1;   // -1..1
            lv_obj_remove_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_confetti[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    bsp_display_unlock();
}

// ── 摇杆彩蛋:抖一下爪子(不影响进度)──────────────────────────────────────
static void trigger_wiggle(void)
{
    s_wiggle_frames = WIGGLE_FRAMES;
    audio_fx_play(SND_BUMP_LIGHT);
}

static void tick_wiggle(void)
{
    if (s_wiggle_frames <= 0) { s_wiggle_dx = 0; return; }
    s_wiggle_frames--;
    s_wiggle_dx = (s_wiggle_frames & 1) ? 3 : -3;
    if (s_wiggle_frames == 0) s_wiggle_dx = 0;
}

// ── 坑位命中测试(横向中心点容差带 + 深度层容差带,双条件,SPEC §5)─────────────
static int find_pit_slot_at(int x, int depth)
{
    int best = -1, best_d = GRAB_ALIGN_TOL_PX + 1;
    for (int i = 0; i < PRIZE_TYPES; i++) {
        if (!s_pit_present[i]) continue;
        if (abs(depth - LAYER_DEPTH(s_pit_layer[i])) > GRAB_DEPTH_TOL_PX) continue;
        int d = abs(x - s_pit_slot_x[i]);
        if (d <= GRAB_ALIGN_TOL_PX && d < best_d) { best = i; best_d = d; }
    }
    return best;
}

// 碰触时玩偶的小幅摆动(dx=0 归位);不动底座,整只容器一起动
static void pit_prize_offset(int i, int dx)
{
    bsp_display_lock(0);
    lv_obj_set_x(s_pit_doll[i], s_pit_slot_x[i] - PRIZE_SZ / 2 - DOLL_PAD_X + dx);
    bsp_display_unlock();
}

// 金星彩蛋:正常战利品收进展示架后,概率在一个空坑位的最深层冒出(同屏最多一颗)
static void maybe_spawn_golden(void)
{
    for (int i = 0; i < PRIZE_TYPES; i++)
        if (s_pit_present[i] && s_pit_kind[i] == KIND_GOLDEN) return;
    if ((int)(esp_random() % 100) >= GOLDEN_CHANCE_PCT) return;

    int empties[PRIZE_TYPES], n = 0;
    for (int i = 0; i < PRIZE_TYPES; i++)
        if (!s_pit_present[i]) empties[n++] = i;
    if (n == 0) return;

    int slot = empties[esp_random() % (uint32_t)n];
    s_pit_kind[slot]    = KIND_GOLDEN;
    s_pit_layer[slot]   = PIT_LAYERS - 1;   // 金星只住最深层(SPEC §11② 深度激励)
    s_pit_present[slot] = true;
    bsp_display_lock(0);
    pit_slot_paint(slot);
    bsp_display_unlock();
    audio_fx_play(SND_NEAR);   // 出现提示(共用"接近"音签,轻)
}

// ── 状态转换 ─────────────────────────────────────────────────────────
static void enter_grabbing(void)
{
    if (s_touch_slot >= 0) pit_prize_offset(s_touch_slot, 0);   // 摆动归位再收爪
    s_touch_slot   = -1;
    s_touch_frames = 0;

    s_state = CRANE_GRABBING;
    s_state_frames = 0;
    s_grabbed_slot = find_pit_slot_at(s_arm_x, s_depth);

    led_base_set(LED_BASE_AMBIENT);
    chain_lab_enc_rgb(255, 255, 255);   // 闪白(复用现状按下逻辑,SPEC §4)
    audio_fx_play(SND_COLLECT);
    haptics_play(HAPTIC_BUMP_MED);
    ledstrip_fx_trigger(LED_FX_FLASH);

    if (s_grabbed_slot >= 0) {
        s_pit_present[s_grabbed_slot] = false;
        pit_slot_show(s_grabbed_slot, false);
        held_prize_show(s_pit_kind[s_grabbed_slot], true);
        position_held_prize_at(s_arm_x, RAIL_Y + ARM_H + s_depth + CLAW_H / 2);
    }
}

static void enter_ascending(void)
{
    s_state = CRANE_ASCENDING;
    s_state_frames = 0;
    s_ascend_from_depth = s_depth;

    if (s_grabbed_slot >= 0) {
        if (s_pit_kind[s_grabbed_slot] == KIND_GOLDEN)
            chain_lab_enc_rgb(255, 214, 64);   // 金星:上升途中亮金色
        else
            chain_lab_enc_rgb(20, 200, 90);    // 成功色:绿
        audio_fx_play_notes((audio_note_t[]){ { 659, 90, 45 }, { 880, 110, 50 } }, 2);
        haptics_play(HAPTIC_COLLECT);
        ledstrip_fx_trigger(LED_FX_COLLECT);
    } else {
        chain_lab_enc_rgb(255, 170, 40);   // 中性琥珀,不用红色(零失败,SPEC §5)
        audio_fx_play(SND_BUMP_LIGHT);
        haptics_play(HAPTIC_BUMP_LIGHT);
        ledstrip_fx_trigger(LED_FX_BUMP);
    }
}

static void enter_deposit(void)
{
    s_state = CRANE_DEPOSIT;
    s_state_frames = 0;

    if (s_grabbed_slot >= 0) {
        int kind = s_pit_kind[s_grabbed_slot];
        s_deposit_from_x = s_arm_x;
        s_deposit_from_y = RAIL_Y + ARM_H + CLAW_H / 2;
        s_deposit_to_x   = (kind == KIND_GOLDEN) ? SCREEN_W / 2   // 金星飞向展示架正中"绽放"
                                                 : s_rack_slot_x[kind] + RACK_SLOT / 2;
        s_deposit_to_y   = RACK_Y + RACK_SLOT / 2;
    }
}

static void enter_party(void)
{
    s_state = CRANE_PARTY;
    s_state_frames = 0;
    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
    confetti_show(true);
}

// 编码器每格增量应用到深度;顺时针+/逆时针-;接近底部自动到底并直接抓取(SPEC §5/§9)
static void apply_descend_delta(int delta)
{
    int nd = s_depth + delta * DESCEND_PER_TICK;
    if (nd < 0) nd = 0;
    if (nd > DESCEND_MAX_PX) nd = DESCEND_MAX_PX;
    bool moved = (nd != s_depth);
    s_depth = nd;
    s_touch_frames = 0;   // 还在转曲柄 = 还在选,dwell 重新计(只有停住才算"就要这个")

    if (moved && s_crank_cool <= 0) {   // 每格轻"咔"(SPEC §4),节流防连转噪音;越深音越低
        uint16_t f = (uint16_t)(1500 - s_depth * 4);
        audio_fx_play_notes((audio_note_t[]){ { f, 20, 35 } }, 1);
        s_crank_cool = CRANK_TICK_MIN_MS / POLL_PERIOD_MS;
    }

    if (s_depth >= DESCEND_MAX_PX - DESCEND_SNAP_TOL) {
        s_depth = DESCEND_MAX_PX;
        enter_grabbing();
        return;
    }

    s_state = (s_depth > 0) ? CRANE_DESCENDING : CRANE_PLAY_IDLE;
    if (s_touch_slot < 0) set_depth_led(s_depth);   // 碰触期间节点灯归 update_touch 管
}

// ── 碰触检测(每帧,PLAY_IDLE/DESCENDING):对上了 → 多通道提示 + dwell 自动收爪 ──
static void update_touch(void)
{
    if (s_state != CRANE_PLAY_IDLE && s_state != CRANE_DESCENDING) return;

    int cur = find_pit_slot_at(s_arm_x, s_depth);
    if (cur != s_touch_slot) {
        if (s_touch_slot >= 0) pit_prize_offset(s_touch_slot, 0);
        s_touch_slot   = cur;
        s_touch_frames = 0;
        if (cur >= 0) {   // 对上了:节点灯变战利品色 + 上扬叮铃 + 轻触感 + 灯带加亮
            uint32_t c = PRIZE_STYLE[s_pit_kind[cur]].base;
            chain_lab_enc_rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            audio_fx_play(SND_NEAR);
            haptics_play(HAPTIC_BUMP_LIGHT);
            led_base_set(LED_BASE_NEAR);
        } else {          // 离开:恢复深度色 + 常态微光
            set_depth_led(s_depth);
            led_base_set(LED_BASE_AMBIENT);
        }
        return;
    }
    if (cur < 0) return;

    s_touch_frames++;
    pit_prize_offset(cur, ((s_touch_frames >> 1) & 1) ? 2 : -2);   // ~5Hz 小幅摆动(氛围档)
    if (s_touch_frames >= TOUCH_DWELL_MS / POLL_PERIOD_MS) {
        enter_grabbing();   // 碰住不放 = 就要这个:自动收爪(不按键也能玩的兜底)
    }
}

// ── 每状态的每帧推进 ─────────────────────────────────────────────────────
static void tick_grabbing(void)
{
    s_state_frames++;
    int total = GRAB_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)s_state_frames / total, 0.f, 1.f);
    s_claw_w = CLAW_OPEN_W + (int)((CLAW_CLOSED_W - CLAW_OPEN_W) * t);

    if (s_grabbed_slot >= 0) {
        position_held_prize_at(s_arm_x, RAIL_Y + ARM_H + s_depth + CLAW_H / 2);
    }

    if (s_state_frames >= total) enter_ascending();
}

static void tick_ascending(void)
{
    s_state_frames++;
    int total = ASCEND_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)s_state_frames / total, 0.f, 1.f);
    s_depth = (int)(s_ascend_from_depth * (1.0f - t));

    if (s_grabbed_slot >= 0) {
        position_held_prize_at(s_arm_x, RAIL_Y + ARM_H + s_depth + CLAW_H / 2);
    }

    if (s_state_frames >= total) {
        s_depth  = 0;
        s_claw_w = CLAW_OPEN_W;
        enter_deposit();
    }
}

static void tick_deposit(void)
{
    s_state_frames++;
    int total = DEPOSIT_MS / POLL_PERIOD_MS; if (total < 1) total = 1;
    float t = clampf((float)s_state_frames / total, 0.f, 1.f);

    if (s_grabbed_slot >= 0) {
        int hx = s_deposit_from_x + (int)((s_deposit_to_x - s_deposit_from_x) * t);
        int hy = s_deposit_from_y + (int)((s_deposit_to_y - s_deposit_from_y) * t);
        position_held_prize_at(hx, hy);
    } else {
        float phase = (t < 0.5f) ? (t / 0.5f) : (1.0f - (t - 0.5f) / 0.5f);
        s_bounce_dy = -(int)(6 * phase);   // 空爪"诶?"弹一下,不是负面反馈(SPEC §5)
    }

    if (s_state_frames >= total) {
        s_bounce_dy = 0;
        if (s_grabbed_slot >= 0) {
            int kind = s_pit_kind[s_grabbed_slot];
            held_prize_show(kind, false);
            if (kind == KIND_GOLDEN) {   // 金星:不占展示架,小庆祝(比派对短)
                audio_fx_play_notes((audio_note_t[]){
                    { 784, 70, 55 }, { 988, 70, 55 }, { 1319, 130, 60 } }, 3);
                haptics_play(HAPTIC_COLLECT);
                ledstrip_fx_trigger(LED_FX_SWEEP_L2R);
                chain_lab_enc_rgb(255, 214, 64);
                chain_lab_joy_rgb(255, 214, 64);
            } else {
                mark_rack_collected(kind);
                maybe_spawn_golden();
            }
            s_grabbed_slot = -1;
        }
        set_depth_led(0);
        if (s_rack_count >= PRIZE_TYPES) enter_party();
        else                              s_state = CRANE_PLAY_IDLE;
    }
}

static void tick_party(void)
{
    s_state_frames++;
    int total = PARTY_HOLD_MS / POLL_PERIOD_MS; if (total < 1) total = 1;

    int hue = (s_state_frames * 9) % 360;
    uint8_t r, g, b;
    chain_lab_hue2rgb(hue, &r, &g, &b);
    chain_lab_enc_rgb(r, g, b);
    chain_lab_joy_rgb(r, g, b);   // 两节点同步跑彩虹(SPEC §4)

    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) {
        float phase = (float)s_state_frames * 0.35f + i * 1.3f;
        int dy = -(int)(5 + 5 * sinf(phase));
        lv_obj_set_y(s_rack_body[i], RACK_Y + dy);
        lv_obj_set_y(s_rack_accent[i], RACK_Y + dy + 3);
    }
    for (int i = 0; i < CONFETTI_N; i++) {
        int y = lv_obj_get_y(s_confetti[i]) + 3;
        if (y > CAB_Y1) y = RACK_Y + RACK_SLOT + 4;
        int x = lv_obj_get_x(s_confetti[i]) + s_confetti_vx[i];
        if (x < 0 || x > SCREEN_W - 8) s_confetti_vx[i] = -s_confetti_vx[i];
        lv_obj_set_pos(s_confetti[i], x, y);
    }
    bsp_display_unlock();

    if (s_state_frames >= total) {
        confetti_show(false);
        reset_round();
        s_state = CRANE_PLAY_IDLE;
    }
}

// ── 输入处理(每帧,SPEC §2/§3)────────────────────────────────────────────
// 死区 + 线性重缩放:盖住摇杆噪声(±40 ADC ≈ ±3px 吊臂抖动),死区外仍是满行程、无跳变
static float apply_deadzone(float n)
{
    float a = fabsf(n);
    if (a < JOY_ARM_DEADZONE) return 0.f;
    float scaled = (a - JOY_ARM_DEADZONE) / (1.0f - JOY_ARM_DEADZONE);
    return (n < 0) ? -scaled : scaled;
}

bool crane_game_recenter_ok(void)
{
    return s_state == CRANE_PLAY_IDLE && s_depth == 0;
}

static void handle_joystick(void)
{
    if (!chain_lab_joy_attached()) return;

    float nx = apply_deadzone(chain_lab_joy_x());
    float ny = chain_lab_joy_y();

    int target = SCREEN_W / 2 + (int)(nx * (CRANE_X_RANGE_PX / 2));
    if (target < ARM_X_MIN) target = ARM_X_MIN;
    if (target > ARM_X_MAX) target = ARM_X_MAX;
    s_arm_x = target;   // 摇杆随时可移吊臂,不受深度状态门控(SPEC §3)

    if (s_state != CRANE_PARTY) {   // 沿用现状既有映射(nx/ny→r/b);派对期间被彩虹覆盖
        uint8_t r = (uint8_t)(clampf((nx + 1) * 0.5f, 0.f, 1.f) * 255);
        uint8_t b = (uint8_t)(clampf((ny + 1) * 0.5f, 0.f, 1.f) * 255);
        chain_lab_joy_rgb(r, 60, b);
    }

    bool btn = chain_lab_joy_button();
    if (btn && !s_prev_joy_btn) trigger_wiggle();
    s_prev_joy_btn = btn;
}

static void handle_encoder(void)
{
    int16_t now = chain_lab_enc_attached() ? chain_lab_enc_value() : s_prev_enc_value;
    int delta = (int)now - (int)s_prev_enc_value;
    s_prev_enc_value = now;

    bool btn = chain_lab_enc_attached() && chain_lab_enc_button();
    bool press_edge = btn && !s_prev_enc_btn;
    s_prev_enc_btn = btn;

    if (s_state == CRANE_PLAY_IDLE || s_state == CRANE_DESCENDING) {
        if (delta != 0 && chain_lab_enc_attached()) apply_descend_delta(delta);
        // 深度门控只在 IDLE/DESCENDING 接受抓取键;GRABBING/ASCENDING/DEPOSIT/PARTY 期间吃掉(SPEC §3/§5)
        if ((s_state == CRANE_PLAY_IDLE || s_state == CRANE_DESCENDING) && press_edge) enter_grabbing();
    }
}

// ── 渲染(动态层:吊臂/缆绳/爪子,每帧小脏矩形,SPEC §10)───────────────────
// 爪子 = 横梁 + 左右两指;s_claw_w 是两指外沿间距(开→合 = 两指向中间并拢)
static void render_frame(void)
{
    bsp_display_lock(0);

    int arm_y  = RAIL_Y + s_bounce_dy;
    int claw_y = RAIL_Y + ARM_H + s_depth + s_bounce_dy;

    lv_obj_set_pos(s_arm, s_arm_x - ARM_W / 2, arm_y);
    lv_obj_set_pos(s_cable, s_arm_x - CABLE_W / 2, arm_y + ARM_H);
    lv_obj_set_size(s_cable, CABLE_W, s_depth > 0 ? s_depth : 1);
    lv_obj_set_pos(s_claw_bar, s_arm_x - CLAW_OPEN_W / 2 + s_wiggle_dx, claw_y);
    lv_obj_set_pos(s_claw_l, s_arm_x - s_claw_w / 2 + s_wiggle_dx, claw_y + CLAW_BAR_H - 2);
    lv_obj_set_pos(s_claw_r, s_arm_x + s_claw_w / 2 - PRONG_W + s_wiggle_dx, claw_y + CLAW_BAR_H - 2);

    bsp_display_unlock();
}

// ── 对外接口 ─────────────────────────────────────────────────────────
void crane_game_create(void)
{
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COL), 0);

    // 静态层:展示架背板 + 机身面板 + 轨道(载入时画一次,SPEC §10)
    lv_obj_t *rack_bg = plain(scr, SCREEN_W - 12, RACK_SLOT + 8, RACK_BG_COL, 10);
    lv_obj_set_pos(rack_bg, 6, 0);

    lv_obj_t *cabinet = plain(scr, SCREEN_W - 12, CAB_Y1 - CAB_Y0, PANEL_COL, 14);
    lv_obj_set_pos(cabinet, 6, CAB_Y0);

    lv_obj_t *rail = plain(scr, CRANE_X_RANGE_PX + ARM_W, 6, RAIL_COL, 3);
    lv_obj_set_pos(rail, ARM_X_MIN - ARM_W / 2, RAIL_Y - 3);

    // 展示架:PRIZE_TYPES 槽,初始灰底"未收集"(同 peekaboo 相册的占位手法)
    for (int i = 0; i < PRIZE_TYPES; i++) {
        int x = RACK_X0 + i * (RACK_SLOT + RACK_GAP);
        s_rack_slot_x[i] = x;
        lv_obj_t *body = plain(scr, RACK_SLOT, RACK_SLOT, UNCOLLECTED_BASE, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(body, x, RACK_Y);
        lv_obj_t *accent = plain(scr, 10, 10, UNCOLLECTED_ACCENT, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(accent, x + 9, RACK_Y + 3);
        s_rack_body[i]   = body;
        s_rack_accent[i] = accent;
    }

    // 战利品坑:横向铺满 CRANE_X_RANGE_PX,与吊臂可达范围一一对齐;
    // 底座先建(垫在玩偶下层),位置/高度每轮由 pit_slot_paint 按层摆
    for (int i = 0; i < PRIZE_TYPES; i++) {
        s_pit_slot_x[i] = (PRIZE_TYPES > 1)
            ? ARM_X_MIN + i * (CRANE_X_RANGE_PX / (PRIZE_TYPES - 1))
            : SCREEN_W / 2;
        s_pit_pedestal[i] = plain(scr, PEDESTAL_W, PEDESTAL_MIN_H, PEDESTAL_COL, 3);
        lv_obj_add_flag(s_pit_pedestal[i], LV_OBJ_FLAG_HIDDEN);
    }
    // 玩偶 = 透明容器 + 身体/特征件/眼/嘴子对象(整只一起移动;装扮由 pit_slot_paint 按 kind 配)
    // 特征件先建(垫在身体后,耳朵从脑后长出),眼/嘴后建(盖在脸上)
    for (int i = 0; i < PRIZE_TYPES; i++) {
        s_pit_doll[i] = plain(scr, PRIZE_SZ + 2 * DOLL_PAD_X, DOLL_HEADROOM + PRIZE_SZ, 0, 0);
        lv_obj_set_style_bg_opa(s_pit_doll[i], LV_OPA_TRANSP, 0);
        s_pit_parta[i] = plain(s_pit_doll[i], 8, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);
        s_pit_partb[i] = plain(s_pit_doll[i], 8, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);
        s_pit_body[i]  = plain(s_pit_doll[i], PRIZE_SZ, PRIZE_SZ, UNCOLLECTED_BASE, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s_pit_body[i], DOLL_PAD_X, DOLL_HEADROOM);
        s_pit_eyel[i]  = plain(s_pit_doll[i], EYE_SZ, EYE_SZ, EYE_COL, LV_RADIUS_CIRCLE);
        s_pit_eyer[i]  = plain(s_pit_doll[i], EYE_SZ, EYE_SZ, EYE_COL, LV_RADIUS_CIRCLE);
        s_pit_mouth[i] = plain(s_pit_doll[i], 6, 3, 0xFFFFFF, 1);
        lv_obj_add_flag(s_pit_doll[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 被抓中的玩偶跟随精灵(身体 + 双眼子对象,初始隐藏)
    s_held_body = plain(scr, PRIZE_HELD_SZ, PRIZE_HELD_SZ, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_t *hel = plain(s_held_body, EYE_SZ, EYE_SZ, EYE_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(hel, 6, 10);
    lv_obj_t *her = plain(s_held_body, EYE_SZ, EYE_SZ, EYE_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(her, 15, 10);
    lv_obj_add_flag(s_held_body, LV_OBJ_FLAG_HIDDEN);

    // 动态层:吊臂(小车)/ 缆绳 / 爪子(横梁 + 左右两指)
    s_arm      = plain(scr, ARM_W, ARM_H, ARM_COL, 6);
    s_cable    = plain(scr, CABLE_W, 1, CABLE_COL, 2);
    s_claw_bar = plain(scr, CLAW_OPEN_W, CLAW_BAR_H, CLAW_COL, 2);
    s_claw_l   = plain(scr, PRONG_W, CLAW_H, CLAW_COL, 2);
    s_claw_r   = plain(scr, PRONG_W, CLAW_H, CLAW_COL, 2);

    // 派对彩纸(初始隐藏)
    for (int i = 0; i < CONFETTI_N; i++) {
        lv_obj_t *c = plain(scr, 6, 6, 0xFFFFFF, 2);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        s_confetti[i] = c;
    }

    // 无字提示卡(没认到任何 Chain 节点时显示;仿 peekaboo 插头图标,不用文字)
    s_hint_card = plain(scr, 132, 76, 0xFFFFFF, 14);
    lv_obj_align(s_hint_card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *socket = plain(s_hint_card, 30, 30, 0xE0DACB, LV_RADIUS_CIRCLE);
    lv_obj_align(socket, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *hole = plain(socket, 12, 12, 0x8A8272, LV_RADIUS_CIRCLE);
    lv_obj_center(hole);
    lv_obj_t *wire = plain(s_hint_card, 34, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 48, 0);
    lv_obj_t *plug = plain(s_hint_card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 82, 0);
    lv_obj_add_flag(s_hint_card, LV_OBJ_FLAG_HIDDEN);
    s_hint_shown = false;

    bsp_display_unlock();

    shuffle_pit();
    bsp_display_lock(0);
    for (int i = 0; i < PRIZE_TYPES; i++) pit_slot_paint(i);
    bsp_display_unlock();

    s_arm_x = SCREEN_W / 2;
    s_depth = 0;
    s_state = CRANE_PLAY_IDLE;
    set_depth_led(0);
    led_base_set(LED_BASE_AMBIENT);

    render_frame();
}

void crane_game_tick(void)
{
    if (s_crank_cool > 0) s_crank_cool--;

    handle_joystick();
    handle_encoder();
    update_touch();

    switch (s_state) {
    case CRANE_GRABBING:  tick_grabbing();  break;
    case CRANE_ASCENDING: tick_ascending(); break;
    case CRANE_DEPOSIT:   tick_deposit();   break;
    case CRANE_PARTY:     tick_party();     break;
    default: break;
    }

    tick_wiggle();
    render_frame();
}

void crane_game_sync_attach(void)
{
    bool enc_on = chain_lab_enc_attached();
    bool joy_on = chain_lab_joy_attached();

    // 节点刚接上(含深度省电重扫后):重同步基准,避免节点计数复位被误读成一次大幅转动
    if (enc_on && !s_enc_was_attached) s_prev_enc_value = chain_lab_enc_value();
    s_enc_was_attached = enc_on;

    hint_card_apply(!enc_on && !joy_on);
}

void crane_game_reset_position(void)
{
    s_state         = CRANE_PLAY_IDLE;
    s_state_frames  = 0;
    s_depth         = 0;
    s_claw_w        = CLAW_OPEN_W;
    s_bounce_dy     = 0;
    s_wiggle_dx     = 0;
    s_wiggle_frames = 0;
    s_arm_x         = SCREEN_W / 2;

    if (s_touch_slot >= 0) pit_prize_offset(s_touch_slot, 0);   // 摆动中的战利品归位
    s_touch_slot   = -1;
    s_touch_frames = 0;

    if (s_grabbed_slot >= 0) {   // 深度省电打断了正在进行的抓取:战利品还给坑,不计入收集
        held_prize_show(s_pit_kind[s_grabbed_slot], false);
        s_pit_present[s_grabbed_slot] = true;
        pit_slot_show(s_grabbed_slot, true);
        s_grabbed_slot = -1;
    }

    s_prev_enc_value = chain_lab_enc_attached() ? chain_lab_enc_value() : 0;
    s_prev_enc_btn   = false;
    s_prev_joy_btn   = false;

    set_depth_led(0);
    led_base_set(LED_BASE_AMBIENT);
    render_frame();
}
