// clock_turn —— 转转钟(教育卡带,ota_3)入口
//
// 本里程碑范围 = SPEC.md M2+M4(2026-08-06 语音整章作废后重排,见 SPEC §13):
//   信息区三子区(状态条①两位模式开关+长按切换、数字钟按需揭晓/题面、反馈脸 idle/yay/huh)
//   + 整齐时刻吸附三通道 + MODE_QUIZ 随机出题闭环(判定/渐进提示弧/庆祝/自动下一题)。
// M0(单元 bring-up)+ M1(静态钟面+两针联动)已完成,本次在其上扩建。
// M5(静止思考宽限/放弃演示/连续使用提醒)、M6(单元容错完整 UI)本批未做,见 SPEC §13。
//
// 硬件:Core2 + Bottom2,Chain Encoder(U207)接 **PORT.C**(蓝口,UART2 G13/G14),
//      经 Chain Bridge;节点吃 PORT.C 5V = M-Bus 5V/EXTEN(core2_board_init 已代开)。

#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_slot.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "imu_mpu6886.h"
#include "power_monitor.h"

#include "chain_bus.h"
#include "unit_chain_encoder.h"

#include "clock_model.h"
#include "clock_ui.h"
#include "feedback.h"
#include "tuning.h"

static const char *TAG = "clock_turn";

// ── Chain Encoder 接管状态 ──────────────────────────────────────────────
static uint8_t s_enc_id;              // 0 = 未绑定;否则链上位置(=UNIT_CHAIN_ENCODER_ID_DEFAULT)
static int16_t s_enc_prev;            // 上一帧原始绝对计数(帧间 delta 靠它,§3 红线:严禁用绝对值)
static bool    s_enc_have_prev;
static int     s_enc_err_streak;
static int     s_rescan_accum_ms;     // 距上次重扫累计的毫秒数(没插节点时 2s 周期重试,SPEC §1)
#define BATT_POLL_MS  10000           // 状态条电量壳刷新周期
static int     s_batt_accum_ms = BATT_POLL_MS;   // 预置满:首帧就刷一次,别让电量壳空顶 10s

// t = 分钟数(0..719)。默认落在 7:30 —— 恰好是 SPEC §13 M1 的验收帧,开机免转钮即可截图自查。
static int s_t = 7 * 60 + 30;

static core2_sleep_t s_sleep;

// ── 旋钮中心键:边沿检测 + 去抖(SPEC §5.4;参考 apps/chain_lab 的 press_edge 套路)────
static bool s_btn_prev;
static int  s_btn_debounce_remain_ms;

// ── 两个模式(SPEC §1.2/§4):状态条①长按 1.5s 切换,回调只在 LVGL 任务里置一个标记,
//    真正的模式切换与副作用(出题/清面板等)留给 game_task 做(见 clock_ui.h 的约定)。──
typedef enum { APP_MODE_FREE = 0, APP_MODE_QUIZ } app_mode_t;
static app_mode_t          s_mode = APP_MODE_FREE;
static volatile bool       s_mode_toggle_req;

// MODE_FREE:按键揭晓当前读数,持续 READOUT_HOLD_MS 后淡回占位点
static bool s_reveal_active;
static int  s_reveal_remain_ms;

// MODE_QUIZ:随机出题 + 判定 + 渐进提示 + 庆祝自动下一题
static int  s_quiz_target = -1;
static int  s_quiz_last_target = -1;   // 传给 clock_model_next_quiz 排除"连续重复出题"
static int  s_quiz_miss;
static bool s_quiz_win_active;
static int  s_quiz_win_remain_ms;

// ── 探测 / 接管(SPEC §1 通用容错形态:没插=无字提示卡+2s 重试;本里程碑
//    "无字提示卡"简化为串口日志 + 钟面照常显示,完整提示卡是 M6 的范围)────────────
static void try_attach(void)
{
    esp_err_t err = unit_chain_encoder_probe(UNIT_CHAIN_ENCODER_ID_DEFAULT);
    if (err == ESP_OK) {
        s_enc_id = UNIT_CHAIN_ENCODER_ID_DEFAULT;
        s_enc_have_prev = false;
        s_enc_err_streak = 0;
        s_btn_prev = false;
        feedback_set_chain_id(s_enc_id);
        clock_ui_set_linked(true);
        // M0 验收项之一"RGB 可控":接管时点一次暖色微光证明写通(与 feedback.c 的
        // RGB_BASE_* 基线同一套暖色);整齐时刻/答对/还没到的配色由 feedback.c 接管。
        chain_bus_set_rgb_brightness(s_enc_id, NODE_RGB_BRIGHTNESS, 40);
        chain_bus_set_rgb(s_enc_id, 0, 60, 46, 28, 40);
        ESP_LOGI(TAG, "Chain Encoder 接管 @链位%u", s_enc_id);
    } else {
        ESP_LOGW(TAG, "PORT.C 上没认到 Chain Encoder(%s,%dms 后重试)。排查:"
                      "①插蓝口 PORT.C(不是红A/黑B) ②Chain Bridge 箭头朝主控(IN朝Core2) "
                      "③节点 5V(底座灯带亮=5V 有电)",
                 esp_err_to_name(err), (int)ATTACH_RETRY_MS);
    }
}

