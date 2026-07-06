// 旋钮忙碌台 —— 游戏主体
//
// 模型:8 根彩虹音柱,档位 level[i] 0~LEVEL_MAX,一一对应 8Encoder 的 8 个旋钮。
//   转旋钮  → 音柱升降(7px/档)+ 五声音阶"叮"(音高=高度)+ 旋钮就地灯变亮
//   按旋钮  → 柱顶小脸"唱歌":弹跳一下 + 长音 + 轻震 + 就地灯闪白
//   全拉满  → 庆祝(SND_WIN + 三连震 + 底座灯彩虹 + 音柱波浪弹跳 + 旋钮灯跑马)
//             然后音柱缓缓落回 0,重新开玩(唯一"彩蛋",非必经,零失败)
//   拨动开关 → 白天/黑夜换景(天色 + 太阳↔月亮 + 星星,轻快琶音)
//
// 渲染纪律(CLAUDE.md §9):每根音柱是一个 LVGL 对象,只在档位变化时改高度
// (脏矩形 ≈ 34×高度差),背景/太阳/星星全静态;绝无整屏重绘。
// 省电:core2_sleep 托管;**旋钮活动也算"有人玩"**(桌面玩法机身不动,必须 kick,
// 否则玩着玩着打盹)。深度省电切 M-Bus 5V → 8Encoder 断电,拿起机身才能唤醒;
// 恢复后单元已复位,要重写就地灯 + 重建开关基线(防幻影翻转)。

#include "knobs_game.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "ledstrip_fx.h"
#include "unit_8encoder.h"

#include "tuning.h"

static const char *TAG = "knobs_game";

#define SCREEN_W  320
#define SCREEN_H  240

// ── 配色(§18 家族:大块扁平圆润 + 暖色)────────────────────────────────
static const uint32_t COL_COLOR[KNOB_COUNT] = {
    0xFF9E80, 0xFB8B24, 0xFFC75F, 0xA7C957,   // 珊瑚橙 / 橙 / 蜜黄 / 草绿
    0x7FD0C0, 0x4FB0D8, 0x9C9AD0, 0xFF8FB0,   // 薄荷 / 海蓝 / 星紫 / 糖粉
};
#define BG_DAY      0xF6EED9   // 暖米(压亮度)
#define BG_NIGHT    0x4A4A78   // 星空云蓝(非纯黑,§13)
#define SUN_COLOR   0xFFC75F
#define MOON_COLOR  0xF4EDC9
#define STAR_COLOR  0xFFE89B
#define EYE_WHITE   0xFFFFFF
#define EYE_PUPIL   0x3A3A38
#define MOUTH_COLOR 0xFB8B24

// C 大调五声音阶两个八度(怎么乱转都和谐);音高 = 音柱高度
static const uint16_t PENTA_HZ[] = { 262, 294, 330, 392, 440, 523, 587, 659, 784, 880, 1047 };
#define PENTA_N (sizeof(PENTA_HZ) / sizeof(PENTA_HZ[0]))

// ── 状态 ─────────────────────────────────────────────────────────────
typedef enum { ST_PLAY = 0, ST_WIN_BOUNCE, ST_WIN_SINK } state_t;

static state_t s_state;
static int     s_frame;                      // 当前状态帧计数
static int     s_level[KNOB_COUNT];          // 档位 0~LEVEL_MAX
static int     s_acc[KNOB_COUNT];            // 编码器计数累加器(ENC_COUNTS_PER_LEVEL>1 时用)
static bool    s_btn[KNOB_COUNT];            // 按键上一帧状态(边沿检测)
static int     s_led_flash[KNOB_COUNT];      // 就地灯闪白倒计时(帧)
static bool    s_switch;                     // 拨动开关上一帧状态
static bool    s_night;
static bool    s_unit_ok;
static int     s_retry_frames;               // 单元缺席时的重试倒计时
static int     s_retry_count;                // 连续重试失败次数(每 15 次扫一遍总线自诊断)
static int     s_err_streak;                 // 连续 I2C 失败计数(拔线检测)

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

