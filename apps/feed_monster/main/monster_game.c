// 喂怪兽 —— 游戏主体
//
// 模型:超声波测"手离探头多远" d(mm),归一化 t(近=1,远=0):
//   连续层(始终在,零技巧 = 空气琴):
//     嘴张开大小 = t(近张大/远闭合,唯一每帧动的主对象)
//     音高       = t 量化到 PITCH_STEPS 档五声音阶(近=高,仅档变时弹一声)
//     底座灯带   = t 过阈值 → NEAR(亮暖)否则 AMBIENT
//   喂食循环(目标钩子,靠近就喂):
//     怪兽饿了→饼干浮在头顶;手进吃区(d≤EAT_MM,边沿触发)→ 饼干飞入嘴 CHOMP
//     + 收集音 + 轻震 + 灯扫一圈 + 肚子弹一下,喂满 WIN_FEED_COUNT → 全屏庆祝 → 重开
//   永不失败:手不靠近就一直等,没有惩罚/计时。
//
// 渲染纪律(CLAUDE.md §9):怪兽身/眼/场景全静态(进场画一次);每帧只动"嘴"(高度变=小
// 脏矩形)+ 飞行中的饼干;庆祝迸发限量精灵。绝无整屏重绘。
// 超声波采样:单任务内**流水线**——每 SONIC_READ_TICKS 帧读一次上次触发的结果再重触发,
// 从不阻塞等测量周期。省电 core2_sleep 托管;手在探头前活动 = 有人玩(kick)。

#include "monster_game.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "ledstrip_fx.h"
#include "unit_ultrasonic.h"

#include "tuning.h"

static const char *TAG = "monster_game";

#define SCREEN_W  320
#define SCREEN_H  240

// ── 配色(§18 家族:大块扁平圆润 + 暖色)────────────────────────────────
#define BG_DAY      0xF6EED9   // 暖米(压亮度)
#define GROUND_COL  0xD7ECBF   // 草地
#define SUN_COL     0xFFC75F
#define BODY_COL    0x7FD0C0   // 薄荷绿怪兽
#define BODY_EDGE   0x63B7A6   // (备用,暂未用于描边)
#define EYE_WHITE   0xFFFFFF
#define EYE_PUPIL   0x3A3A38
#define CHEEK_COL   0xFFB3C6
#define MOUTH_COL   0x7A3B34   // 嘴内暗红
#define TONGUE_COL  0xFF8FB0
#define COOKIE_COL  0xE0B87A
#define CHIP_COL    0x6B4329
#define HEART_FULL  0xE0B87A
#define HEART_EMPTY 0xCFC6B8
#define STAR_COL    0xFFE89B
#define HINT_CARD   0xFFFFFF

// 怪兽几何(身体是所有部件的父容器,整体弹跳=移动它)
#define BODY_D      150
#define BODY_X      ((SCREEN_W - BODY_D) / 2)   // 85
#define BODY_Y      72
#define BODY_CX     (BODY_X + BODY_D / 2)        // 160
#define MOUTH_TOP   76                            // 嘴顶在身体内的 y(固定,高度向下长)
#define MOUTH_CY    (BODY_Y + MOUTH_TOP + 22)     // 嘴中心屏幕 y(饼干飞入目标)

#define COOKIE_SZ   36
#define COOKIE_HX   (BODY_CX - COOKIE_SZ / 2)     // 头顶家位 x(左上)
#define COOKIE_HY   28

// C 大调五声音阶(近=高档);怎么比划都和谐
static const uint16_t PENTA_HZ[PITCH_STEPS] = { 523, 587, 659, 784, 880, 1047, 1175, 1319 };

// ── 状态 ─────────────────────────────────────────────────────────────
typedef enum { ST_PLAY = 0, ST_WIN } state_t;

static state_t s_state;
static int     s_frame;                 // 当前状态帧计数(WIN 用)