static void mark_lost(void)
{
    s_enc_id = 0;
    feedback_set_chain_id(0);
    clock_ui_set_linked(false);
}

// 读一次编码器,把"帧间 delta"换算成分钟步进,推进 clock_model 的 t。
// @return true = t 发生了变化(供调用方决定要不要 core2_sleep_kick / 刷屏 / 发落格反馈)。
static bool poll_encoder(void)
{
    if (!s_enc_id) {
        return false;
    }
    int16_t v;
    if (unit_chain_encoder_read_value(s_enc_id, &v) != ESP_OK) {
        if (++s_enc_err_streak >= ERR_STREAK_LOST) {
            ESP_LOGW(TAG, "Chain Encoder 失联(拔线/断电?),回退到无字提示态");
            mark_lost();
        }
        return false;
    }
    s_enc_err_streak = 0;

    if (!s_enc_have_prev) {
        // 刚接管 / 刚从深度省电醒来重新 probe:只记基准,不产生跳变(§11 深度省电坑:
        // 节点断电复位后绝对计数归零,若直接拿本帧减"旧世界"的 prev 会算出一个假 delta)。
        s_enc_prev = v;
        s_enc_have_prev = true;
        return false;
    }

    int delta = (int)v - (int)s_enc_prev;
    s_enc_prev = v;
    if (delta == 0) {
        return false;
    }

#if ENC_INVERT
    delta = -delta;
#endif

    int old_t = s_t;
    s_t = clock_model_step(s_t, delta, MIN_PER_STEP);
    ESP_LOGD(TAG, "encoder raw=%d delta=%+d  t: %d(%d:%02d) -> %d(%d:%02d)",
             v, delta, old_t, old_t / 60, old_t % 60, s_t, s_t / 60, s_t % 60);
    return true;
}

// 转一格的落格反馈:按 t%15==0 分三档(整点/半点/一刻)+ 普通格(SPEC §5.2/§7)。
// ⚠️ 2026-08-10 MIN_PER_STEP=15 后 t 恒是 15 的倍数 → FEEDBACK_TICK_PLAIN 这一档**不再可达**,
//    每一格都落在整齐时刻上,区分退化成整点/半点/一刻三级。分支照留(步长调细即恢复)。
static void emit_step_feedback(void)
{
    feedback_tick_kind_t kind = FEEDBACK_TICK_PLAIN;
    if (clock_model_is_quarter(s_t)) {
        if (s_t % 60 == 0)       kind = FEEDBACK_TICK_HOUR;
        else if (s_t % 60 == 30) kind = FEEDBACK_TICK_HALF;
        else                     kind = FEEDBACK_TICK_QUARTER;
    }
    feedback_emit_step(kind);
}

// ── MODE_QUIZ:随机出题(SPEC §5.5,用户拍板豁免手工编排铁律)────────────────
static void start_new_quiz_question(void)
{
    uint32_t r = esp_random();
    s_quiz_target = clock_model_next_quiz(s_t, s_quiz_last_target, QUIZ_GRAIN_MIN, r);
    s_quiz_last_target = s_quiz_target;
    s_quiz_miss = 0;
    s_quiz_win_active = false;

    clock_ui_set_panel(true, s_quiz_target, true, false);
    clock_ui_set_face(CLOCK_UI_FACE_IDLE);
    clock_ui_set_hint(s_t, s_quiz_target, 0);   // 新题:提示弧隐藏,按错次数清零
    ESP_LOGI(TAG, "MODE_QUIZ 出题:目标 %d:%02d", s_quiz_target / 60, s_quiz_target % 60);
}

