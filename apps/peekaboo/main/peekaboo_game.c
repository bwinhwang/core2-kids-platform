// 躲猫猫昼夜屋 —— 游戏主体
//
// 模型:DLight 测环境光 lux。用**自适应基准** s_ref 追踪"没捂住时的房间亮度"(向上跟得快、
//   向下跟得极慢),于是:
//     捂住(挡光)→ lux 掉到 s_ref×COVER_FRAC 以下 → 入夜(NIGHT)
//     松开(光回)→ lux 升到 s_ref×UNCOVER_FRAC 以上 → 天亮(DAY)
//   两个阈值之间是迟滞带,防在临界抖动。这样任何房间明暗都好用,无需按房间标定绝对 lux。
//
//   昼夜是**离散二值状态**(不是每帧连续调色)——只有"捂/松"这一下才切换场景,天生符合
//   §9 渲染红线:整屏级重绘只发生在切换那一刻(像 tilt_maze 进关 / feed_monster 庆祝),
//   不是每帧。日常帧里只有限量小精灵(星星眨/萤火虫漂)在各自小区域动。
//
//   四通道反馈(§6):切换瞬间 画面换场景 + 音(天亮=鸡鸣上扬/入夜=风铃下行)+ 轻震 + 灯带
//   (白天暖亮 AMBIENT / 夜晚暗呼吸 IDLE)。每次"天亮"= 一次躲猫猫揭晓 → 计数,攒够
//   WIN_CYCLES 次 → 全屏小庆祝(星星迸发 + 过关音 + 三连震 + 彩虹)。零失败、无计时。
//
//   桌面/手持玩法:机身不动 ≠ 没人玩 —— 照度变化(手在捂/松)= 有人玩 → core2_sleep_kick,
//   否则玩着玩着会打盹。深度省电切 M-Bus 5V → DLight 断电复位,唤醒后重新接管。

#include "peekaboo_game.h"

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
#include "unit_dlight.h"

#include "tuning.h"

static const char *TAG = "peekaboo";

#define SCREEN_W  320
#define SCREEN_H  240

// ── 配色(§18 家族:大块扁平圆润 + 暖色;夜空用深蓝不用纯黑,§13 护眼)──────────
#define DAY_SKY     0x8FCBE8   // 白天天空(柔和蓝,压亮度)
#define NIGHT_SKY   0x33335C   // 夜空(深蓝,非纯黑)
#define DAY_GROUND  0xD7ECBF   // 白天草地
#define NIGHT_GROUND 0x3E4468  // 夜晚草地(压暗)
#define SUN_COL     0xFFC75F
#define MOON_COL    0xF4EDC9   // 月亮奶白
#define STAR_COL    0xFFE89B
#define FIREFLY_COL 0xFFF3B0
#define BODY_COL    0xFFD23F   // 吉祥物「圆圆」身
#define CHEEK_COL   0xFFB3C6
#define BEAK_COL    0xFB8B24
#define EYE_WHITE   0xFFFFFF
#define EYE_PUPIL   0x3A3A38
#define DOT_FULL    0xFFD23F   // 天亮计数点(亮=已揭晓)
#define DOT_EMPTY   0xCFC6B8
#define BURST_COL   0xFFE89B
#define HINT_CARD   0xFFFFFF

// 吉祥物几何(身体是所有部件的父容器,整体弹跳=移动它)
#define BODY_D      104
#define BODY_X      ((SCREEN_W - BODY_D) / 2)   // 108
#define BODY_Y      92
#define BODY_CX     (BODY_X + BODY_D / 2)        // 160
#define BODY_CY     (BODY_Y + BODY_D / 2)        // 144
#define GROUND_H    40
#define GROUND_Y    (SCREEN_H - GROUND_H)        // 200