// 趣味增量:图案彩蛋 + 摇一摇洗牌
typedef enum { PAT_NONE = 0, PAT_EQUAL, PAT_UP, PAT_DOWN, PAT_MOUNTAIN, PAT_VALLEY } pattern_t;
static pattern_t s_last_pattern;        // 上次识别到的队形(防同一图案反复触发)
static float     s_prev_acc[3];         // 摇一摇:上一帧加速度
static bool      s_prev_acc_valid;
static int       s_shake_hits;          // 攒够 SHAKE_NEEDED 下算一次摇(带泄漏)
static int       s_shake_cooldown;      // 触发后冷却帧数

// ── LVGL 对象 ─────────────────────────────────────────────────────────
static lv_obj_t *s_sun;                      // 白天=太阳,黑夜=月亮(换色)
static lv_obj_t *s_stars[3];                 // 黑夜限定小星星
static lv_obj_t *s_col[KNOB_COUNT];          // 音柱(小脸是其子对象,随柱顶走)
static lv_obj_t *s_plug_hint;                // "去插旋钮"无字提示卡(单元缺席时显示)

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

static inline int col_x(int i)      { return 3 + i * (COL_W + COL_GAP); }
static inline int level_h(int lv)   { return COL_H_MIN + (COL_H_MAX - COL_H_MIN) * lv / LEVEL_MAX; }
static inline uint16_t level_hz(int lv) { return PENTA_HZ[lv * (PENTA_N - 1) / LEVEL_MAX]; }

// 音柱高度统一走这个(柱底伸出屏外 COL_RADIUS,视觉上底是平的);也是 lv_anim 的 exec 回调
static void col_set_h(void *var, int32_t h)
{
    lv_obj_t *col = (lv_obj_t *)var;
    lv_obj_set_y(col, SCREEN_H - (int)h);
    lv_obj_set_height(col, (int)h + COL_RADIUS);
}

static int col_get_h(int i) { return lv_obj_get_height(s_col[i]) - COL_RADIUS; }

// 256 色环 → RGB(庆祝跑马灯用)
static void hue_rgb(uint8_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t seg = h / 43, rem = (h - seg * 43) * 6;
    switch (seg) {
        case 0:  *r = 255;       *g = rem;       *b = 0;         break;
        case 1:  *r = 255 - rem; *g = 255;       *b = 0;         break;
        case 2:  *r = 0;         *g = 255;       *b = rem;       break;
        case 3:  *r = 0;         *g = 255 - rem; *b = 255;       break;
        case 4:  *r = rem;       *g = 0;         *b = 255;       break;
        default: *r = 255;       *g = 0;         *b = 255 - rem; break;
    }
}

// ── 8Encoder 就地灯 ──────────────────────────────────────────────────
static void knob_led_show_level(int i)
{
    uint32_t c = COL_COLOR[i];
    int v = KNOB_LED_FLOOR + (KNOB_LED_MAX - KNOB_LED_FLOOR) * s_level[i] / LEVEL_MAX;
    unit_8encoder_set_led(i, ((c >> 16) & 0xFF) * v / 255,
                             ((c >> 8) & 0xFF) * v / 255,
                             (c & 0xFF) * v / 255);
}

static void knob_led_show_switch(void)
{
    // LED8 = 拨动开关就地灯:白天暖黄 / 黑夜靛蓝(低亮常驻,当"这里还有个开关"的线索)
    if (s_night) unit_8encoder_set_led(8, 18, 16, 60);
    else         unit_8encoder_set_led(8, 60, 44, 10);
}

static void knob_leds_show_all(void)
{
    for (int i = 0; i < KNOB_COUNT; i++) knob_led_show_level(i);
    knob_led_show_switch();
}