// ── 模式切换(状态条①长按 1.5s 触发,SPEC §5.3.5)────────────────────────
static void apply_mode_switch(void)
{
    // 🔴 两个模式各自的"倒计时中"状态是模式内私有的,切模式时必须清零——否则遗留的
    // 计时器会在切走之后的某一帧到期,跑到另一个模式里执行它的收尾逻辑(FREE 揭晓倒计时
    // 跑去清 QUIZ 的面板 / QUIZ 庆祝倒计时跑去在 FREE 态里出题),把画面拨乱。
    // 例:长按切模式发生在"MODE_FREE 刚按键揭晓、4s 计时还没到"或"MODE_QUIZ 刚答对、
    // 2s 庆祝还没完"这两个窗口内,不清零就会在切走后的下一帧复现上一个模式的收尾动作。
    s_reveal_active = false;
    s_quiz_win_active = false;

    s_mode = (s_mode == APP_MODE_FREE) ? APP_MODE_QUIZ : APP_MODE_FREE;
    core2_sleep_kick(&s_sleep);
    feedback_emit_mode_switch();
    clock_ui_set_mode(s_mode == APP_MODE_QUIZ);

    if (s_mode == APP_MODE_QUIZ) {
        start_new_quiz_question();
    } else {
        clock_ui_set_panel(false, 0, false, false);
        clock_ui_set_face(CLOCK_UI_FACE_IDLE);
        clock_ui_set_hint(s_t, s_t, 0);
    }
    ESP_LOGI(TAG, "模式切换 -> %s", s_mode == APP_MODE_QUIZ ? "MODE_QUIZ" : "MODE_FREE");
}

// LVGL 任务上下文触发(见 clock_ui.h 文档注释):只置标记,真正的状态切换 + 出题这些
// "较重"的工作留给 game_task 做,不在 LVGL 事件回调里做。
static void on_mode_toggle_from_ui(void *user)
{
    (void)user;
    s_mode_toggle_req = true;
}

// ── 旋钮中心键语义按当前模式路由(SPEC §5.4)────────────────────────────────
static void handle_button_press(void)
{
    core2_sleep_kick(&s_sleep);   // 桌面玩法坑(CLAUDE.md §10):按键也要 kick

    if (s_mode == APP_MODE_FREE) {
        s_reveal_active = true;
        s_reveal_remain_ms = READOUT_HOLD_MS;
        clock_ui_set_panel(true, s_t, false, false);
        clock_ui_set_face(CLOCK_UI_FACE_IDLE);
        feedback_emit_reveal();
        return;
    }

    // MODE_QUIZ
    if (s_quiz_win_active) {
        return;   // 庆祝期间锁输入,防连点误触发/打断下一题的出题时序
    }
    if (clock_model_quiz_hit(s_t, s_quiz_target)) {
        s_quiz_win_active = true;
        s_quiz_win_remain_ms = WIN_HOLD_MS;
        clock_ui_set_panel(true, s_quiz_target, true, true);   // 数字变绿
        clock_ui_set_face(CLOCK_UI_FACE_YAY);
        clock_ui_set_hint(s_t, s_quiz_target, 0);               // 答对:提示弧隐藏
        clock_ui_play_win();
        feedback_emit_win();
        ESP_LOGI(TAG, "MODE_QUIZ 答对!");
    } else {
        s_quiz_miss++;
        clock_ui_set_face(CLOCK_UI_FACE_HUH);
        int width = (s_quiz_miss <= 1) ? HINT_ARC_W1 : HINT_ARC_W2;
        clock_ui_set_hint(s_t, s_quiz_target, width);
        feedback_emit_miss();
        ESP_LOGI(TAG, "MODE_QUIZ 还没到(第 %d 次)", s_quiz_miss);
    }
}

static void poll_button(void)
{
    if (!s_enc_id) {
        return;
    }
    bool btn = s_btn_prev;   // 读失败时保留旧值(同 chain_lab 的"尽力读"套路)
    unit_chain_encoder_read_button(s_enc_id, &btn);

    bool edge = btn && !s_btn_prev;
    s_btn_prev = btn;

    if (edge && s_btn_debounce_remain_ms <= 0) {
        s_btn_debounce_remain_ms = BTN_DEBOUNCE_MS;
        handle_button_press();
    }
}