// ── 状态 ─────────────────────────────────────────────────────────────
static bool  s_night;                   // 当前昼夜
static bool  s_first;                    // 照度首帧(重建基准)
static float s_lux;                      // 滤波后照度
static float s_ref;                      // 自适应"房间亮度"基准
static float s_last_lux;                 // 上次照度(算手是否在动)

static bool  s_unit_ok;
static int   s_retry_frames;            // 单元缺席重试倒计时
static int   s_retry_count;             // 连续重试失败(每 15 次扫总线自诊断)
static int   s_err_streak;              // 连续 I2C 读失败(拔线检测)
static int   s_since_read;              // 距上次读过了几帧

static int   s_reveals;                 // 已揭晓(天亮)几次
static int   s_win_frames;              // >0 = 庆祝迸发进行中(倒计时帧)
static int   s_led_base = -1;           // 当前灯带基础模式(-1=未定,强制下次 set)

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

// ── LVGL 对象 ─────────────────────────────────────────────────────────
static lv_obj_t *s_sky;                 // 全屏天空(切换时改色)
static lv_obj_t *s_day_grp;             // 白天场景(太阳/白天草地)
static lv_obj_t *s_night_grp;           // 夜晚场景(月亮/星星/萤火虫/夜草地)
static lv_obj_t *s_char;                // 圆圆身体(眼/腮/喙是其子)
static lv_obj_t *s_eyes_open;           // 睁眼组(白天)
static lv_obj_t *s_eyes_shut;           // 闭眼组(夜晚)
static lv_obj_t *s_zzz;                 // 夜晚"Zzz"
static lv_obj_t *s_dots[WIN_CYCLES];    // 天亮计数
static lv_obj_t *s_burst[BURST_COUNT];  // 庆祝迸发(celebrate 建,收场删)
static lv_obj_t *s_plug_hint;           // 没插单元的无字提示卡

// ── 小工具(纯 LVGL,调用方持锁)──────────────────────────────────────────
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