// ── 场景(白天/黑夜)────────────────────────────────────────────────────
static void scene_apply(void)   // 调用方持有 display lock
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(s_night ? BG_NIGHT : BG_DAY), 0);
    lv_obj_set_style_bg_color(s_sun, lv_color_hex(s_night ? MOON_COLOR : SUN_COLOR), 0);
    for (int i = 0; i < 3; i++) {
        if (s_night) lv_obj_remove_flag(s_stars[i], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(s_stars[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void scene_toggle_feedback(void)
{
    if (s_night) {
        audio_fx_play_notes((audio_note_t[]){ { 659, 80, 50 }, { 392, 140, 45 } }, 2);  // 下行=入夜
    } else {
        audio_fx_play_notes((audio_note_t[]){ { 523, 70, 55 }, { 784, 120, 55 } }, 2);  // 上行=天亮
    }
    haptics_play(HAPTIC_BUMP_LIGHT);
}

// ── UI 搭建 ───────────────────────────────────────────────────────────
static void make_column(lv_obj_t *scr, int i)
{
    lv_obj_t *col = plain(scr, COL_W, COL_H_MIN + COL_RADIUS, COL_COLOR[i], COL_RADIUS);
    lv_obj_set_x(col, col_x(i));
    s_col[i] = col;
    col_set_h(col, level_h(0));

    // 柱顶小脸(子对象,TOP 对齐 → 永远贴着柱顶随高度走)
    lv_obj_t *el = plain(col, 8, 8, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(el, LV_ALIGN_TOP_MID, -7, 5);
    lv_obj_t *er = plain(col, 8, 8, EYE_WHITE, LV_RADIUS_CIRCLE);
    lv_obj_align(er, LV_ALIGN_TOP_MID, 7, 5);
    lv_obj_t *pl = plain(el, 4, 4, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_center(pl);
    lv_obj_t *pr = plain(er, 4, 4, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_center(pr);
    lv_obj_t *mouth = plain(col, 10, 4, MOUTH_COLOR, 2);
    lv_obj_align(mouth, LV_ALIGN_TOP_MID, 0, 15);
}

static void make_plug_hint(lv_obj_t *scr)
{
    // 无字提示卡:一个大旋钮 + 引线 + 插头(给家长看的"去插 8Encoder";幼儿看到大圆点也无害)
    lv_obj_t *card = plain(scr, 116, 72, 0xFFFFFF, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -36);
    lv_obj_t *knob = plain(card, 40, 40, 0x9C9AD0, LV_RADIUS_CIRCLE);
    lv_obj_align(knob, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *dot = plain(knob, 6, 12, 0xFFFFFF, 3);          // 旋钮指针
    lv_obj_align(dot, LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_t *wire = plain(card, 34, 4, 0x3A3A38, 2);          // 引线
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 52, 0);
    lv_obj_t *plug = plain(card, 16, 20, 0x3A3A38, 4);         // 插头
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 86, 0);
    s_plug_hint = card;
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
}

static void ui_create(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_sun = plain(scr, 36, 36, SUN_COLOR, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_sun, 12, 8);
    const int star_pos[3][2] = { { 80, 16 }, { 170, 26 }, { 255, 12 } };
    for (int i = 0; i < 3; i++) {
        s_stars[i] = plain(scr, 6, 6, STAR_COLOR, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s_stars[i], star_pos[i][0], star_pos[i][1]);
    }

    for (int i = 0; i < KNOB_COUNT; i++) make_column(scr, i);
    make_plug_hint(scr);
    scene_apply();

    bsp_display_unlock();
}

static void plug_hint_show(bool show)
{
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

// ── 单元接管 / 基线 ───────────────────────────────────────────────────
// (重)接管 8Encoder:清增量存量、按当前物理状态建基线(不触发事件)、重写就地灯
static bool unit_attach(bool greet)
{
    if (unit_8encoder_init(core2_board_port_a(), 0) != ESP_OK) return false;

    (void)unit_8encoder_read_buttons(s_btn);     // 失败就保持旧基线,无碍
    bool sw = s_switch;
    if (unit_8encoder_read_switch(&sw) == ESP_OK && sw != s_switch) {
        s_switch = sw;                           // 开机就按物理开关位摆场景,不放翻转音
        s_night  = sw;
        bsp_display_lock(0);
        scene_apply();
        bsp_display_unlock();
    }
    knob_leds_show_all();
    s_err_streak = 0;
    s_unit_ok    = true;
    plug_hint_show(false);
    if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
    ESP_LOGI(TAG, "8Encoder 已接管");
    return true;
}

static void unit_lost(void)
{
    s_unit_ok      = false;
    s_retry_frames = 0;
    plug_hint_show(true);
    audio_fx_play(SND_BUMP_MED);   // 温柔一声"咦?"
    ESP_LOGW(TAG, "8Encoder 失联(拔线/断电?),转入重试探测");
}

// ── 图案彩蛋:摆出漂亮队形(阶梯/等高/山/谷)给个小庆祝 ──────────────────
// 不改档位、不重置——找到就继续玩;声音按柱高左→右扫,天然"听出形状"。
static pattern_t detect_pattern(void)
{
    bool equal = true, up = true, down = true;
    for (int i = 1; i < KNOB_COUNT; i++) {
        if (s_level[i] != s_level[i - 1])   equal = false;
        if (!(s_level[i] > s_level[i - 1])) up    = false;
        if (!(s_level[i] < s_level[i - 1])) down  = false;
    }
    if (equal) return (s_level[0] > 0) ? PAT_EQUAL : PAT_NONE;  // 全 0 是初始/重置态,不算
    if (up)    return PAT_UP;
    if (down)  return PAT_DOWN;

    // 山形:严格升到单峰再严格降(峰不在两端)
    int i = 1;
    while (i < KNOB_COUNT && s_level[i] > s_level[i - 1]) i++;
    if (i - 1 > 0 && i - 1 < KNOB_COUNT - 1) {
        bool ok = true;
        for (; i < KNOB_COUNT; i++) if (!(s_level[i] < s_level[i - 1])) { ok = false; break; }
        if (ok) return PAT_MOUNTAIN;
    }
    // 谷形:严格降到单谷再严格升
    i = 1;
    while (i < KNOB_COUNT && s_level[i] < s_level[i - 1]) i++;
    if (i - 1 > 0 && i - 1 < KNOB_COUNT - 1) {
        bool ok = true;
        for (; i < KNOB_COUNT; i++) if (!(s_level[i] > s_level[i - 1])) { ok = false; break; }
        if (ok) return PAT_VALLEY;
    }
    return PAT_NONE;
}

// "演奏这一排":按当前 8 柱高度左→右弹一串音(8 音正好一次 play_notes,<400ms)
static void play_row_arp(uint8_t amp)
{
    audio_note_t seq[KNOB_COUNT];
    for (int i = 0; i < KNOB_COUNT; i++) {
        seq[i].freq_hz = level_hz(s_level[i]);
        seq[i].ms      = ARP_MS;
        seq[i].amp     = amp;
    }
    audio_fx_play_notes(seq, KNOB_COUNT);
}

// 8 柱错峰小跳一下(左→右波浪,与 arp 同向),跳完各自回到本档高度
static void wave_bounce(int lift, int step_ms)
{
    bsp_display_lock(0);
    for (int i = 0; i < KNOB_COUNT; i++) {
        int h = level_h(s_level[i]);
        lv_anim_delete(s_col[i], col_set_h);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_col[i]);
        lv_anim_set_exec_cb(&a, col_set_h);
        lv_anim_set_values(&a, h, h + lift);
        lv_anim_set_duration(&a, 110);
        lv_anim_set_playback_duration(&a, 150);
        lv_anim_set_delay(&a, i * step_ms);
        lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
        lv_anim_start(&a);
    }
    bsp_display_unlock();
}

static void pattern_reward(void)
{
    play_row_arp(ARP_AMP);
    wave_bounce(12, 45);
    ledstrip_fx_trigger(LED_FX_COLLECT);   // 底座扫一圈
    haptics_play(HAPTIC_COLLECT);
}

// ── 玩法:转 / 按 / 拨 ────────────────────────────────────────────────
static void enter_win(void);

static void apply_rotation(int i, int32_t inc)
{
    s_acc[i] += ENC_DIR * (int)inc;
    int delta = s_acc[i] / ENC_COUNTS_PER_LEVEL;
    if (delta == 0) return;
    s_acc[i] -= delta * ENC_COUNTS_PER_LEVEL;

    int lv = s_level[i] + delta;
    if (lv < 0) lv = 0;
    if (lv > LEVEL_MAX) lv = LEVEL_MAX;

    // 顶/底继续转:柱子不动也给声音(输入永远有回应,零失败)
    audio_fx_play_notes((audio_note_t[]){ { level_hz(lv), TICK_MS, TICK_AMP } }, 1);
    if (lv == s_level[i]) return;

    bool hit_top = (lv == LEVEL_MAX && s_level[i] < LEVEL_MAX);
    s_level[i] = lv;

    bsp_display_lock(0);
    lv_anim_delete(s_col[i], col_set_h);          // 掐掉残留弹跳动画再手动定高
    col_set_h(s_col[i], level_h(lv));
    bsp_display_unlock();

    knob_led_show_level(i);

    if (hit_top) {                                 // "咔哒到顶"的小满足感
        audio_fx_play(SND_COLLECT);
        haptics_play(HAPTIC_COLLECT);
    }

    bool all_max = true;
    for (int k = 0; k < KNOB_COUNT; k++) {
        if (s_level[k] != LEVEL_MAX) { all_max = false; break; }
    }
    if (all_max) { enter_win(); return; }

    // 摆成漂亮队形 → 小庆祝(同一图案只贺一次,变了才重贺)
    pattern_t p = detect_pattern();
    if (p != s_last_pattern) {
        s_last_pattern = p;
        if (p != PAT_NONE) pattern_reward();
    }
}

static void sing(int i)
{
    int h = level_h(s_level[i]);
    bsp_display_lock(0);
    lv_anim_delete(s_col[i], col_set_h);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_col[i]);
    lv_anim_set_exec_cb(&a, col_set_h);
    lv_anim_set_values(&a, h, h + 14);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_playback_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
    lv_anim_start(&a);
    bsp_display_unlock();

    audio_fx_play_notes((audio_note_t[]){ { level_hz(s_level[i]), SING_MS, SING_AMP } }, 1);
    haptics_play(HAPTIC_COLLECT);

    unit_8encoder_set_led(i, 255, 255, 255);       // 就地灯闪白
    s_led_flash[i] = 5;                            // ~165ms 后回档位色
}

// ── 全拉满庆祝 ────────────────────────────────────────────────────────
static void enter_win(void)
{
    s_state = ST_WIN_BOUNCE;
    s_frame = 0;
    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);

    bsp_display_lock(0);
    for (int i = 0; i < KNOB_COUNT; i++) {         // 波浪弹跳(错峰起跳,循环)
        lv_anim_delete(s_col[i], col_set_h);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_col[i]);
        lv_anim_set_exec_cb(&a, col_set_h);
        lv_anim_set_values(&a, COL_H_MAX, COL_H_MAX + 16);
        lv_anim_set_duration(&a, WIN_BOUNCE_MS);
        lv_anim_set_playback_duration(&a, WIN_BOUNCE_MS);
        lv_anim_set_delay(&a, i * 70);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
    bsp_display_unlock();
    ESP_LOGI(TAG, "八柱全满 → 庆祝!");
}

static void win_bounce_tick(void)
{
    s_frame++;
    if (s_unit_ok && s_frame % 3 == 0) {           // 旋钮灯跑马(10Hz,9 颗)
        for (int i = 0; i < UNIT_8ENCODER_NUM_LEDS; i++) {
            uint8_t r, g, b;
            hue_rgb((uint8_t)(s_frame * 8 + i * 28), &r, &g, &b);
            unit_8encoder_set_led(i, r * KNOB_LED_MAX / 255, g * KNOB_LED_MAX / 255,
                                  b * KNOB_LED_MAX / 255);
        }
    }
    if (s_frame >= WIN_HOLD_MS / POLL_PERIOD_MS) { // 收场:缓缓落回 0
        s_state = ST_WIN_SINK;
        s_frame = 0;
        for (int i = 0; i < KNOB_COUNT; i++) s_level[i] = 0;
        s_last_pattern = PAT_NONE;
        bsp_display_lock(0);
        for (int i = 0; i < KNOB_COUNT; i++) {
            lv_anim_delete(s_col[i], col_set_h);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_col[i]);
            lv_anim_set_exec_cb(&a, col_set_h);
            lv_anim_set_values(&a, col_get_h(i), level_h(0));
            lv_anim_set_duration(&a, SINK_MS);
            lv_anim_set_delay(&a, i * 40);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            lv_anim_start(&a);
        }
        bsp_display_unlock();
        if (s_unit_ok) knob_leds_show_all();
    }
}

static void win_sink_tick(void)
{
    s_frame++;
    if (s_frame >= (SINK_MS + KNOB_COUNT * 40 + 200) / POLL_PERIOD_MS) {
        s_state = ST_PLAY;
        s_frame = 0;
    }
}

// ── 单元轮询(清醒 + 打盹都跑;深度省电时 5V 已切,不跑)────────────────
static void poll_unit(core2_sleep_stage_t stage)
{
    if (!s_unit_ok) {
        if (++s_retry_frames >= UNIT_RETRY_MS / POLL_PERIOD_MS) {
            s_retry_frames = 0;
            if (unit_attach(true)) {
                s_retry_count = 0;
            } else if (++s_retry_count % 15 == 0) {
                // 久等不来:扫总线自诊断;被拉死(单元卡死拽线)或扫到器件却连不上 0x41
                // (困在 bootloader 0x54)→ 断电重启单元自愈,下个 2s 重试即接管
                bool found = core2_board_port_a_scan();
                if (found || core2_board_port_a_stuck()) {
                    core2_board_port_a_recover();
                }
            }
        }
        return;
    }

    int32_t inc[KNOB_COUNT];
    bool    btn[KNOB_COUNT];
    bool    sw = s_switch;
    esp_err_t err = unit_8encoder_read_increments(inc);
    if (err == ESP_OK) err = unit_8encoder_read_buttons(btn);
    if (err == ESP_OK) err = unit_8encoder_read_switch(&sw);
    if (err != ESP_OK) {
        if (++s_err_streak >= 20) unit_lost();     // 偶发失败忍,连续失败判拔线
        return;
    }
    s_err_streak = 0;

    bool awake_play = (stage == CORE2_SLEEP_AWAKE && s_state == ST_PLAY);
    bool activity   = false;

    for (int i = 0; i < KNOB_COUNT; i++) {
        if (inc[i] != 0) {
            activity = true;
            if (awake_play) apply_rotation(i, inc[i]);
        }
        if (btn[i] != s_btn[i]) {
            s_btn[i] = btn[i];
            if (btn[i]) {
                activity = true;
                if (awake_play) sing(i);
            }
        }
        if (s_led_flash[i] > 0 && --s_led_flash[i] == 0) knob_led_show_level(i);
    }

    if (sw != s_switch) {
        s_switch = sw;
        activity = true;
        if (stage == CORE2_SLEEP_AWAKE) {
            s_night = sw;
            bsp_display_lock(0);
            scene_apply();
            bsp_display_unlock();
            scene_toggle_feedback();
            knob_led_show_switch();
        }
    }

    if (activity) {
        core2_sleep_kick(&s_sleep);                // 桌面玩法机身不动,旋钮活动=有人玩
        if (stage != CORE2_SLEEP_AWAKE) core2_sleep_wake(&s_sleep);
    }
}

// ── 摇一摇:把 8 柱洗成一个漂亮队形(用上平时只做休眠检测的 IMU)────────────
static void shuffle_targets(int out[KNOB_COUNT])
{
    switch (esp_random() % 3) {
    case 0:  // 上楼梯
        for (int i = 0; i < KNOB_COUNT; i++) out[i] = LEVEL_MAX * i / (KNOB_COUNT - 1);
        break;
    case 1:  // 下楼梯
        for (int i = 0; i < KNOB_COUNT; i++) out[i] = LEVEL_MAX * (KNOB_COUNT - 1 - i) / (KNOB_COUNT - 1);
        break;
    default: // 小山
        for (int i = 0; i < KNOB_COUNT; i++) {
            int m = (i < KNOB_COUNT / 2) ? i : (KNOB_COUNT - 1 - i);
            out[i] = LEVEL_MAX * m / (KNOB_COUNT / 2 - 1);
        }
        break;
    }
    for (int i = 0; i < KNOB_COUNT; i++) {
        if (out[i] < 0) out[i] = 0;
        if (out[i] > LEVEL_MAX) out[i] = LEVEL_MAX;
    }
}

static void trigger_shuffle(void)
{
    int tgt[KNOB_COUNT];
    shuffle_targets(tgt);

    bsp_display_lock(0);
    for (int i = 0; i < KNOB_COUNT; i++) {
        int from    = col_get_h(i);
        s_level[i]  = tgt[i];
        s_acc[i]    = 0;
        lv_anim_delete(s_col[i], col_set_h);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_col[i]);
        lv_anim_set_exec_cb(&a, col_set_h);
        lv_anim_set_values(&a, from, level_h(tgt[i]));
        lv_anim_set_duration(&a, SHUFFLE_MS);
        lv_anim_set_delay(&a, i * 30);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    bsp_display_unlock();

    play_row_arp(60);                    // 哗啦一声滑音(按新队形高度)
    haptics_play(HAPTIC_BUMP_MED);
    ledstrip_fx_trigger(LED_FX_WIN);
    if (s_unit_ok) knob_leds_show_all();

    s_last_pattern = detect_pattern();   // 记住新队形,别紧接着又判成图案彩蛋
    ESP_LOGI(TAG, "摇一摇 → 洗成新队形");
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

        // 深度省电切过 M-Bus 5V → 8Encoder 掉电复位:醒来后重新接管(重写灯/基线)
        if (s_prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_unit_ok = false;
            unit_attach(false);
        }
        s_prev_stage = stage;

        // 摇一摇 → 洗成新队形(用上平时只做休眠检测的 IMU)
        if (s_shake_cooldown > 0) s_shake_cooldown--;
        if (have && s_prev_acc_valid) {
            float d = fabsf(acc.x - s_prev_acc[0]) + fabsf(acc.y - s_prev_acc[1])
                    + fabsf(acc.z - s_prev_acc[2]);
            if (d > SHAKE_THRESH) {
                if (s_shake_hits < SHAKE_NEEDED) s_shake_hits++;
                if (s_shake_hits >= SHAKE_NEEDED && s_shake_cooldown == 0 &&
                    stage == CORE2_SLEEP_AWAKE && s_state == ST_PLAY && s_unit_ok) {
                    trigger_shuffle();
                    s_shake_cooldown = SHAKE_COOLDOWN_MS / POLL_PERIOD_MS;
                    s_shake_hits     = 0;
                }
            } else if (s_shake_hits > 0) {
                s_shake_hits--;   // 泄漏:要连着晃几下,单次磕碰会被漏掉
            }
        }
        if (have) {
            s_prev_acc[0] = acc.x; s_prev_acc[1] = acc.y; s_prev_acc[2] = acc.z;
            s_prev_acc_valid = true;
        }

        if (stage != CORE2_SLEEP_DEEP) poll_unit(stage);

        if (stage == CORE2_SLEEP_AWAKE) {
            switch (s_state) {
                case ST_PLAY:       break;         // 输入在 poll_unit 里就地处理
                case ST_WIN_BOUNCE: win_bounce_tick(); break;
                case ST_WIN_SINK:   win_sink_tick();   break;
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void knobs_game_start(void)
{
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
        ESP_LOGW(TAG, "8Encoder 未就位:必须插 Core2 机身侧面的红色 PORT.A 口"
                      "(底座黑口 PORT.B/蓝口 PORT.C 不是 I2C)");
        // 自诊断 + 自愈:总线被拉死(单元卡死拽线)或扫到了器件却不是 0x41
        // (典型=8Encoder 困在 bootloader 0x54)→ 断电重启单元,再试一次
        bool found = core2_board_port_a_scan();
        if (found || core2_board_port_a_stuck()) {
            core2_board_port_a_recover();
            attached = unit_attach(false);
            if (!attached) core2_board_port_a_scan();   // 复原后仍不在 0x41:让真凶现形(0x54?又拽死?)
        }
    }
    if (attached) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    } else {
        plug_hint_show(true);                // 没插单元也能开机:出提示卡,低频重试,插上即"你好"
        audio_fx_play(SND_BUMP_MED);
    }

    xTaskCreate(game_task, "knobs", 4096, NULL, 5, NULL);
}