static bool    s_unit_ok;
static int     s_retry_frames;          // 单元缺席重试倒计时
static int     s_retry_count;           // 连续重试失败(每 15 次扫总线自诊断)
static int     s_err_streak;            // 连续 I2C 读失败(拔线检测)
static int     s_since_trig;            // 距上次触发过了几帧(流水线读)

static bool    s_have_target;           // 手在量程内?
static float   s_dist;                  // 滤波后距离(mm)
static float   s_dist_raw;              // 上次原始读数(算手是否在动)
static int     s_mouth_target;          // 嘴目标高度
static int     s_mouth_h;               // 嘴当前显示高度
static int     s_pitch_bucket;          // 上次音高档(-1=无)
static bool    s_armed;                 // 喂食边沿:手退出吃区后才武装
static led_base_t s_led_base;           // 当前灯带基础模式(避免重复 set)

static int     s_fed;                   // 已喂几个
static int     s_cookie_state;          // 0=在家 1=飞入嘴 2=等重生
static int     s_cookie_p;              // 飞入进度帧
static int     s_cookie_wait;           // 重生倒计时帧

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

// ── LVGL 对象 ─────────────────────────────────────────────────────────
static lv_obj_t *s_monster;             // 身体容器(眼/嘴/舌/腮是其子)
static lv_obj_t *s_mouth;               // 嘴(高度每帧变)
static lv_obj_t *s_cookie;              // 头顶饼干
static lv_obj_t *s_hearts[WIN_FEED_COUNT];
static lv_obj_t *s_burst[BURST_COUNT];  // 庆祝迸发(win 时建,重置时删)
static lv_obj_t *s_plug_hint;           // 没插单元的无字提示卡

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

// lv_anim exec 包装(避免把 lv_obj_set_x/y 直接强转成 exec_cb 的函数指针 UB)
static void anim_set_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, v); }
static void anim_set_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }

// ── UI 搭建 ───────────────────────────────────────────────────────────
static void make_monster(lv_obj_t *scr)
{
    lv_obj_t *body = plain(scr, BODY_D, BODY_D, BODY_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(body, BODY_X, BODY_Y);
    s_monster = body;

    // 眼(白底黑瞳)
    lv_obj_t *el = plain(body, 38, 38, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(el, LV_ALIGN_TOP_MID, -34, 26);
    lv_obj_t *er = plain(body, 38, 38, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(er, LV_ALIGN_TOP_MID, 34, 26);
    lv_obj_t *pl = plain(el, 18, 18, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_align(pl, LV_ALIGN_CENTER, 0, 4);
    lv_obj_t *pr = plain(er, 18, 18, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_align(pr, LV_ALIGN_CENTER, 0, 4);

    // 腮红
    lv_obj_t *cl = plain(body, 20, 14, CHEEK_COL, LV_RADIUS_CIRCLE);
    lv_obj_align(cl, LV_ALIGN_TOP_MID, -52, 62);
    lv_obj_t *cr = plain(body, 20, 14, CHEEK_COL, LV_RADIUS_CIRCLE);
    lv_obj_align(cr, LV_ALIGN_TOP_MID, 52, 62);

    // 嘴(顶边固定在 y=MOUTH_TOP,高度向下长;子对象被裁到嘴框内)
    s_mouth = plain(body, MOUTH_W, MOUTH_H_MIN, MOUTH_COL, 12);
    lv_obj_align(s_mouth, LV_ALIGN_TOP_MID, 0, MOUTH_TOP);
    // 舌头(嘴内底部;嘴短时被裁掉)
    lv_obj_t *tongue = plain(s_mouth, MOUTH_W - 26, 26, TONGUE_COL, 10);
    lv_obj_align(tongue, LV_ALIGN_BOTTOM_MID, 0, 6);
    s_mouth_h = MOUTH_H_MIN;
    s_mouth_target = MOUTH_H_MIN;
}

static void make_cookie(lv_obj_t *scr)
{
    lv_obj_t *c = plain(scr, COOKIE_SZ, COOKIE_SZ, COOKIE_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(c, COOKIE_HX, COOKIE_HY);
    // 3 颗巧克力豆
    const int chip[3][2] = { { 8, 9 }, { 20, 14 }, { 13, 22 } };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *d = plain(c, 6, 6, CHIP_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(d, chip[i][0], chip[i][1]);
    }
    s_cookie = c;
}

static void make_hearts(lv_obj_t *scr)
{
    for (int i = 0; i < WIN_FEED_COUNT; i++) {
        lv_obj_t *h = plain(scr, 16, 16, HEART_EMPTY, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(h, 8 + i * 22, 8);
        s_hearts[i] = h;
    }
}

static void make_plug_hint(lv_obj_t *scr)
{
    // 无字提示卡:超声波单元(方块+两个"喇叭"圆)+ 引线 + 插头。给家长看"去插超声波"
    lv_obj_t *card = plain(scr, 132, 76, HINT_CARD, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *sensor = plain(card, 46, 40, 0x4FB0D8, 8);
    lv_obj_align(sensor, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *e1 = plain(sensor, 14, 14, 0xEAF7FB, LV_RADIUS_CIRCLE);
    lv_obj_align(e1, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_t *e2 = plain(sensor, 14, 14, 0xEAF7FB, LV_RADIUS_CIRCLE);
    lv_obj_align(e2, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_t *wire = plain(card, 30, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 62, 0);
    lv_obj_t *plug = plain(card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 96, 0);
    s_plug_hint = card;
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_DAY), 0);

    lv_obj_t *sun = plain(scr, 34, 34, SUN_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(sun, SCREEN_W - 48, 8);
    lv_obj_t *ground = plain(scr, SCREEN_W, 34, GROUND_COL, 0);
    lv_obj_set_pos(ground, 0, SCREEN_H - 30);

    make_hearts(scr);
    make_monster(scr);
    make_cookie(scr);
    make_plug_hint(scr);

    bsp_display_unlock();
}

// ── 嘴 / 灯 / 心 更新 ─────────────────────────────────────────────────
static void mouth_apply(int h)   // 调用方持锁;顶边固定,高度向下长
{
    lv_obj_set_height(s_mouth, h);
}

static void update_hearts(void)  // 调用方持锁
{
    for (int i = 0; i < WIN_FEED_COUNT; i++) {
        lv_obj_set_style_bg_color(s_hearts[i],
            lv_color_hex(i < s_fed ? HEART_FULL : HEART_EMPTY), 0);
    }
}

static void set_led_base(led_base_t base)
{
    if (base != s_led_base) {
        s_led_base = base;
        ledstrip_fx_set_base(base);
    }
}

// ── 怪兽整体弹跳(吃到 / 庆祝)─────────────────────────────────────────
static void monster_bounce(int lift, int up_ms, int down_ms)
{
    bsp_display_lock(0);
    lv_anim_delete(s_monster, anim_set_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_monster);
    lv_anim_set_exec_cb(&a, anim_set_y);
    lv_anim_set_values(&a, BODY_Y, BODY_Y - lift);
    lv_anim_set_duration(&a, up_ms);
    lv_anim_set_playback_duration(&a, down_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
    bsp_display_unlock();
}

// ── 喂食 / 庆祝 ───────────────────────────────────────────────────────
static void enter_win(void);

static void do_eat(void)
{
    // 饼干飞入嘴的流水线在 cookie_tick 里推进;这里起头 + 四通道反馈
    s_cookie_state = 1;   // 飞入
    s_cookie_p     = 0;

    s_mouth_h = MOUTH_H_MIN;      // 咔嚓咬合一下:嘴瞬间闭合,随后 lerp 重新张开 = CHOMP
    monster_bounce(14, 110, 150); // 肚子弹一下

    audio_fx_play(SND_COLLECT);
    haptics_play(HAPTIC_COLLECT);
    ledstrip_fx_trigger(LED_FX_COLLECT);

    s_fed++;
    bsp_display_lock(0);
    update_hearts();
    bsp_display_unlock();

    ESP_LOGI(TAG, "喂到一个(%d/%d)", s_fed, WIN_FEED_COUNT);
    if (s_fed >= WIN_FEED_COUNT) enter_win();
}

static void cookie_tick(void)   // 每帧推进饼干飞入 / 重生(仅 ST_PLAY)
{
    if (s_cookie_state == 1) {           // 飞入嘴:home →(mouth 中心)+ 缩小
        s_cookie_p++;
        int total = COOKIE_EAT_MS / POLL_PERIOD_MS;
        if (total < 1) total = 1;
        int p = s_cookie_p * 100 / total;
        if (p > 100) p = 100;
        int sz = COOKIE_SZ - (COOKIE_SZ - 8) * p / 100;
        int cx = (COOKIE_HX + COOKIE_SZ / 2) + (BODY_CX - (COOKIE_HX + COOKIE_SZ / 2)) * p / 100;
        int cy = (COOKIE_HY + COOKIE_SZ / 2) + (MOUTH_CY - (COOKIE_HY + COOKIE_SZ / 2)) * p / 100;
        bsp_display_lock(0);
        lv_obj_set_size(s_cookie, sz, sz);
        lv_obj_set_pos(s_cookie, cx - sz / 2, cy - sz / 2);
        bsp_display_unlock();
        if (s_cookie_p >= total) {
            bsp_display_lock(0);
            lv_obj_add_flag(s_cookie, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();
            s_cookie_state = 2;
            s_cookie_wait  = COOKIE_RESPAWN_MS / POLL_PERIOD_MS;
        }
    } else if (s_cookie_state == 2) {    // 等一会冒出下一颗
        if (--s_cookie_wait <= 0) {
            bsp_display_lock(0);
            lv_obj_set_size(s_cookie, COOKIE_SZ, COOKIE_SZ);
            lv_obj_set_pos(s_cookie, COOKIE_HX, COOKIE_HY);
            lv_obj_remove_flag(s_cookie, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();
            s_cookie_state = 0;
        }
    }
}

static void burst_clear(void)   // 调用方持锁
{
    for (int i = 0; i < BURST_COUNT; i++) {
        if (s_burst[i]) { lv_obj_delete(s_burst[i]); s_burst[i] = NULL; }
    }
}

static void enter_win(void)
{
    s_state = ST_WIN;
    s_frame = 0;

    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
    monster_bounce(26, 200, 240);

    bsp_display_lock(0);
    lv_obj_add_flag(s_cookie, LV_OBJ_FLAG_HIDDEN);   // 庆祝时不显示饼干
    for (int i = 0; i < BURST_COUNT; i++) {          // 从怪兽中心迸发小星(限量,一次性)
        lv_obj_t *s = plain(lv_screen_active(), 12, 12, STAR_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s, BODY_CX - 6, BODY_Y + 40);
        s_burst[i] = s;
        int ang = i * (360 / BURST_COUNT);
        int dx = (int)(90 * cosf(ang * 3.14159f / 180));
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
        lv_anim_set_values(&ay, BODY_Y + 40, BODY_Y + 40 + dy);
        lv_anim_set_duration(&ay, 520);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
        lv_anim_start(&ay);
    }
    bsp_display_unlock();
    ESP_LOGI(TAG, "喂满 %d → 庆祝!", WIN_FEED_COUNT);
}

static void win_tick(void)
{
    s_frame++;
    if (s_frame >= WIN_HOLD_MS / POLL_PERIOD_MS) {   // 收场:清零重开
        s_fed = 0;
        s_cookie_state = 0;
        s_armed = false;                              // 要求手先退出吃区才能再喂
        bsp_display_lock(0);
        burst_clear();
        update_hearts();
        lv_obj_set_size(s_cookie, COOKIE_SZ, COOKIE_SZ);
        lv_obj_set_pos(s_cookie, COOKIE_HX, COOKIE_HY);
        lv_obj_remove_flag(s_cookie, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
        s_state = ST_PLAY;
        s_frame = 0;
    }
}

// ── 单元接管 / 缺席 ───────────────────────────────────────────────────
static void plug_hint_show(bool show)
{
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

static bool unit_attach(bool greet)
{
    if (unit_ultrasonic_init(core2_board_port_a(), 0) != ESP_OK) return false;
    // init 已发一次触发探在位;从此帧起 SONIC_READ_TICKS 帧后即可读到有效结果
    s_since_trig  = 0;
    s_err_streak  = 0;
    s_unit_ok     = true;
    s_have_target = false;
    s_armed       = true;
    plug_hint_show(false);
    if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
    ESP_LOGI(TAG, "超声波已接管");
    return true;
}

static void unit_lost(void)
{
    s_unit_ok      = false;
    s_retry_frames = 0;
    s_have_target  = false;
    s_mouth_target = MOUTH_H_MIN;
    plug_hint_show(true);
    audio_fx_play(SND_BUMP_MED);   // 温柔一声"咦?"
    ESP_LOGW(TAG, "超声波失联(拔线/断电?),转入重试探测");
}

// ── 一次新距离读数:更新连续层 + 喂食判定(在 poll_sonic 里,~10Hz)──────────
static void on_reading(float mm, core2_sleep_stage_t stage)
{
    bool moving = s_have_target && fabsf(mm - s_dist_raw) > SONIC_MOVE_MM;
    s_dist_raw = mm;

    if (!s_have_target) s_dist = mm;                 // 首帧直接采纳
    else                s_dist += (mm - s_dist) * DIST_ALPHA;
    s_have_target = true;

    // 归一化 t(近=1)
    float d = s_dist;
    if (d < NEAR_MM) d = NEAR_MM;
    if (d > FAR_MM)  d = FAR_MM;
    float t = (FAR_MM - d) / (FAR_MM - NEAR_MM);

    s_mouth_target = MOUTH_H_MIN + (int)((MOUTH_H_MAX - MOUTH_H_MIN) * t);

    // 音高:仅档变时弹一声(避免机枪音)
    int bucket = (int)(t * (PITCH_STEPS - 1) + 0.5f);
    if (bucket < 0) bucket = 0;
    if (bucket > PITCH_STEPS - 1) bucket = PITCH_STEPS - 1;
    if (stage == CORE2_SLEEP_AWAKE && s_state == ST_PLAY && bucket != s_pitch_bucket) {
        audio_fx_play_notes((audio_note_t[]){ { PENTA_HZ[bucket], TONE_MS, TONE_AMP } }, 1);
    }
    s_pitch_bucket = bucket;

    // 灯带(仅清醒态驱动;打盹/深度由 core2_sleep 托管)
    if (stage == CORE2_SLEEP_AWAKE) {
        set_led_base(t > LED_NEAR_T ? LED_BASE_NEAR : LED_BASE_AMBIENT);
    }

    // 喂食:手跨进吃区(边沿)→ 吃;须先退出吃区再武装
    if (stage == CORE2_SLEEP_AWAKE && s_state == ST_PLAY) {
        if (s_dist <= EAT_MM && s_armed && s_cookie_state == 0) {
            s_armed = false;
            do_eat();
        }
    }
    if (s_dist > EAT_EXIT_MM) s_armed = true;

    // 手在动 = 有人玩
    if (moving) {
        core2_sleep_kick(&s_sleep);
        if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
    }
}

static void on_no_target(core2_sleep_stage_t stage)
{
    s_have_target  = false;
    s_mouth_target = MOUTH_H_MIN;   // 手离开 → 闭嘴
    s_pitch_bucket = -1;            // 重置,下次进量程会重新起调
    s_armed        = true;          // 手走了,重新武装
    if (stage == CORE2_SLEEP_AWAKE) set_led_base(LED_BASE_AMBIENT);
}

// ── 超声波轮询(流水线:读上次触发结果 → 立刻重触发)────────────────────
static void poll_sonic(core2_sleep_stage_t stage)
{
    if (!s_unit_ok) {
        if (++s_retry_frames >= UNIT_RETRY_MS / POLL_PERIOD_MS) {
            s_retry_frames = 0;
            if (unit_attach(true)) {
                s_retry_count = 0;
            } else if (++s_retry_count % 15 == 0) {
                // 久等不来:扫总线自诊断,被拉死则断电重启单元(RCWL 一般不卡,但骨架照留)
                bool found = core2_board_port_a_scan();
                if (found || core2_board_port_a_stuck()) core2_board_port_a_recover();
            }
        }
        return;
    }

    if (++s_since_trig < SONIC_READ_TICKS) return;
    s_since_trig = 0;

    float mm = 0;
    esp_err_t r = unit_ultrasonic_read_mm(&mm);
    unit_ultrasonic_trigger();      // 读完立刻重触发,下个周期再读

    if (r == ESP_OK) {
        s_err_streak = 0;
        on_reading(mm, stage);
    } else if (r == ESP_ERR_NOT_FOUND) {
        s_err_streak = 0;           // 单元活着,只是没目标(手离开量程)
        on_no_target(stage);
    } else {
        if (++s_err_streak >= ERR_STREAK_LOST) unit_lost();   // 通信失败连续多次 = 拔线
    }
}

// ── 每帧:嘴平滑到目标高度 ────────────────────────────────────────────
static void mouth_tick(void)
{
    int diff = s_mouth_target - s_mouth_h;
    if (diff > -3 && diff < 3) s_mouth_h = s_mouth_target;
    else                       s_mouth_h += diff * MOUTH_LERP_NUM / MOUTH_LERP_DEN;
    bsp_display_lock(0);
    mouth_apply(s_mouth_h);
    bsp_display_unlock();
}

// ── 主任务(30Hz)──────────────────────────────────────────────────────
static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);

        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ acc.x, acc.y, acc.z } : NULL,
                                        s_state == ST_PLAY && have);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);

        // 深度省电切过 M-Bus 5V → 超声波掉电复位:醒来后重新接管
        if (s_prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_unit_ok = false;
            unit_attach(false);
        }
        s_prev_stage = stage;
        // 非清醒态:core2_sleep 会把灯带切 IDLE/OFF;复位本地追踪,回清醒时重评估
        if (stage != CORE2_SLEEP_AWAKE) s_led_base = LED_BASE_AMBIENT;

        if (stage != CORE2_SLEEP_DEEP) poll_sonic(stage);

        if (stage == CORE2_SLEEP_AWAKE) {
            mouth_tick();
            switch (s_state) {
                case ST_PLAY: cookie_tick(); break;
                case ST_WIN:  win_tick();    break;
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void monster_game_start(void)
{
    s_pitch_bucket = -1;
    s_led_base     = LED_BASE_AMBIENT;
    s_armed        = true;

    ui_create();

    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    scfg.nap_after_ms     = NAP_AFTER_MS;
    scfg.deep_after_ms    = DEEP_AFTER_MS;
    scfg.awake_brightness = PLAY_BRIGHTNESS;
    scfg.nap_brightness   = NAP_BRIGHTNESS;
    scfg.frame_ms         = POLL_PERIOD_MS;
    core2_sleep_init(&s_sleep, &scfg);

    ledstrip_fx_set_base(LED_BASE_AMBIENT);

    bool attached = unit_attach(false);
    if (!attached) {
        ESP_LOGW(TAG, "超声波未就位:必须插 Core2 机身侧面的红色 PORT.A 口"
                      "(底座黑口 PORT.B/蓝口 PORT.C 不是 I2C)");
        bool found = core2_board_port_a_scan();
        if (found || core2_board_port_a_stuck()) {
            core2_board_port_a_recover();
            attached = unit_attach(false);
            if (!attached) core2_board_port_a_scan();
        }
    }
    if (attached) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    } else {
        plug_hint_show(true);          // 没插也能开机:出提示卡,低频重试,插上即"你好"
        audio_fx_play(SND_BUMP_MED);
    }

    xTaskCreate(game_task, "monster", 4096, NULL, 5, NULL);
}
