// magic_wand —— 主任务:轮询手势、派发法术(spellbook 数据表)、省电挂载、单元容错
//
// 状态机(SPEC.md §3 的简化落地——IDLE_READY/CASTING/SHIMMER 在实现里合并成一个
// ST_READY,区分靠局部计时器而非独立顶层状态,观感与 SPEC 一致):
//   ST_NO_UNIT ──(Gesture 单元 init 成功)──► ST_READY
//   ST_READY   ──(手势分类成功,§5 表)──► 派发全部反馈通道,立即回 ST_READY(可打断)
//              ──(SHIMMER_IDLE_MS 内无手势)──► 退化低频 ping(§4 点 2,无独立信号)
//              ──(法术书 9 页集满)──► ST_PARTY
//   ST_PARTY   ──(9 步回放完)──► spellbook 清零 → ST_READY
#include "magic_wand.h"

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
#include "unit_gesture.h"

#include "spellbook.h"
#include "wand_fx.h"
#include "wizard.h"

#include "tuning.h"

static const char *TAG = "magic_wand";

#define HINT_CARD       0xFFFFFF
#define PARTY_INTRO_MS  1200   // 派对开场欢跳(wizard_party_begin 的 3 跳 ≈ 1200ms)

typedef enum { ST_NO_UNIT = 0, ST_READY, ST_PARTY } wand_state_t;

static wand_state_t s_state;

static bool s_unit_ok;
static int  s_retry_frames;
static int  s_retry_count;
static int  s_err_streak;
static int  s_poll_accum_ms;

static uint32_t        s_now_ms;          // 本局累计运行时长(帧 delay_ms 累加,非真实墙钟)
static gesture_event_t s_last_gesture = GESTURE_NONE;
static uint32_t        s_last_gesture_ms;
static uint32_t        s_last_activity_ms;   // 距上次"有动静"多久(驱动退化 SHIMMER ping)

static bool     s_pending_reveal;         // 躲猫咒:视觉先播(内部自带停顿),音/震/灯/魔法棒延后到揭晓
static uint32_t s_pending_reveal_at_ms;

static int s_party_phase;                 // 0=开场倒计时;1..SPELLBOOK_SIZE=已放到第几步;之后收场
static int s_party_accum_ms;

static core2_sleep_t       s_sleep;
static core2_sleep_stage_t s_prev_stage = CORE2_SLEEP_AWAKE;

static lv_obj_t *s_plug_hint;

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