// 透明全屏图层(装一组场景精灵,整组 show/hide)
static lv_obj_t *layer(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// lv_anim exec 包装(避免把 lv_obj_set_* 直接强转成 exec_cb 的函数指针 UB)
static void anim_set_x(void *o, int32_t v)   { lv_obj_set_x((lv_obj_t *)o, v); }
static void anim_set_y(void *o, int32_t v)   { lv_obj_set_y((lv_obj_t *)o, v); }
static void anim_set_opa(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

// ── UI 搭建 ───────────────────────────────────────────────────────────
static void make_day(lv_obj_t *parent)
{
    s_day_grp = layer(parent);
    lv_obj_t *ground = plain(s_day_grp, SCREEN_W, GROUND_H, DAY_GROUND, 0);
    lv_obj_set_pos(ground, 0, GROUND_Y);
    lv_obj_t *sun = plain(s_day_grp, 46, 46, SUN_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(sun, SCREEN_W - 62, 14);
}

static void make_night(lv_obj_t *parent)
{
    s_night_grp = layer(parent);
    lv_obj_t *ground = plain(s_night_grp, SCREEN_W, GROUND_H, NIGHT_GROUND, 0);
    lv_obj_set_pos(ground, 0, GROUND_Y);
    lv_obj_t *moon = plain(s_night_grp, 42, 42, MOON_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(moon, SCREEN_W - 60, 14);

    // 眨眼的星星(各自小区域,低频 opa 动画,§9.5 帧预算内)
    static const int spos[STAR_COUNT][2] = {
        { 40, 36 }, { 96, 18 }, { 150, 44 }, { 206, 26 }, { 258, 58 }, { 122, 70 },
    };
    for (int i = 0; i < STAR_COUNT; i++) {
        lv_obj_t *st = plain(s_night_grp, 8, 8, STAR_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(st, spos[i][0], spos[i][1]);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, st);
        lv_anim_set_exec_cb(&a, anim_set_opa);
        lv_anim_set_values(&a, 90, 255);
        lv_anim_set_duration(&a, 640 + i * 130);
        lv_anim_set_playback_duration(&a, 640 + i * 130);
        lv_anim_set_delay(&a, i * 150);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    // 漂移的萤火虫(小发光点,各自小范围 x/y 循环)
    static const int fpos[FIREFLY_COUNT][2] = {
        { 66, 150 }, { 190, 138 }, { 240, 168 }, { 128, 176 },
    };
    for (int i = 0; i < FIREFLY_COUNT; i++) {
        lv_obj_t *ff = plain(s_night_grp, 9, 9, FIREFLY_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(ff, fpos[i][0], fpos[i][1]);
        lv_anim_t ax;
        lv_anim_init(&ax);
        lv_anim_set_var(&ax, ff);
        lv_anim_set_exec_cb(&ax, anim_set_x);
        lv_anim_set_values(&ax, fpos[i][0] - 16, fpos[i][0] + 16);
        lv_anim_set_duration(&ax, 1700 + i * 240);
        lv_anim_set_playback_duration(&ax, 1700 + i * 240);
        lv_anim_set_repeat_count(&ax, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&ax, lv_anim_path_ease_in_out);
        lv_anim_start(&ax);
        lv_anim_t ay;
        lv_anim_init(&ay);
        lv_anim_set_var(&ay, ff);
        lv_anim_set_exec_cb(&ay, anim_set_y);
        lv_anim_set_values(&ay, fpos[i][1] - 10, fpos[i][1] + 10);
        lv_anim_set_duration(&ay, 1300 + i * 190);
        lv_anim_set_playback_duration(&ay, 1300 + i * 190);
        lv_anim_set_repeat_count(&ay, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_in_out);
        lv_anim_start(&ay);
    }
}

static void make_char(lv_obj_t *parent)
{
    lv_obj_t *body = plain(parent, BODY_D, BODY_D, BODY_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(body, BODY_X, BODY_Y);
    s_char = body;

    // 睁眼组(白底黑瞳,白天可见)
    s_eyes_open = layer(body);
    lv_obj_set_size(s_eyes_open, BODY_D, BODY_D);
    lv_obj_t *el = plain(s_eyes_open, 26, 26, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(el, LV_ALIGN_TOP_MID, -22, 26);
    lv_obj_t *er = plain(s_eyes_open, 26, 26, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(er, LV_ALIGN_TOP_MID, 22, 26);
    lv_obj_t *pl = plain(el, 12, 12, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_align(pl, LV_ALIGN_CENTER, 0, 3);
    lv_obj_t *pr = plain(er, 12, 12, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_align(pr, LV_ALIGN_CENTER, 0, 3);

    // 闭眼组(两道弯睡眼,夜晚可见)
    s_eyes_shut = layer(body);
    lv_obj_set_size(s_eyes_shut, BODY_D, BODY_D);
    lv_obj_t *sl = plain(s_eyes_shut, 24, 6, EYE_PUPIL, 3);
    lv_obj_align(sl, LV_ALIGN_TOP_MID, -22, 36);
    lv_obj_t *sr = plain(s_eyes_shut, 24, 6, EYE_PUPIL, 3);
    lv_obj_align(sr, LV_ALIGN_TOP_MID, 22, 36);
    lv_obj_add_flag(s_eyes_shut, LV_OBJ_FLAG_HIDDEN);   // 初始白天

    // 腮红 + 喙(两态都在)
    lv_obj_t *cl = plain(body, 18, 12, CHEEK_COL, LV_RADIUS_CIRCLE);
    lv_obj_align(cl, LV_ALIGN_TOP_MID, -34, 58);
    lv_obj_t *cr = plain(body, 18, 12, CHEEK_COL, LV_RADIUS_CIRCLE);
    lv_obj_align(cr, LV_ALIGN_TOP_MID, 34, 58);
    lv_obj_t *beak = plain(body, 14, 10, BEAK_COL, 3);
    lv_obj_align(beak, LV_ALIGN_TOP_MID, 0, 56);
}

static void make_zzz(lv_obj_t *scr)
{
    lv_obj_t *z = lv_label_create(scr);
    lv_label_set_text(z, "z Z");
    lv_obj_set_style_text_color(z, lv_color_hex(0xEAEAF2), 0);
    lv_obj_set_pos(z, BODY_CX + 30, BODY_Y - 6);
    s_zzz = z;
    lv_obj_add_flag(z, LV_OBJ_FLAG_HIDDEN);             // 初始白天

    // 缓缓上浮(局部小区域;白天隐藏时不绘制)
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, z);
    lv_anim_set_exec_cb(&a, anim_set_y);
    lv_anim_set_values(&a, BODY_Y - 6, BODY_Y - 20);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_playback_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void make_dots(lv_obj_t *scr)
{
    for (int i = 0; i < WIN_CYCLES; i++) {
        lv_obj_t *d = plain(scr, 14, 14, DOT_EMPTY, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(d, 8 + i * 20, 8);
        s_dots[i] = d;
    }
}

static void make_plug_hint(lv_obj_t *scr)
{
    // 无字提示卡:光传感器(方块+"眼")+ 引线 + 插头。给家长看"去插 DLight"
    lv_obj_t *card = plain(scr, 132, 76, HINT_CARD, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *sensor = plain(card, 42, 42, 0xFFC75F, 8);   // 光单元(暖黄)
    lv_obj_align(sensor, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *eye = plain(sensor, 16, 16, 0xFFF3B0, LV_RADIUS_CIRCLE);   // 感光"眼"
    lv_obj_center(eye);
    lv_obj_t *wire = plain(card, 30, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_t *plug = plain(card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 94, 0);
    s_plug_hint = card;
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_sky = plain(scr, SCREEN_W, SCREEN_H, DAY_SKY, 0);   // 底层天空
    lv_obj_set_pos(s_sky, 0, 0);

    make_day(scr);
    make_night(scr);
    lv_obj_add_flag(s_night_grp, LV_OBJ_FLAG_HIDDEN);     // 初始白天

    make_char(scr);
    make_zzz(scr);
    make_dots(scr);
    make_plug_hint(scr);

    bsp_display_unlock();
}

// ── 昼夜场景切换(整屏级重绘只在这一刻;自持锁)──────────────────────────────
static void apply_scene(void)
{
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_sky, lv_color_hex(s_night ? NIGHT_SKY : DAY_SKY), 0);
    if (s_night) {
        lv_obj_add_flag(s_day_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_night_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_open, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_eyes_shut, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_day_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_night_grp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_eyes_open, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eyes_shut, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

static void update_dots(void)   // 自持锁
{
    bsp_display_lock(0);
    for (int i = 0; i < WIN_CYCLES; i++) {
        lv_obj_set_style_bg_color(s_dots[i],
            lv_color_hex(i < s_reveals ? DOT_FULL : DOT_EMPTY), 0);
    }
    bsp_display_unlock();
}

static void apply_led_base(void)
{
    led_base_t b = s_night ? LED_BASE_IDLE : LED_BASE_AMBIENT;
    if ((int)b != s_led_base) {
        s_led_base = (int)b;
        ledstrip_fx_set_base(b);
    }
}

// 圆圆整体弹跳(醒来伸懒腰 / 庆祝;自持锁)
static void char_bounce(int lift, int up_ms, int down_ms)
{
    bsp_display_lock(0);
    lv_anim_delete(s_char, anim_set_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_char);
    lv_anim_set_exec_cb(&a, anim_set_y);
    lv_anim_set_values(&a, BODY_Y, BODY_Y - lift);
    lv_anim_set_duration(&a, up_ms);
    lv_anim_set_playback_duration(&a, down_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
    bsp_display_unlock();
}

// ── 庆祝(每 WIN_CYCLES 次天亮)────────────────────────────────────────────
static void burst_clear(void)   // 自持锁
{
    bsp_display_lock(0);
    for (int i = 0; i < BURST_COUNT; i++) {
        if (s_burst[i]) { lv_obj_delete(s_burst[i]); s_burst[i] = NULL; }
    }
    bsp_display_unlock();
}

static void celebrate(void)
{
    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
    char_bounce(24, 200, 240);

    bsp_display_lock(0);
    for (int i = 0; i < BURST_COUNT; i++) {   // 从圆圆中心迸发小星(限量,一次性)
        lv_obj_t *s = plain(lv_screen_active(), 12, 12, BURST_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s, BODY_CX - 6, BODY_CY - 6);
        s_burst[i] = s;
        int ang = i * (360 / BURST_COUNT);
        int dx = (int)(96 * cosf(ang * 3.14159f / 180));
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
        lv_anim_set_values(&ay, BODY_CY - 6, BODY_CY - 6 + dy);
        lv_anim_set_duration(&ay, 520);
        lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
        lv_anim_start(&ay);
    }
    bsp_display_unlock();

    s_win_frames = WIN_HOLD_MS / POLL_PERIOD_MS;
    ESP_LOGI(TAG, "攒够 %d 次天亮 → 庆祝!", WIN_CYCLES);
}

static void win_tick(void)   // 庆祝收场:清零重开
{
    if (--s_win_frames <= 0) {
        s_win_frames = 0;
        s_reveals    = 0;
        burst_clear();
        update_dots();
    }
}

// ── 昼夜转换 ──────────────────────────────────────────────────────────
static void go_night(void)
{
    s_night = true;
    apply_scene();
    // 入夜:柔和下行风铃 + 轻震 + 灯带转暗呼吸
    audio_fx_play_notes((audio_note_t[]){ { 587, 130, 45 }, { 440, 170, 40 } }, 2);
    haptics_play(HAPTIC_BUMP_LIGHT);
    apply_led_base();
    ESP_LOGI(TAG, "捂住 → 入夜(lux %.0f / ref %.0f)", s_lux, s_ref);
}

static void go_day(void)
{
    s_night = false;
    apply_scene();
    // 天亮 = 躲猫猫揭晓:上扬"早安"鸡鸣 + 轻震 + 灯带亮 + 一圈扫光 + 圆圆伸懒腰
    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_WAKE);
    ledstrip_fx_trigger(LED_FX_COLLECT);
    apply_led_base();
    char_bounce(16, 150, 190);

    if (s_reveals < WIN_CYCLES) s_reveals++;
    update_dots();
    ESP_LOGI(TAG, "松开 → 天亮!躲猫猫 %d/%d(lux %.0f / ref %.0f)",
             s_reveals, WIN_CYCLES, s_lux, s_ref);
    if (s_reveals >= WIN_CYCLES && s_win_frames == 0) celebrate();
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
    if (unit_dlight_init(core2_board_port_a(), 0) != ESP_OK) return false;
    s_since_read = 0;
    s_err_streak = 0;
    s_unit_ok    = true;
    s_first      = true;    // 重建自适应基准(可能换了房间/刚复电)
    plug_hint_show(false);
    if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
    ESP_LOGI(TAG, "DLight 已接管");
    return true;
}

static void unit_lost(void)
{
    s_unit_ok      = false;
    s_retry_frames = 0;
    plug_hint_show(true);
    audio_fx_play(SND_BUMP_MED);   // 温柔一声"咦?"
    ESP_LOGW(TAG, "DLight 失联(拔线/断电?),转入重试探测");
}

// ── 一次新照度读数:更新基准 + 昼夜判定 ─────────────────────────────────
static void on_lux(float lux, core2_sleep_stage_t stage)
{
    if (s_first) {
        s_lux      = lux;
        s_ref      = lux > REF_MIN_LUX ? lux : REF_MIN_LUX;
        s_last_lux = lux;
        s_first    = false;
    } else {
        s_lux += (lux - s_lux) * LUX_ALPHA;
    }

    // 自适应基准:向上跟得快(松手秒回房间亮度),向下跟得极慢(捂住时基准几乎不掉)
    if (s_lux > s_ref) s_ref += (s_lux - s_ref) * REF_RISE;
    else               s_ref += (s_lux - s_ref) * REF_FALL;
    if (s_ref < REF_MIN_LUX) s_ref = REF_MIN_LUX;

    // 手在捂/松 = 照度在变 = 有人玩(打盹中也能被这唤醒)
    if (fabsf(s_lux - s_last_lux) > s_ref * MOVE_FRAC) {
        core2_sleep_kick(&s_sleep);
        if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
    }
    s_last_lux = s_lux;

    if (stage != CORE2_SLEEP_AWAKE) return;

    // 昼夜迟滞:捂住到基准的 COVER_FRAC 以下入夜;松开到 UNCOVER_FRAC 以上天亮
    if (!s_night && s_lux < s_ref * COVER_FRAC)      go_night();
    else if (s_night && s_lux > s_ref * UNCOVER_FRAC) go_day();
}

// ── DLight 轮询 ───────────────────────────────────────────────────────
static void poll_dlight(core2_sleep_stage_t stage)
{
    if (!s_unit_ok) {
        if (++s_retry_frames >= UNIT_RETRY_MS / POLL_PERIOD_MS) {
            s_retry_frames = 0;
            if (unit_attach(true)) {
                s_retry_count = 0;
            } else if (++s_retry_count % 15 == 0) {
                // 久等不来:扫总线自诊断,被拉死则断电重启单元
                bool found = core2_board_port_a_scan();
                if (found || core2_board_port_a_stuck()) core2_board_port_a_recover();
            }
        }
        return;
    }

    if (++s_since_read < DLIGHT_READ_TICKS) return;
    s_since_read = 0;

    float lux = 0;
    esp_err_t r = unit_dlight_read_lux(&lux);
    if (r == ESP_OK) {
        s_err_streak = 0;
        on_lux(lux, stage);
    } else {
        if (++s_err_streak >= ERR_STREAK_LOST) unit_lost();   // 连续失败 = 拔线
    }
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
                                        have);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);

        // 深度省电切过 M-Bus 5V → DLight 掉电复位:醒来后重新接管(重配连续模式)
        if (s_prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_unit_ok = false;
            unit_attach(false);
        }
        s_prev_stage = stage;
        // 非清醒态:core2_sleep 托管灯带(IDLE/OFF);置 -1 使回清醒时强制重设本地基础模式
        if (stage != CORE2_SLEEP_AWAKE) s_led_base = -1;

        if (stage != CORE2_SLEEP_DEEP) poll_dlight(stage);

        if (stage == CORE2_SLEEP_AWAKE) {
            apply_led_base();                 // 回清醒时按当前昼夜重设灯带
            if (s_win_frames > 0) win_tick();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void peekaboo_game_start(void)
{
    s_night    = false;
    s_first    = true;
    s_reveals  = 0;
    s_win_frames = 0;
    s_led_base = -1;

    ui_create();

    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    scfg.nap_after_ms     = NAP_AFTER_MS;
    scfg.deep_after_ms    = DEEP_AFTER_MS;
    scfg.awake_brightness = PLAY_BRIGHTNESS;
    scfg.nap_brightness   = NAP_BRIGHTNESS;
    scfg.frame_ms         = POLL_PERIOD_MS;
    core2_sleep_init(&s_sleep, &scfg);

    ledstrip_fx_set_base(LED_BASE_AMBIENT);
    s_led_base = (int)LED_BASE_AMBIENT;

    bool attached = unit_attach(false);
    if (!attached) {
        ESP_LOGW(TAG, "DLight 未就位:必须插 Core2 机身侧面的红色 PORT.A 口"
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

    xTaskCreate(game_task, "peekaboo", 4096, NULL, 5, NULL);
}
