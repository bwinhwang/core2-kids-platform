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

// 当前生效图纸下标(SPEC §13:后院图纸批)。开机 = 图纸 A,每轮派对后依次 +1。
static int s_bp_idx;

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

// ── 下一张图纸:依次轮换(2026-08-10 用户拍板"最简单的就是依次选中")────────
// 3 张图纸不值得上洗牌袋(那是 tilt_maze 16 关的做法):轮换代码更短、行为可预期、
// 保证每轮都换,也不用打"不背靠背重复"的补丁。
static int next_blueprint(void)
{
    int n = scene_blueprint_count();
    if (n < 1) n = 1;
    return (s_bp_idx + 1) % n;
}

// ── PARTY 每帧:纯倒计时(视觉/音/震/灯已交给 feedback + lv_anim 异步演)──
static void party_tick(void)
{
    if (--s_party_frames > 0) return;

    // 换下一张图纸(§13,依次轮换)→ 清掉旧静态层重画(scene_apply_blueprint,§6.1 允许的
    // 整屏重绘时机)→ 重散一批(§5.4,读的是刚提交的新图纸几何)→ 精灵复位 →
    // 计数/小脸清零 → 回 PLAY。顺序不能乱:必须先切图纸,flock_scatter 才会按新图纸摆点。
    s_bp_idx = next_blueprint();
    scene_apply_blueprint(s_bp_idx);
    flock_scatter(s_animals, ANIMAL_COUNT);
    critters_respawn(s_animals, ANIMAL_COUNT);
    s_home_count[0] = s_home_count[1] = 0;
    s_total = 0;
    scene_set_home_count(0, 0);
    scene_set_home_count(1, 0);
    s_state = ST_PLAY;
    ESP_LOGI(TAG, "重散一批,新一轮开始(图纸 #%d)", s_bp_idx);
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
                // 2026-08-10 摇一摇从彩蛋升级为功能键(SPEC §3):随机方向冲量先冲散
                // 门口堆积,视觉/音效反馈紧随其后。进度计数不受影响(归家还是得靠玩家
                // 自己倾斜对准家门,冲量方向是随机的,不是"游戏帮忙瞄准")。
                flock_shake_impulse(s_animals, ANIMAL_COUNT);
                critters_hop_all();      // 在场动物全体原地小跳
                feedback_emit_shake();   // 叽嘎合唱 + 中震 + 彩虹一闪
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
        // 不带 have:IMU 掉线时反而更该让它睡,否则传感器故障 = 耗干电池
        bool nap_ok = (s_state != ST_PARTY);
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
    // flock_init 的开局摆位是写死给图纸 A 的预置网格(两行、绕 cy=120 手调);再走一次
    // 图纸感知的约束布点,开局才不会有动物生在门判定区里(开机虽恒为 A,但这条不该
    // 依赖"开机是哪张图纸",tools/verify_host.sh 三张图纸各跑 40 个种子验的就是它)。
    flock_scatter(s_animals, ANIMAL_COUNT);
    critters_init(s_animals, ANIMAL_COUNT);

    s_state = ST_ATTRACT;
    critters_set_asleep(true);   // 开机动物们睡着(Zzz + 慢呼吸),倾斜即醒(SPEC §4)
    s_home_count[0] = s_home_count[1] = 0;
    s_total = 0;
    for (int i = 0; i < ANIMAL_COUNT; i++) s_last_bounce[i] = 0;
    s_prev_acc_valid = false;
    s_shake_hits     = 0;
    s_shake_cooldown = 0;
    s_bp_idx = scene_current_blueprint();   // 跟 scene_init 应用的那张对齐(含校验回退)

    core2_sleep_init(&s_sleep, NULL);   // NULL = 实机定案默认值(SPEC §10:全托管,不自定义)

    // 🔴 栈 8192(原 4096):2026-08-10 图纸批起,party_tick() 会在本任务里直调
    // scene_apply_blueprint() → 一整批 lv_obj_clean/lv_obj_create。LVGL 在 LV_OS_NONE 下
    // 跑在**调用者栈**上(根 CLAUDE.md §11),同一条代码路径在 scene_init() 里是跑在
    // app_main 的 CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192 上的 —— 放到 4096 任务里等于把它
    // 减半。栈溢出的表现是 canary → panic → 立刻重启,而本工程重启即回 launcher,
    // 会被误判成"玩着玩着游戏自己退了"(§11 已有 screenshot 在 4096 栈里一按必重启的前科)。
    // 4KB 内部 RAM 换掉一整类只在实机上才暴露、且每次复现都要人工插线的故障。
    xTaskCreate(game_task, "game", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "ATTRACT:%d 只动物睡着入场,倾斜唤醒(完整状态机:睡醒/归家/派对/重散/彩蛋)",
             ANIMAL_COUNT);
}
