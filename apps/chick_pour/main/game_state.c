#include "game_state.h"
#include "tuning.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "imu_mpu6886.h"
#include "core2_sleep.h"
#include "flock.h"
#include "critters.h"
#include "scene.h"
#include "feedback.h"

static const char *TAG = "game";

// 完整状态机(SPEC §4):ATTRACT(睡)→ PLAY → PARTY → 重散回 PLAY。
// SCATTER 不设独立状态 —— 重散是 PARTY 倒计时结束时的一次性动作,做完直接回 PLAY。
typedef enum { ST_ATTRACT, ST_PLAY, ST_PARTY } state_t;

static animal_t      s_animals[ANIMAL_COUNT];
static core2_sleep_t s_sleep;

static state_t    s_state;
static int        s_home_count[2];                 // [kind] 各家已归数(探头小脸用)
static int        s_total;                         // 已归家总数(五声音阶第几音)
static int        s_party_frames;                  // PARTY 剩余帧数
static TickType_t s_last_bounce[ANIMAL_COUNT];     // 每只的弹出反馈节流时戳(§5.2)

// 摇一摇彩蛋(SPEC §3:busy_knobs 泄漏计数法原样搬,SHAKE_* 三常量见 tuning.h)
static float s_prev_acc[3];
static bool  s_prev_acc_valid;
static int   s_shake_hits;      // 带泄漏地攒够 SHAKE_NEEDED 下才算"摇一摇"
static int   s_shake_cooldown;  // 触发后冷却帧数,防一次摇晃连发

// ── ATTRACT 每帧:睡着慢呼吸,倾斜超阈全体醒来(SPEC §4)─────────────────
static void attract_tick(const imu_accel_t *acc)
{
    // 倾斜幅度只看水平两轴合成(与屏轴映射无关):平放噪声 ~0.02g,拿起倾斜轻松过 0.22
    float tilt = sqrtf(acc->x * acc->x + acc->y * acc->y);
    if (tilt > ATTRACT_TILT_THRESH) {
        critters_set_asleep(false);
        critters_hop_all();
        feedback_emit_hello();
        s_state = ST_PLAY;
        ESP_LOGI(TAG, "醒来 → PLAY");
        return;
    }
    critters_idle_tick();   // 慢呼吸(内部自带 §6.5 低频分频)
}

// ── PLAY 每帧 ────────────────────────────────────────────────────────
static void play_tick(const imu_accel_t *acc)
{
    flock_step(s_animals, ANIMAL_COUNT, acc, PHYS_DT);

    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < ANIMAL_COUNT; i++) {
        animal_t *a = &s_animals[i];

        // 撞栅栏/灌木/家外墙且够快(SPEC §6 首行;§9 BUMP_MIN_SPEED)
        if (a->bumped && a->bump_speed >= BUMP_MIN_SPEED) {
            feedback_emit_bump(i);
        }

        switch ((gate_event_t)a->gate_event) {
        case GATE_EV_CAPTURE: {
            // 进度即时累加,不等动画(§5.3 影子变量纪律);动画只演视觉。
            int kind = (int)a->kind;
            s_home_count[kind]++;
            s_total++;
            critters_capture(i, a);
            scene_set_home_count(kind, s_home_count[kind]);
            feedback_emit_collect(kind, s_total);
            ESP_LOGI(TAG, "归家 %d/%d(%s)", s_total, ANIMAL_COUNT,
                     kind == ANIMAL_CHICK ? "小鸡→鸡窝" : "小鸭→池塘");

            if (s_total >= ANIMAL_COUNT) {
                s_state = ST_PARTY;
                s_party_frames = PARTY_HOLD_MS / PHYS_PERIOD_MS;
                feedback_emit_party();
                ESP_LOGI(TAG, "全部归家 → 派对!");
            }
            break;
        }
        case GATE_EV_BOUNCE:
            // 物理弹出每次照做(flock 已做),反馈按每只节流(§5.2)
            if (now - s_last_bounce[i] >= pdMS_TO_TICKS(BOUNCE_SND_COOLDOWN_MS)) {
                s_last_bounce[i] = now;
                feedback_emit_bounce(i);
            }
            break;
        default:
            break;
        }
    }

    critters_update(s_animals, ANIMAL_COUNT);
}

