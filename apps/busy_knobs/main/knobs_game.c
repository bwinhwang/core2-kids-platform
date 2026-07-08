// 旋钮忙碌台 —— 游戏主体
//
// 模型:8 根彩虹音柱,档位 level[i] 0~LEVEL_MAX,一一对应 8Encoder 的 8 个旋钮。
//   转旋钮  → 音柱升降(7px/档)+ 五声音阶"叮"(音高=高度)+ 旋钮就地灯变亮
//   按旋钮  → 柱顶小脸"唱歌":弹跳一下 + 长音 + 轻震 + 就地灯闪白
//   全拉满  → 庆祝(SND_WIN + 三连震 + 底座灯彩虹 + 音柱波浪弹跳 + 旋钮灯跑马)
//             然后音柱缓缓落回 0,重新开玩(唯一"彩蛋",非必经,零失败)
//   拨动开关 → 白天/黑夜换景(天色 + 太阳↔月亮 + 星星,轻快琶音)
//
// 趣味增量第二批(FUN2_SPEC.md):柱顶小脸活化(眨眼/看向/嘴型/鼓腮)、
// 小鸟访客(自发拜访 + 图案彩蛋时逐柱蹦跳叙事)、图案彩蛋按形状差异化反馈、
// 夜晚音色下移 + 星星微闪、多键齐按和弦彩蛋。
//
// 渲染纪律(CLAUDE.md §9):每根音柱是一个 LVGL 对象,只在档位变化时改高度
// (脏矩形 ≈ 34×高度差),背景/太阳/星星全静态;绝无整屏重绘。本批新增视觉
// 同样只用不透明纯色块 + 小脏矩形,无逐帧 alpha。
// 省电:core2_sleep 托管;**旋钮活动也算"有人玩"**(桌面玩法机身不动,必须 kick,
// 否则玩着玩着打盹)。深度省电切 M-Bus 5V → 8Encoder 断电,拿起机身才能唤醒;
// 恢复后单元已复位,要重写就地灯 + 重建开关基线(防幻影翻转)。小脸/星星/小鸟
// 装饰 tick 只在 AWAKE 跑,不参与 core2_sleep_kick(不算"有人玩")。

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
#define BG_DAY          0xF6EED9   // 暖米(压亮度)
#define BG_NIGHT        0x4A4A78   // 星空云蓝(非纯黑,§13)
#define SUN_COLOR       0xFFC75F
#define MOON_COLOR      0xF4EDC9
#define STAR_COLOR      0xFFE89B
#define STAR_DIM_COLOR  0xC9B67A
#define EYE_WHITE       0xFFFFFF
#define EYE_PUPIL       0x3A3A38
#define MOUTH_COLOR     0xFB8B24
#define MOUTH_COLOR_OPEN 0xD46A1A   // 张嘴笑:深一号橙
#define CHEEK_COLOR     0xFFAFA3   // 到顶鼓腮:粉

#define BIRD_BODY_DAY   0xFFB35C
#define BIRD_BODY_NIGHT 0xB8B4E0
#define BIRD_BELLY      0xFFF1D6
#define BIRD_BEAK       0xFB8B24
#define BIRD_WING       0xCC8F4A   // 比身体深一号(昼夜不换,规格 §4.3 仅换身体色)

// C 大调五声音阶两个八度(怎么乱转都和谐);音高 = 音柱高度
static const uint16_t PENTA_HZ[] = { 262, 294, 330, 392, 440, 523, 587, 659, 784, 880, 1047 };
#define PENTA_N (sizeof(PENTA_HZ) / sizeof(PENTA_HZ[0]))
// 夜用音阶:整体下移纯四度(不是降八度——NS4168 小喇叭 200Hz 以下还原差),与 PENTA_HZ 等长
static const uint16_t PENTA_HZ_NIGHT[] =
    { 196, 220, 262, 294, 330, 392, 440, 523, 587, 659, 784 };

// ── 状态 ─────────────────────────────────────────────────────────────
typedef enum { ST_PLAY = 0, ST_WIN_BOUNCE, ST_WIN_SINK } state_t;
typedef enum { WAVE_L2R, WAVE_R2L, WAVE_IN, WAVE_OUT, WAVE_ALL } wave_dir_t;
typedef enum { BIRD_ABSENT = 0, BIRD_FLY_IN, BIRD_PERCHED, BIRD_FLY_OUT, BIRD_RIDE } bird_state_t;

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

// 趣味增量第二批:柱顶小脸(眨眼/看向/嘴型)
static int  s_mouth_tier[KNOB_COUNT];   // -1=未落地,0=平嘴,1=微笑,2=张嘴笑
static int  s_blink_col = -1;           // 当前眨眼柱(-1=无)
static int  s_blink_timer;              // 帧倒计时(idle→下次眨眼;closed→重新睁眼)
static bool s_blink_closed;             // 当前是否处于合眼阶段
static bool s_blink_double_pending;
static int  s_gaze_target = -1;         // 当前看向的柱(-1=回中)
static int  s_gaze_frames;