// ── UI:场景 + 法术书 + 无字提示卡(仅一次,进场画;之后只改状态)────────────
static void make_plug_hint(lv_obj_t *scr)
{
    // 无字提示卡:手势单元(方块+镜头圆)+ 引线 + 插头。给家长看"去插 Gesture"
    lv_obj_t *card = plain(scr, 132, 76, HINT_CARD, 14);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 30);
    lv_obj_t *sensor = plain(card, 46, 40, 0x8060C0, 8);
    lv_obj_align(sensor, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_t *lens = plain(sensor, 18, 18, 0xE8E0FF, LV_RADIUS_CIRCLE);
    lv_obj_align(lens, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *wire = plain(card, 30, 4, 0x3A3A38, 2);
    lv_obj_align(wire, LV_ALIGN_LEFT_MID, 62, 0);
    lv_obj_t *plug = plain(card, 16, 22, 0x3A3A38, 4);
    lv_obj_align(plug, LV_ALIGN_LEFT_MID, 96, 0);
    s_plug_hint = card;
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
}

static void plug_hint_show(bool show)
{
    bsp_display_lock(0);
    if (show) lv_obj_remove_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s_plug_hint, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

static void ui_create(void)
{
    lv_obj_t *scr;
    bsp_display_lock(0);
    scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    bsp_display_unlock();

    wizard_scene_create(scr);   // 自带锁
    spellbook_create(scr);      // 自带锁

    bsp_display_lock(0);
    make_plug_hint(scr);
    bsp_display_unlock();
}

// ── 单元接管 / 缺席(仿 feed_monster/busy_knobs 惯例)──────────────────────
static bool unit_attach(bool greet)
{
    if (unit_gesture_init(core2_board_port_a(), 0) != ESP_OK) return false;
    s_err_streak = 0;
    s_unit_ok    = true;
    s_state      = ST_READY;
    plug_hint_show(false);
    if (greet) {
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
    }
    ESP_LOGI(TAG, "Gesture 已接管");
    return true;
}

static void unit_lost(void)
{
    s_unit_ok      = false;
    s_retry_frames = 0;
    s_state        = ST_NO_UNIT;
    plug_hint_show(true);
    audio_fx_play(SND_BUMP_MED);
    ESP_LOGW(TAG, "Gesture 失联(拔线/断电?),转入重试探测");
}

// ── 退化 SHIMMER:固定低频存活性 ping(无"在场未分类"信号,见 unit_gesture.h)──
static void maybe_shimmer(void)
{
    if (s_now_ms - s_last_activity_ms < SHIMMER_IDLE_MS) return;
    s_last_activity_ms = s_now_ms;
    wizard_shimmer();
    wand_fx_trigger(WAND_FX_SHIMMER);
    audio_fx_play_notes((audio_note_t[]){ { 1400, 20, 25 } }, 1);
    // 注:不调 core2_sleep_kick——这是"魔法一直在待命"的装饰性 ping,不代表真的有人在
    // 玩,踢一下会让打盹永不发生(违反 §7 打盹判据只看机身动作的精神)。
}

// ── 一次手势分类成功 ─────────────────────────────────────────────────────
static void fan_out(const spell_def_t *def)
{
    audio_fx_play_notes(def->notes, def->note_count);
    haptics_play(def->haptic);
    ledstrip_fx_trigger(def->led_fx);
    wand_fx_trigger(def->wand_fx);
}

static void handle_gesture(gesture_event_t g)
{
    s_last_activity_ms = s_now_ms;

    bool recast = (g == s_last_gesture) && (s_now_ms - s_last_gesture_ms < RECAST_COOLDOWN_MS);
    s_last_gesture    = g;
    s_last_gesture_ms = s_now_ms;

    const spell_def_t *def = spellbook_spell_def(g);
    if (!def) return;

    if (recast) {
        // 冷却窗口内的重复触发:轻量重复,不进书页/连击判定(SPEC.md §4 点 3)。
        wizard_cast_light(g);
        fan_out(def);
        return;
    }

    wizard_cast(g);
    if (g == GESTURE_BACKWARD) {
        // 躲猫咒:视觉自带 PEEK_HOLD_MS 停顿+回弹,音/震/灯/魔法棒延后到"揭晓"瞬间
        // 一起打出(呼应 peekaboo 揭晓手感),由 pending_reveal_tick() 接手。
        s_pending_reveal    = true;
        s_pending_reveal_at_ms = s_now_ms + PEEK_HOLD_MS;
    } else {
        fan_out(def);
    }

    // 连击彩蛋(P3)判定在手势分类成功之后、法术书首遇点亮之前(SPEC.md §7 点 2)。
    spellbook_combo_feed(g, s_now_ms);
    if (spellbook_combo_check(s_now_ms)) {
        wizard_hidden_spell();
        audio_fx_play(SND_WIN);
        haptics_play(HAPTIC_WIN);
        ledstrip_fx_trigger(LED_FX_WIN);
    }

    bool first_unlock = spellbook_unlock(g);
    if (first_unlock) {
        audio_fx_play(SND_COLLECT);
        haptics_play(HAPTIC_COLLECT);
    }
    if (spellbook_is_complete()) {
        s_state = ST_PARTY;
        s_party_phase = 0;
        s_party_accum_ms = 0;
        wizard_party_begin();
    }
}

// ── 手势轮询(仿 feed_monster poll_sonic 的接管/重试骨架)──────────────────
static void poll_gesture(core2_sleep_stage_t stage)
{
    if (!s_unit_ok) {
        if (++s_retry_frames >= ATTACH_RETRY_MS / GESTURE_POLL_MS) {
            s_retry_frames = 0;
            if (unit_attach(true)) {
                s_retry_count = 0;
            } else if (++s_retry_count % 15 == 0) {
                bool found = core2_board_port_a_scan();
                if (found || core2_board_port_a_stuck()) core2_board_port_a_recover();
            }
        }
        return;
    }
    if (s_state == ST_PARTY) return;   // 派对期间手势输入被忽略(SPEC.md §3)

    gesture_event_t g;
    esp_err_t r = unit_gesture_read(&g);
    if (r != ESP_OK) {
        if (++s_err_streak >= ERR_STREAK_LOST) unit_lost();
        return;
    }
    s_err_streak = 0;

    if (g == GESTURE_NONE) {
        if (stage == CORE2_SLEEP_AWAKE) maybe_shimmer();
        return;
    }

    // 有手势信号(不论休眠态是否要正式判定):都算"有人在玩",顶住/唤醒。
    core2_sleep_kick(&s_sleep);
    if (stage != CORE2_SLEEP_AWAKE) {
        core2_sleep_wake(&s_sleep);
        return;   // 只当唤醒信号,不判定法术(仿 busy_knobs 小鸟先例)
    }

    handle_gesture(g);
}

// ── 躲猫咒延迟揭晓 ───────────────────────────────────────────────────────
static void pending_reveal_tick(void)
{
    if (!s_pending_reveal) return;
    if ((int32_t)(s_now_ms - s_pending_reveal_at_ms) < 0) return;
    s_pending_reveal = false;
    fan_out(spellbook_spell_def(GESTURE_BACKWARD));
}

// ── 法术大派对:9 步接力回放(压缩版,略去停顿类细节)─────────────────────
static void fire_party_step(int step_idx)
{
    gesture_event_t g = SPELL_ORDER[step_idx];
    wizard_party_step(g);
    fan_out(spellbook_spell_def(g));
}

static void party_tick(int delay_ms)
{
    s_party_accum_ms += delay_ms;
    if (s_party_phase == 0) {
        if (s_party_accum_ms < PARTY_INTRO_MS) return;
        s_party_accum_ms = 0;
        s_party_phase = 1;
        fire_party_step(0);
        return;
    }
    if (s_party_accum_ms < PARTY_STEP_MS) return;
    s_party_accum_ms = 0;

    if (s_party_phase < SPELLBOOK_SIZE) {
        fire_party_step(s_party_phase);
        s_party_phase++;
        return;
    }

    // 9 步放完:收场
    wizard_party_end();
    spellbook_reset();
    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
    s_state = ST_READY;
    s_last_gesture = GESTURE_NONE;
    s_last_activity_ms = s_now_ms;
    ESP_LOGI(TAG, "法术大派对结束,法术书清零开新一轮");
}

// ── 主任务(帧周期随 core2_sleep 建议值走,清醒态 ~60Hz)────────────────────
static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);

        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ acc.x, acc.y, acc.z } : NULL,
                                        s_state == ST_READY && have);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);
        s_now_ms += (uint32_t)delay_ms;

        // 深度省电切过 M-Bus 5V → Gesture/RGB 单元掉电复位:醒来后重新接管
        if (s_prev_stage == CORE2_SLEEP_DEEP && stage != CORE2_SLEEP_DEEP) {
            s_unit_ok = false;
            unit_attach(false);
            wand_fx_start();
        }
        // 唤醒(NAP/DEEP → AWAKE):不续用休眠前的冷却计时(仿 busy_knobs 小鸟先例)
        if (s_prev_stage != CORE2_SLEEP_AWAKE && stage == CORE2_SLEEP_AWAKE) {
            s_last_gesture      = GESTURE_NONE;
            s_last_activity_ms  = s_now_ms;
            s_pending_reveal    = false;
        }
        s_prev_stage = stage;

        if (stage != CORE2_SLEEP_DEEP) {
            s_poll_accum_ms += delay_ms;
            if (s_poll_accum_ms >= GESTURE_POLL_MS) {
                s_poll_accum_ms = 0;
                poll_gesture(stage);
            }
        }

        // CASTING/SHIMMER/法术书装饰动画只在 AWAKE 跑(SPEC.md §7 点 4)。
        if (stage == CORE2_SLEEP_AWAKE) {
            pending_reveal_tick();
            if (s_state == ST_PARTY) party_tick(delay_ms);
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void magic_wand_start(void)
{
    ui_create();

    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    core2_sleep_init(&s_sleep, &scfg);

    ledstrip_fx_set_base(LED_BASE_AMBIENT);
    wand_fx_start();   // P4:RGB 单元缺席不阻塞启动(SPEC.md §13)

    bool attached = unit_attach(false);
    if (!attached) {
        ESP_LOGW(TAG, "Gesture 未就位:必须插 Core2 机身侧面的红色 PORT.A 口"
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
        s_state = ST_NO_UNIT;
        plug_hint_show(true);
        audio_fx_play(SND_BUMP_MED);
    }

    s_last_activity_ms = 0;

    // 栈:施法状态/法术书/魔法棒特效变多,直接从 SPEC.md §11 建议的上限 5120 起
    // (而不是其它 app 常用的 4096),省一轮"栈告警再调大"的实机往返。
    xTaskCreate(game_task, "magicwand", 5120, NULL, 5, NULL);
}