// ── PARTY 每帧:纯倒计时(视觉/音/震/灯已交给 feedback + lv_anim 异步演)──
static void party_tick(void)
{
    if (--s_party_frames > 0) return;

    // 重散一批(§5.4 约束随机 + 网格兜底)→ 精灵复位 → 计数/小脸清零 → 回 PLAY
    flock_scatter(s_animals, ANIMAL_COUNT);
    critters_respawn(s_animals, ANIMAL_COUNT);
    s_home_count[0] = s_home_count[1] = 0;
    s_total = 0;
    scene_set_home_count(0, 0);
    scene_set_home_count(1, 0);
    s_state = ST_PLAY;
    ESP_LOGI(TAG, "重散一批,新一轮开始");
}

// ── 摇一摇彩蛋(busy_knobs 泄漏计数法,SPEC §3;仅 PLAY 且清醒时触发)────
static void shake_check(const imu_accel_t *acc, bool have, core2_sleep_stage_t stage)
{
    if (s_shake_cooldown > 0) s_shake_cooldown--;
    if (have && s_prev_acc_valid) {
        float d = fabsf(acc->x - s_prev_acc[0]) + fabsf(acc->y - s_prev_acc[1])
                + fabsf(acc->z - s_prev_acc[2]);
        if (d > SHAKE_THRESH) {
            if (s_shake_hits < SHAKE_NEEDED) s_shake_hits++;
            if (s_shake_hits >= SHAKE_NEEDED && s_shake_cooldown == 0 &&
                stage == CORE2_SLEEP_AWAKE && s_state == ST_PLAY) {
                critters_hop_all();      // 在场动物全体原地小跳
                feedback_emit_shake();   // 叽嘎合唱 + 中震 + 彩虹一闪;不改进度
                s_shake_cooldown = SHAKE_COOLDOWN_MS / PHYS_PERIOD_MS;
                s_shake_hits     = 0;
                ESP_LOGI(TAG, "摇一摇彩蛋!");
            }
        } else if (s_shake_hits > 0) {
            s_shake_hits--;   // 泄漏:要连着晃几下,单次磕碰会被漏掉
        }
    }
    if (have) {
        s_prev_acc[0] = acc->x; s_prev_acc[1] = acc->y; s_prev_acc[2] = acc->z;
        s_prev_acc_valid = true;
    }
}

// ── 主任务 ───────────────────────────────────────────────────────────
static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);

        // 纯 IMU 玩法(SPEC §10):喂加速度即可,无需 core2_sleep_kick。
        // 打盹在 ATTRACT(睡着摆样)与 PLAY 允许;PARTY 是庆祝态,按 core2_sleep 约定
        // 给 false。休眠中进度保留(§4:唤醒回当前进度,归家的不放出来)。
        bool nap_ok = have && (s_state != ST_PARTY);
        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ acc.x, acc.y, acc.z } : NULL,
                                        nap_ok);
        core2_sleep_stage_t stage = core2_sleep_stage(&s_sleep);

        shake_check(&acc, have, stage);

        if (stage == CORE2_SLEEP_AWAKE) {
            switch (s_state) {
                case ST_ATTRACT: if (have) attract_tick(&acc); break;
                case ST_PLAY:    if (have) play_tick(&acc);    break;
                case ST_PARTY:   party_tick();                 break;
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}

void game_state_start(void)
{
    flock_init(s_animals, ANIMAL_COUNT);
    critters_init(s_animals, ANIMAL_COUNT);

    s_state = ST_ATTRACT;
    critters_set_asleep(true);   // 开机动物们睡着(Zzz + 慢呼吸),倾斜即醒(SPEC §4)
    s_home_count[0] = s_home_count[1] = 0;
    s_total = 0;
    for (int i = 0; i < ANIMAL_COUNT; i++) s_last_bounce[i] = 0;
    s_prev_acc_valid = false;
    s_shake_hits     = 0;
    s_shake_cooldown = 0;

    core2_sleep_init(&s_sleep, NULL);   // NULL = 实机定案默认值(SPEC §10:全托管,不自定义)

    xTaskCreate(game_task, "game", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ATTRACT:%d 只动物睡着入场,倾斜唤醒(完整状态机:睡醒/归家/派对/重散/彩蛋)",
             ANIMAL_COUNT);
}