// 趣味增量第二批:小鸟访客
static bird_state_t s_bird_state = BIRD_ABSENT;
static int  s_bird_col = -1;            // 当前(或落点)所在柱
static int  s_bird_frame;               // 状态内帧计数
static int  s_bird_fly_ms;              // 本次飞入/飞出用的时长
static int  s_bird_visit;               // ABSENT:拜访倒计时(帧)
static int  s_bird_perch;               // PERCHED:栖息倒计时(帧)
static int  s_bird_perch_level;         // 栖息柱当时档位(检测变化用)
static int  s_bird_x, s_bird_y;         // 影子坐标(容器逻辑位置,随每次移动指令更新)
static bool s_bird_pending_ride;        // FLY_IN 落地后是否直接转 RIDE
static int  s_bird_pending_ride_to;
static int  s_bird_ride_dir;            // RIDE 方向 +1/-1
static int  s_bird_ride_target;         // RIDE 终点柱

// 趣味增量第二批:星星微闪
static bool s_star_bright[3];
static int  s_star_next[3];             // 帧倒计时

// 趣味增量第二批:多键齐按和弦
static bool s_chord_pending[KNOB_COUNT];
static int  s_chord_wait;               // 收集窗口倒计时(帧,0=未开窗)

// ── LVGL 对象 ─────────────────────────────────────────────────────────
static lv_obj_t *s_sun;                      // 白天=太阳,黑夜=月亮(换色)
static lv_obj_t *s_stars[3];                 // 黑夜限定小星星
static const int STAR_POS[3][2] = { { 80, 16 }, { 170, 26 }, { 255, 12 } };
static lv_obj_t *s_col[KNOB_COUNT];          // 音柱(小脸是其子对象,随柱顶走)
static lv_obj_t *s_eye[KNOB_COUNT][2];       // 眼白(左/右)
static lv_obj_t *s_pupil[KNOB_COUNT][2];     // 瞳孔(眼白的子对象)
static lv_obj_t *s_mouth[KNOB_COUNT];
static lv_obj_t *s_cheek[KNOB_COUNT][2];     // 鼓腮(默认 HIDDEN)
static lv_obj_t *s_plug_hint;                // "去插旋钮"无字提示卡(单元缺席时显示)
static lv_obj_t *s_bird;                     // 小鸟容器(透明,只移动它)
static lv_obj_t *s_bird_body;

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
static inline uint16_t level_hz(int lv)
{
    const uint16_t *table = s_night ? PENTA_HZ_NIGHT : PENTA_HZ;
    return table[lv * (PENTA_N - 1) / LEVEL_MAX];
}

// 秒区间随机转帧数(眨眼间隔 / 小鸟拜访 / 栖息时长 共用)
static inline int rand_range_frames(int min_s, int max_s)
{
    int ms = (min_s + (int)(esp_random() % (uint32_t)(max_s - min_s + 1))) * 1000;
    return ms / POLL_PERIOD_MS;
}