static void game_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have_acc = (imu_mpu6886_read_accel(&acc) == ESP_OK);
        int delay_ms = core2_sleep_feed(&s_sleep,
                            have_acc ? (float[]){ acc.x, acc.y, acc.z } : NULL, true);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);

        // 状态条电量壳:只在清醒时刷(屏黑着改 LVGL 对象照样触发重绘 + SPI flush)
        s_batt_accum_ms += delay_ms;
        if (s_batt_accum_ms >= BATT_POLL_MS) {
            s_batt_accum_ms = 0;
            power_status_t ps;
            if (stage == CORE2_SLEEP_AWAKE &&
                power_monitor_read(&ps) == ESP_OK && ps.bat_mv > 0) {
                clock_ui_set_battery(ps.pct, ps.usb);
            }
        }

        if (stage != CORE2_SLEEP_DEEP) {
            if (!s_enc_id) {
                s_rescan_accum_ms += delay_ms;
                if (s_rescan_accum_ms >= ATTACH_RETRY_MS) {
                    s_rescan_accum_ms = 0;
                    try_attach();
                }
            }

            // 状态条①长按满 1.5s 的模式切换请求(在 LVGL 任务里置位,这里消费)
            if (s_mode_toggle_req) {
                s_mode_toggle_req = false;
                apply_mode_switch();
            }

            bool moved = poll_encoder();
            if (moved) {
                // 🔴 桌面玩法坑(CLAUDE.md §10/SPEC §11):转旋钮要 kick,否则机身不动
                // 会被误判"没人玩"、玩着玩着就打盹。
                core2_sleep_kick(&s_sleep);
                if (stage != CORE2_SLEEP_AWAKE) {
                    core2_sleep_wake(&s_sleep);
                }
                emit_step_feedback();
                // MODE_QUIZ 提示弧已浮现时跟手持续重算(目标没变,当前 t 变了,SPEC §5.6)
                if (s_mode == APP_MODE_QUIZ && s_quiz_miss > 0 && !s_quiz_win_active) {
                    int width = (s_quiz_miss <= 1) ? HINT_ARC_W1 : HINT_ARC_W2;
                    clock_ui_set_hint(s_t, s_quiz_target, width);
                }
            }
            poll_button();

            // clock_ui_set_time 内部按 t 是否变化做 no-op 判断,这里无条件调用即可
            // (NAP 态下 t 通常没变 → 零 LVGL 调用,天然满足"打盹应暂停画面更新")。
            clock_ui_set_time(s_t);

            // MODE_FREE 揭晓倒计时:到点淡回占位点(SPEC §5.4)
            if (s_reveal_active) {
                s_reveal_remain_ms -= delay_ms;
                if (s_reveal_remain_ms <= 0) {
                    s_reveal_active = false;
                    clock_ui_set_panel(false, 0, false, false);
                }
            }

            // MODE_QUIZ 庆祝倒计时:到点自动出下一题(SPEC §4)
            if (s_quiz_win_active) {
                s_quiz_win_remain_ms -= delay_ms;
                if (s_quiz_win_remain_ms <= 0) {
                    start_new_quiz_question();
                }
            }

            if (s_btn_debounce_remain_ms > 0) {
                s_btn_debounce_remain_ms -= delay_ms;
            }
        }

        // 🔴 帧节拍走 core2_sleep_pace,不许裸 vTaskDelayUntil(逾期会攒时间欠债 →
        // 之后连跑几百帧还债 = 全场对象突然加速,因果见 core2_sleep.h 文件头)
        core2_sleep_pace(&last, delay_ms);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== clock_turn 转转钟 启动(M2+M4:两模式+信息区三子区+随机出题闭环)===");

    // ⓪ 第一行:把启动分区设回 factory,此后任何复位/崩溃都回 launcher(CLAUDE.md §9)
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V/EXTEN → PORT.C Chain 节点才有电;
    //    touch_btns 随 core2_board_init 自动起,BtnA 长按=回launcher 全局白拿,本阶段无需覆盖)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 反馈编排器(SPEC §7;需在 audio_fx/haptics/ledstrip_fx 之后,core2_board_init 已代管)
    ESP_ERROR_CHECK(feedback_init());

    // ③ 钟面 UI:静态层画一次(圆盘+边框+60刻度+12数字+信息区三底卡),两针落在默认 t(7:30);
    //    默认 MODE_FREE、占位点、脸 idle、连接点先设绿(还没探测,试探到再校正)。
    clock_ui_create();
    clock_ui_set_time(s_t);
    clock_ui_set_mode_toggle_cb(on_mode_toggle_from_ui, NULL);

    // ④ 省电托管(CLAUDE.md §7)。SPEC §11:本卡带"静止思考期"宽限本批未接入(见 M5),
    //    用默认参数即可,MODE_QUIZ 长时间不动手会照常打盹——已知取舍,非缺陷。
    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    core2_sleep_init(&s_sleep, &scfg);

    // ⑤ Chain Encoder 传输层 + 首次探测(没插也继续跑:钟面照常显示,串口周期重试)
    esp_err_t bus_err = chain_bus_init_port_c();
    if (bus_err != ESP_OK) {
        ESP_LOGE(TAG, "chain_bus 初始化失败:%s(PORT.C 上的编码器将不可用)",
                 esp_err_to_name(bus_err));
        clock_ui_set_linked(false);
    } else {
        try_attach();
        if (!s_enc_id) {
            clock_ui_set_linked(false);
        }
    }

    xTaskCreate(game_task, "clock_turn", 4096, NULL, 5, NULL);
}