// 夜晚音色/响度统一走这几个(白天用 tuning.h 原值)
static inline uint16_t fx_tick_ms(void)  { return s_night ? TICK_MS_NIGHT  : TICK_MS; }
static inline uint8_t  fx_tick_amp(void) { return s_night ? TICK_AMP_NIGHT : TICK_AMP; }
static inline uint8_t  fx_sing_amp(void) { return s_night ? SING_AMP_NIGHT : SING_AMP; }
static inline uint8_t  fx_arp_amp(void)  { return s_night ? ARP_AMP_NIGHT  : ARP_AMP; }
static inline uint8_t  fx_bird_amp(void) { return s_night ? BIRD_NOTE_AMP_NIGHT : BIRD_NOTE_AMP; }

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
        // 星星复位为亮态(不论昼夜切哪个方向都重置,确保下次入夜从亮态起跳)
        lv_obj_set_size(s_stars[i], 6, 6);
        lv_obj_set_pos(s_stars[i], STAR_POS[i][0], STAR_POS[i][1]);
        lv_obj_set_style_bg_color(s_stars[i], lv_color_hex(STAR_COLOR), 0);
        s_star_bright[i] = true;
        s_star_next[i]   = TWINKLE_MIN_F + (int)(esp_random() % (TWINKLE_MAX_F - TWINKLE_MIN_F + 1));
        if (s_night) lv_obj_remove_flag(s_stars[i], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(s_stars[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(s_bird_body, lv_color_hex(s_night ? BIRD_BODY_NIGHT : BIRD_BODY_DAY), 0);
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
    s_eye[i][0] = el; s_eye[i][1] = er;

    // 瞳孔是眼白的子对象:LVGL 9 默认裁剪子对象到父对象区域(LV_OBJ_FLAG_OVERFLOW_VISIBLE
    // 默认不设),眼白压扁后瞳孔会被自动裁掉,无需手动 HIDDEN。
    lv_obj_t *pl = plain(el, 4, 4, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_center(pl);
    lv_obj_t *pr = plain(er, 4, 4, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_center(pr);
    s_pupil[i][0] = pl; s_pupil[i][1] = pr;

    lv_obj_t *mouth = plain(col, 10, 4, MOUTH_COLOR, 2);
    lv_obj_align(mouth, LV_ALIGN_TOP_MID, 0, 15);
    s_mouth[i] = mouth;

    lv_obj_t *cl = plain(col, 6, 6, CHEEK_COLOR, LV_RADIUS_CIRCLE);
    lv_obj_align(cl, LV_ALIGN_TOP_MID, -11, 15);
    lv_obj_add_flag(cl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *cr = plain(col, 6, 6, CHEEK_COLOR, LV_RADIUS_CIRCLE);
    lv_obj_align(cr, LV_ALIGN_TOP_MID, 11, 15);
    lv_obj_add_flag(cr, LV_OBJ_FLAG_HIDDEN);
    s_cheek[i][0] = cl; s_cheek[i][1] = cr;

    s_mouth_tier[i] = -1;   // 强制首次 face_set_mouth 落地(即使目标恰好是档 0 平嘴)
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

static void make_bird(lv_obj_t *scr)
{
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 22, 18);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    s_bird = c;

    lv_obj_t *body = plain(c, 16, 13, BIRD_BODY_DAY, 6);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    s_bird_body = body;
    lv_obj_t *belly = plain(body, 8, 6, BIRD_BELLY, LV_RADIUS_CIRCLE);
    lv_obj_align(belly, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_t *eye = plain(body, 3, 3, EYE_PUPIL, LV_RADIUS_CIRCLE);
    lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -3, 2);
    lv_obj_t *beak = plain(body, 5, 4, BIRD_BEAK, 1);
    lv_obj_align(beak, LV_ALIGN_RIGHT_MID, 4, 1);
    lv_obj_t *wing = plain(body, 7, 5, BIRD_WING, 2);
    lv_obj_align(wing, LV_ALIGN_LEFT_MID, 1, 1);
}

static void ui_create(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_sun = plain(scr, 36, 36, SUN_COLOR, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_sun, 12, 8);
    for (int i = 0; i < 3; i++) {
        s_stars[i] = plain(scr, 6, 6, STAR_COLOR, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s_stars[i], STAR_POS[i][0], STAR_POS[i][1]);
    }

    for (int i = 0; i < KNOB_COUNT; i++) make_column(scr, i);
    make_plug_hint(scr);
    make_bird(scr);
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

// ── 柱顶小脸:看向(瞳孔追随)──────────────────────────────────────────
// 只在 target 变化或超时归位时批量重对齐一次,不每帧对齐 16 颗瞳孔。
static void gaze_apply(int target)   // target=-1 表示回中
{
    bsp_display_lock(0);
    for (int j = 0; j < KNOB_COUNT; j++) {
        int dx = 0;
        if (target >= 0) dx = (target > j) ? GAZE_DX : (target < j) ? -GAZE_DX : 0;
        lv_obj_align(s_pupil[j][0], LV_ALIGN_CENTER, dx, 0);
        lv_obj_align(s_pupil[j][1], LV_ALIGN_CENTER, dx, 0);
    }
    bsp_display_unlock();
}

// ── 柱顶小脸:眨眼 ───────────────────────────────────────────────────
static void blink_set(int col, bool closed)
{
    bsp_display_lock(0);
    lv_obj_set_height(s_eye[col][0], closed ? 2 : 8);
    lv_obj_set_height(s_eye[col][1], closed ? 2 : 8);
    bsp_display_unlock();
}

static void blink_start(int col)
{
    s_blink_col    = col;
    s_blink_closed = true;
    s_blink_timer  = BLINK_FRAMES;
    s_blink_double_pending = (int)(esp_random() % 100) < BLINK_DOUBLE_PCT;
    blink_set(col, true);
}

static void face_blink_tick(void)
{
    if (--s_blink_timer > 0) return;
    if (s_blink_closed) {
        blink_set(s_blink_col, false);
        if (s_blink_double_pending) {
            s_blink_double_pending = false;
            s_blink_timer = BLINK_FRAMES;
            blink_set(s_blink_col, true);   // 紧接着再眨一次,仍处于"合眼"阶段
        } else {
            s_blink_closed = false;
            s_blink_col    = -1;
            s_blink_timer  = rand_range_frames(BLINK_MIN_S, BLINK_MAX_S);
        }
    } else {
        blink_start((int)(esp_random() % KNOB_COUNT));
    }
}

// 眨眼/看向仅在清醒 + ST_PLAY 跑(WIN/打盹中冻结,不必复位)
static void face_tick(void)
{
    if (s_gaze_target >= 0 && --s_gaze_frames <= 0) {
        s_gaze_target = -1;
        gaze_apply(-1);
    }
    face_blink_tick();
}

// ── 柱顶小脸:嘴随高度(3 档)+ 到顶鼓腮 ─────────────────────────────────
static void face_set_mouth(int i)
{
    int lv   = s_level[i];
    int tier = (lv >= MOUTH_OPEN_LV) ? 2 : (lv >= MOUTH_SMILE_LV) ? 1 : 0;
    if (tier != s_mouth_tier[i]) {
        s_mouth_tier[i] = tier;
        bsp_display_lock(0);
        switch (tier) {
        case 0:
            lv_obj_set_size(s_mouth[i], 10, 4);
            lv_obj_set_style_radius(s_mouth[i], 2, 0);
            lv_obj_set_style_bg_color(s_mouth[i], lv_color_hex(MOUTH_COLOR), 0);
            break;
        case 1:
            lv_obj_set_size(s_mouth[i], 14, 5);
            lv_obj_set_style_radius(s_mouth[i], 3, 0);
            lv_obj_set_style_bg_color(s_mouth[i], lv_color_hex(MOUTH_COLOR), 0);
            break;
        default:
            lv_obj_set_size(s_mouth[i], 10, 8);
            lv_obj_set_style_radius(s_mouth[i], 4, 0);
            lv_obj_set_style_bg_color(s_mouth[i], lv_color_hex(MOUTH_COLOR_OPEN), 0);
            break;
        }
        lv_obj_align(s_mouth[i], LV_ALIGN_TOP_MID, 0, 15);
        bsp_display_unlock();
    }

    bool puff  = (lv >= LEVEL_MAX);
    bool shown = !lv_obj_has_flag(s_cheek[i][0], LV_OBJ_FLAG_HIDDEN);
    if (puff != shown) {
        bsp_display_lock(0);
        if (puff) {
            lv_obj_remove_flag(s_cheek[i][0], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_cheek[i][1], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_cheek[i][0], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_cheek[i][1], LV_OBJ_FLAG_HIDDEN);
        }
        bsp_display_unlock();
    }
}

// unit_attach / trigger_shuffle / WIN 收场落回 0 后:整排嘴型/鼓腮全量刷新
// (这几处都会绕开 apply_rotation 直接改 s_level,面部状态需要补一次)
static void faces_refresh_all(void)
{
    for (int i = 0; i < KNOB_COUNT; i++) face_set_mouth(i);
}

// ── 星星微闪(仅夜晚 + AWAKE)────────────────────────────────────────
static void stars_tick(void)
{
    bool changed[3] = { false, false, false };
    for (int i = 0; i < 3; i++) {
        if (--s_star_next[i] <= 0) {
            s_star_bright[i] = !s_star_bright[i];
            s_star_next[i]   = TWINKLE_MIN_F + (int)(esp_random() % (TWINKLE_MAX_F - TWINKLE_MIN_F + 1));
            changed[i] = true;
        }
    }
    if (!changed[0] && !changed[1] && !changed[2]) return;

    bsp_display_lock(0);
    for (int i = 0; i < 3; i++) {
        if (!changed[i]) continue;
        if (s_star_bright[i]) {
            lv_obj_set_size(s_stars[i], 6, 6);
            lv_obj_set_pos(s_stars[i], STAR_POS[i][0], STAR_POS[i][1]);
            lv_obj_set_style_bg_color(s_stars[i], lv_color_hex(STAR_COLOR), 0);
        } else {
            lv_obj_set_size(s_stars[i], 4, 4);
            lv_obj_set_pos(s_stars[i], STAR_POS[i][0] + 1, STAR_POS[i][1] + 1);
            lv_obj_set_style_bg_color(s_stars[i], lv_color_hex(STAR_DIM_COLOR), 0);
        }
    }
    bsp_display_unlock();
}

// ── 小鸟访客 ─────────────────────────────────────────────────────────
static inline int bird_perch_x(int i) { return col_x(i) + (COL_W - 22) / 2; }
static inline int bird_perch_y(int i) { return SCREEN_H - level_h(s_level[i]) - 18; }

static void bird_set_x(void *var, int32_t x) { lv_obj_set_x((lv_obj_t *)var, x); }
static void bird_set_y(void *var, int32_t y) { lv_obj_set_y((lv_obj_t *)var, y); }

// x/y 各一条 lv_anim(独立 exec 回调);完成与否由 game_task 侧的帧倒计时判定,
// 不依赖 anim ready 回调(避免跨任务碰状态)。s_bird_x/y 是影子坐标,随每次
// 移动指令立即更新为目标值,供后续逻辑(如判断出场方向)读取,不查询 LVGL 对象。
static void bird_anim_to(int x1, int y1, int dur_ms, lv_anim_path_cb_t y_path)
{
    int x0 = s_bird_x, y0 = s_bird_y;
    bsp_display_lock(0);
    lv_anim_delete(s_bird, bird_set_x);
    lv_anim_delete(s_bird, bird_set_y);

    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, s_bird);
    lv_anim_set_exec_cb(&ax, bird_set_x);
    lv_anim_set_values(&ax, x0, x1);
    lv_anim_set_duration(&ax, dur_ms);
    lv_anim_set_path_cb(&ax, lv_anim_path_linear);
    lv_anim_start(&ax);

    lv_anim_t ay;
    lv_anim_init(&ay);
    lv_anim_set_var(&ay, s_bird);
    lv_anim_set_exec_cb(&ay, bird_set_y);
    lv_anim_set_values(&ay, y0, y1);
    lv_anim_set_duration(&ay, dur_ms);
    lv_anim_set_path_cb(&ay, y_path);
    lv_anim_start(&ay);
    bsp_display_unlock();

    s_bird_x = x1;
    s_bird_y = y1;
}

static int highest_column(void)   // 并列取最左
{
    int best = 0;
    for (int i = 1; i < KNOB_COUNT; i++) if (s_level[i] > s_level[best]) best = i;
    return best;
}

// 进 NAP/DEEP:瞬时隐藏,不放飞行动画(省电态不许跑动画)
static void bird_force_hide(void)
{
    if (s_bird_state == BIRD_ABSENT) return;
    bsp_display_lock(0);
    lv_anim_delete(s_bird, bird_set_x);
    lv_anim_delete(s_bird, bird_set_y);
    lv_obj_add_flag(s_bird, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    s_bird_state = BIRD_ABSENT;
    s_bird_col   = -1;
    s_bird_pending_ride = false;
}

// enter_win / trigger_shuffle(受惊)/ unit_lost 都经这个统一出口飞走
static void bird_start_fly_out(void)
{
    if (s_bird_state == BIRD_ABSENT || s_bird_state == BIRD_FLY_OUT) return;
    bool exit_left = (s_bird_x < SCREEN_W / 2);
    int tx = exit_left ? -24 : SCREEN_W + 2;
    bird_anim_to(tx, 30, BIRD_FLY_MS, lv_anim_path_ease_in);
    s_bird_state  = BIRD_FLY_OUT;
    s_bird_frame  = 0;
    s_bird_fly_ms = BIRD_FLY_MS;
    s_bird_pending_ride = false;
}

static void bird_enter_perched(void)
{
    s_bird_state = BIRD_PERCHED;
    s_bird_frame = 0;
    s_bird_perch = rand_range_frames(BIRD_PERCH_MIN_S, BIRD_PERCH_MAX_S);
    s_bird_perch_level = s_level[s_bird_col];
}

// 所栖柱档位变化 → 受惊小跳(重贴新柱顶 + 短啾)
static void bird_startle_hop(void)
{
    bird_anim_to(bird_perch_x(s_bird_col), bird_perch_y(s_bird_col), 150, lv_anim_path_overshoot);
    audio_fx_play_notes((audio_note_t[]){ { 1760, 30, 30 } }, 1);
}

// PAT_EQUAL 命中且鸟在场 → 原地开心双跳(不移动 x,只有 y 上下两次)
static void bird_happy_hops(void)
{
    int y = s_bird_y;
    bsp_display_lock(0);
    lv_anim_delete(s_bird, bird_set_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_bird);
    lv_anim_set_exec_cb(&a, bird_set_y);
    lv_anim_set_values(&a, y, y - 8);
    lv_anim_set_duration(&a, 90);
    lv_anim_set_playback_duration(&a, 90);
    lv_anim_set_repeat_count(&a, 2);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    bsp_display_unlock();
}

static void bird_hop_to_next(void)
{
    int next = s_bird_col + s_bird_ride_dir;
    bird_anim_to(bird_perch_x(next), bird_perch_y(next), BIRD_HOP_MS, lv_anim_path_overshoot);
    audio_fx_play_notes((audio_note_t[]){ { level_hz(s_level[next]), BIRD_NOTE_MS, fx_bird_amp() } }, 1);
    s_bird_col   = next;
    s_bird_frame = 0;
}

static void bird_start_ride(int from_col, int to_col)
{
    if (to_col == from_col) { s_bird_col = from_col; bird_enter_perched(); return; }
    s_bird_ride_dir    = (to_col > from_col) ? 1 : -1;
    s_bird_ride_target = to_col;
    s_bird_col   = from_col;
    s_bird_state = BIRD_RIDE;
    bird_hop_to_next();
}

static void bird_start_fly_in(int target_col, bool fast)
{
    bool from_left = (esp_random() & 1) != 0;
    int sx = from_left ? -24 : SCREEN_W + 2;
    s_bird_x = sx; s_bird_y = 40;
    bsp_display_lock(0);
    lv_obj_set_pos(s_bird, sx, 40);
    lv_obj_remove_flag(s_bird, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();

    int dur = fast ? 500 : BIRD_FLY_MS;
    bird_anim_to(bird_perch_x(target_col), bird_perch_y(target_col), dur, lv_anim_path_ease_out);
    audio_fx_play_notes((audio_note_t[]){ { 1568, 40, 35 }, { 2093, 60, 35 } }, 2);

    s_bird_state  = BIRD_FLY_IN;
    s_bird_frame  = 0;
    s_bird_fly_ms = dur;
    s_bird_col    = target_col;
}

// 图案彩蛋命中(UP/DOWN/MOUNTAIN/VALLEY):鸟在场直接从当前栖息柱骑向终点柱;
// 鸟不在场则快速飞入起点柱再骑。RIDE 进行中忽略新命中(音/跳/灯照发,不叠加)。
static void bird_on_pattern(int fly_in_col, int ride_to_col)
{
    if (s_bird_state == BIRD_RIDE) return;
    if (s_bird_state == BIRD_PERCHED) {
        bird_start_ride(s_bird_col, ride_to_col);
    } else if (s_bird_state == BIRD_ABSENT) {
        s_bird_pending_ride    = true;
        s_bird_pending_ride_to = ride_to_col;
        bird_start_fly_in(fly_in_col, true);
    }
    // FLY_IN / FLY_OUT 中:鸟正忙,忽略
}

static void bird_on_equal(void)
{
    if (s_bird_state == BIRD_PERCHED) bird_happy_hops();
}

// 每帧从 game_task 调用;NAP/DEEP 一律瞬时隐藏,AWAKE 时跑状态机。
static void bird_tick(core2_sleep_stage_t stage, bool st_play, bool unit_ok)
{
    if (stage != CORE2_SLEEP_AWAKE) { bird_force_hide(); return; }
    bool allow = st_play && unit_ok;

    switch (s_bird_state) {
    case BIRD_ABSENT:
        if (allow && --s_bird_visit <= 0) bird_start_fly_in(highest_column(), false);
        break;
    case BIRD_FLY_IN:
        if (++s_bird_frame >= s_bird_fly_ms / POLL_PERIOD_MS) {
            if (s_bird_pending_ride) {
                s_bird_pending_ride = false;
                bird_start_ride(s_bird_col, s_bird_pending_ride_to);
            } else {
                bird_enter_perched();
            }
        }
        break;
    case BIRD_PERCHED:
        if (!allow) { bird_start_fly_out(); break; }
        if (s_level[s_bird_col] != s_bird_perch_level) {
            s_bird_perch_level = s_level[s_bird_col];
            bird_startle_hop();
        }
        if (--s_bird_perch <= 0) bird_start_fly_out();
        break;
    case BIRD_RIDE:
        if (++s_bird_frame >= BIRD_HOP_MS / POLL_PERIOD_MS) {
            if (s_bird_col != s_bird_ride_target) bird_hop_to_next();
            else bird_enter_perched();
        }
        break;
    case BIRD_FLY_OUT:
        if (++s_bird_frame >= s_bird_fly_ms / POLL_PERIOD_MS) {
            bsp_display_lock(0);
            lv_obj_add_flag(s_bird, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();
            s_bird_state = BIRD_ABSENT;
            s_bird_col   = -1;
            s_bird_visit = rand_range_frames(BIRD_VISIT_MIN_S, BIRD_VISIT_MAX_S);
        }
        break;
    }
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
    faces_refresh_all();                          // DEEP 恢复重接管也要把嘴型/鼓腮补齐
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
    bird_start_fly_out();
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

// "演奏这一排":按当前 8 柱高度弹一串音(8 音正好一次 play_notes,<400ms)
// reverse=true 时从右往左取音(听感方向与 arp 播放顺序一致)。
static void play_row_arp(uint8_t amp, bool reverse)
{
    audio_note_t seq[KNOB_COUNT];
    for (int i = 0; i < KNOB_COUNT; i++) {
        int col = reverse ? (KNOB_COUNT - 1 - i) : i;
        seq[i].freq_hz = level_hz(s_level[col]);
        seq[i].ms      = ARP_MS;
        seq[i].amp     = amp;
    }
    audio_fx_play_notes(seq, KNOB_COUNT);
}

static int wave_delay(int i, int step_ms, wave_dir_t dir)
{
    int mirror = (i < KNOB_COUNT - 1 - i) ? i : (KNOB_COUNT - 1 - i);   // min(i, 7-i)
    switch (dir) {
        case WAVE_L2R: return i * step_ms;
        case WAVE_R2L: return (KNOB_COUNT - 1 - i) * step_ms;
        case WAVE_IN:  return mirror * step_ms;
        case WAVE_OUT: return (KNOB_COUNT / 2 - 1 - mirror) * step_ms;
        case WAVE_ALL:
        default:       return 0;
    }
}

// 8 柱错峰小跳一下,跳完各自回到本档高度
static void wave_bounce(int lift, int step_ms, wave_dir_t dir)
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
        lv_anim_set_delay(&a, wave_delay(i, step_ms, dir));
        lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
        lv_anim_start(&a);
    }
    bsp_display_unlock();
}

// 五种图案各自的音/跳/灯/震/小鸟叙事(FUN2_SPEC.md §3.2)
static void pattern_reward(pattern_t p)
{
    switch (p) {
    case PAT_UP:
        play_row_arp(fx_arp_amp(), false);
        wave_bounce(12, WAVE_STEP_MS, WAVE_L2R);
        ledstrip_fx_trigger(LED_FX_SWEEP_L2R);
        haptics_play(HAPTIC_COLLECT);
        bird_on_pattern(0, KNOB_COUNT - 1);
        break;
    case PAT_DOWN:
        play_row_arp(fx_arp_amp(), true);
        wave_bounce(12, WAVE_STEP_MS, WAVE_R2L);
        ledstrip_fx_trigger(LED_FX_SWEEP_R2L);
        haptics_play(HAPTIC_COLLECT);
        bird_on_pattern(KNOB_COUNT - 1, 0);
        break;
    case PAT_MOUNTAIN:
        play_row_arp(fx_arp_amp(), false);
        wave_bounce(12, WAVE_STEP_MS, WAVE_IN);
        ledstrip_fx_trigger(LED_FX_GATHER);
        haptics_play(HAPTIC_COLLECT);
        bird_on_pattern(0, KNOB_COUNT - 1);
        break;
    case PAT_VALLEY:
        play_row_arp(fx_arp_amp(), false);
        wave_bounce(12, WAVE_STEP_MS, WAVE_OUT);
        ledstrip_fx_trigger(LED_FX_SPREAD);
        haptics_play(HAPTIC_COLLECT);
        bird_on_pattern(0, KNOB_COUNT - 1);
        break;
    case PAT_EQUAL:
        audio_fx_play_notes((audio_note_t[]){
            { level_hz(s_level[0]), EQUAL_NOTE_MS, EQUAL_NOTE_AMP } }, 1);
        wave_bounce(12, WAVE_STEP_MS, WAVE_ALL);
        ledstrip_fx_trigger(LED_FX_FLASH);
        haptics_play(HAPTIC_BUMP_MED);
        bird_on_equal();
        break;
    default:
        break;
    }
}

// ── 玩法:转 / 按 / 拨 ────────────────────────────────────────────────
static void enter_win(void);

static void apply_rotation(int i, int32_t inc)
{
    // 看向:任一旋钮转动(含顶/底清零转动)都刷新——只在目标变化时批量重对齐一次
    if (s_gaze_target != i) {
        s_gaze_target = i;
        gaze_apply(i);
    }
    s_gaze_frames = GAZE_HOLD_MS / POLL_PERIOD_MS;

    s_acc[i] += ENC_DIR * (int)inc;
    int delta = s_acc[i] / ENC_COUNTS_PER_LEVEL;
    if (delta == 0) return;
    s_acc[i] -= delta * ENC_COUNTS_PER_LEVEL;

    int lv = s_level[i] + delta;
    if (lv < 0) lv = 0;
    if (lv > LEVEL_MAX) lv = LEVEL_MAX;

    // 顶/底继续转:柱子不动也给声音(输入永远有回应,零失败)
    audio_fx_play_notes((audio_note_t[]){ { level_hz(lv), fx_tick_ms(), fx_tick_amp() } }, 1);
    if (lv == s_level[i]) return;

    bool hit_top = (lv == LEVEL_MAX && s_level[i] < LEVEL_MAX);
    s_level[i] = lv;
    face_set_mouth(i);

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
        if (p != PAT_NONE) pattern_reward(p);
    }
}

// 纯动画部分(柱顶弹跳一下);和弦齐按时对每根被按柱各调一次,不叠加音/震/灯
static void sing_anim(int i)
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
}

static void sing(int i)
{
    sing_anim(i);

    audio_fx_play_notes((audio_note_t[]){ { level_hz(s_level[i]), SING_MS, fx_sing_amp() } }, 1);
    haptics_play(HAPTIC_COLLECT);

    unit_8encoder_set_led(i, 255, 255, 255);       // 就地灯闪白
    s_led_flash[i] = 5;                            // ~165ms 后回档位色
}

// 多键齐按(§5.2):被按各柱同时弹跳 + 一次琶音(按档位升序)+ 一次中震 + 各柱就地灯闪白
static void chord_burst(const int *cols, int n)
{
    for (int k = 0; k < n; k++) sing_anim(cols[k]);

    int order[KNOB_COUNT];
    for (int k = 0; k < n; k++) order[k] = cols[k];
    for (int a = 1; a < n; a++) {
        int key = order[a], b = a - 1;
        while (b >= 0 && s_level[order[b]] > s_level[key]) { order[b + 1] = order[b]; b--; }
        order[b + 1] = key;
    }
    audio_note_t seq[KNOB_COUNT];
    for (int k = 0; k < n; k++) {
        seq[k].freq_hz = level_hz(s_level[order[k]]);
        seq[k].ms      = CHORD_NOTE_MS;
        seq[k].amp     = CHORD_NOTE_AMP;
    }
    audio_fx_play_notes(seq, n);

    haptics_play(HAPTIC_BUMP_MED);
    ledstrip_fx_trigger(LED_FX_FLASH);

    for (int k = 0; k < n; k++) {
        unit_8encoder_set_led(cols[k], 255, 255, 255);
        s_led_flash[cols[k]] = 5;
    }
}

// ── 全拉满庆祝 ────────────────────────────────────────────────────────
static void enter_win(void)
{
    bird_start_fly_out();   // 庆祝不等它,立即飞走

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
        for (int i = 0; i < KNOB_COUNT; i++) {
            s_level[i] = 0;
            face_set_mouth(i);                     // 直接改 s_level,绕开 apply_rotation,补一次
        }
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
                if (awake_play) {                  // 两帧收集窗口:攒齐"同时"按下的柱再结算
                    s_chord_pending[i] = true;
                    if (s_chord_wait == 0) s_chord_wait = CHORD_WINDOW_FRAMES;
                }
            }
        }
        if (s_led_flash[i] > 0 && --s_led_flash[i] == 0) knob_led_show_level(i);
    }

    if (s_chord_wait > 0 && --s_chord_wait == 0) {
        int cols[KNOB_COUNT], n = 0;
        for (int i = 0; i < KNOB_COUNT; i++) {
            if (s_chord_pending[i]) { cols[n++] = i; s_chord_pending[i] = false; }
        }
        // 窗口开着时若状态跑离 awake_play(如全满触发 WIN),直接丢弃,不结算
        if (awake_play) {
            if (n == 1) sing(cols[0]);
            else if (n > 1) chord_burst(cols, n);
        }
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
    faces_refresh_all();                 // 直接改 s_level,绕开 apply_rotation,补一次嘴型/鼓腮

    play_row_arp(60, false);             // 哗啦一声滑音(按新队形高度)
    haptics_play(HAPTIC_BUMP_MED);
    ledstrip_fx_trigger(LED_FX_WIN);
    if (s_unit_ok) knob_leds_show_all();
    bird_start_fly_out();                // 被"哗啦"吓走——刻意的笑点

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

        core2_sleep_stage_t prev_stage = s_prev_stage;
        // 深度省电切过 M-Bus 5V → 8Encoder 掉电复位:醒来后重新接管(重写灯/基线)
        if (prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_unit_ok = false;
            unit_attach(false);
        }
        // 打盹/深度省电醒来:小鸟保持 ABSENT,拜访计时重新随机(不续用休眠前的残余倒计时)
        if (prev_stage != CORE2_SLEEP_AWAKE && stage == CORE2_SLEEP_AWAKE) {
            s_bird_visit = rand_range_frames(BIRD_VISIT_MIN_S, BIRD_VISIT_MAX_S);
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
                case ST_PLAY:       face_tick(); break;   // 眨眼/看向仅 ST_PLAY 跑,WIN 中冻结
                case ST_WIN_BOUNCE: win_bounce_tick(); break;
                case ST_WIN_SINK:   win_sink_tick();   break;
            }
            if (s_night) stars_tick();
        }
        bird_tick(stage, s_state == ST_PLAY, s_unit_ok);

        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void knobs_game_start(void)
{
    ui_create();

    // 趣味增量第二批:装饰状态初始化(星星已在 ui_create→scene_apply 里落地)
    s_blink_timer = rand_range_frames(BLINK_MIN_S, BLINK_MAX_S);
    s_gaze_target = -1;
    s_bird_visit  = rand_range_frames(BIRD_VISIT_MIN_S, BIRD_VISIT_MAX_S);

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
