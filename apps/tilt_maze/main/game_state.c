#include "game_state.h"
#include "tuning.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include "bsp/m5stack_core_2.h"
#include "imu_mpu6886.h"
#include "physics.h"
#include "maze.h"
#include "render.h"
#include "feedback.h"
#include "ledstrip_fx.h"
#include "core2_sleep.h"

static const char *TAG = "game";

// 打盹/深度省电不再是本状态机的状态:整套两级省电编排(含唤醒)由 core2_sleep 组件管,
// 非清醒时跳过游戏逻辑即可(见 game_task)。
// 无 CALIBRATE 态:绝对零点免校准(§20.9),"平"=与地面平,ATTRACT 触发直接进 PLAY。
typedef enum { ST_ATTRACT, ST_PLAY, ST_FAIL, ST_WIN } state_t;

static state_t          s_state;
static physics_t        s_phys;
static int              s_level_idx;
static const level_t   *s_level;
static vec2_t           s_home_px;
static int              s_last_near;

// 收集星(本关;2026-07-27 起过关必收,见 play_tick 到家判定)
static vec2_t           s_star_px[2];
static bool             s_star_got[2];
static int              s_n_stars;
static bool             s_home_hinted;   // 已在本次"停在家门口"提示过"还差星星"(上升沿去重)

// 静态陷阱格 + 巡逻怪(2026-07-27 放弃零失败,见 maze.h 顶注)
static bool             s_trap_active;
static vec2_t           s_trap_px;

// 巡逻怪运行态(每关最多 MAZE_HAZARDS 只;走法见 maze.h 的 hazard_t)
typedef struct {
    vec2_t px[MAZE_HAZARD_PTS];  // 拐点的像素坐标
    int    n;
    bool   loop;
    int    seg;                  // 当前所在段:px[seg] → px[下一个]
    float  t;                    // 0~1,段内插值位置
    float  dir;                  // 往返用:+1 前进 / -1 回头;绕圈恒 +1
    float  dwell;                // 端点剩余停留 ms(>0 不移动);绕圈不用
    vec2_t pos;
} hazard_run_t;
static hazard_run_t     s_haz[MAZE_HAZARDS];
static int              s_n_haz;

static int    s_frame;                 // 进入当前状态后的帧数

// M6 共享状态
static volatile bool s_paused;
static volatile int  s_play_bright = PLAY_BRIGHTNESS;

// 两级省电编排器(打盹→深度省电→去抖唤醒,判据/时序见 components/core2_sleep)
static core2_sleep_t s_sleep;

#define WIN_HOLD_FRAMES    (WIN_HOLD_MS / PHYS_PERIOD_MS)
#define FAIL_HOLD_FRAMES   (FAIL_HOLD_MS / PHYS_PERIOD_MS)

// ── 状态进入 ─────────────────────────────────────────────────────────
static void enter_attract(void)
{
    s_state = ST_ATTRACT;
    s_frame = 0;
    render_show_splash(0xFFE0A8);
    ledstrip_fx_set_base(LED_BASE_AMBIENT);
    bsp_display_brightness_set(s_play_bright);
    ESP_LOGI(TAG, "ATTRACT:倾斜机身开始玩");
}

static void start_play(int idx)
{
    s_level_idx = idx;
    s_level = maze_get_level(idx);
    if (!maze_is_solvable(s_level)) {
        ESP_LOGE(TAG, "关卡 L%d 不连通!数据有误", s_level->id);
    }
    render_load_level(s_level);

    vec2_t start = maze_cell_center(s_level->start);
    physics_set_position(&s_phys, start.x, start.y);
    s_home_px = maze_cell_center(s_level->home);
    s_last_near = 0;
    core2_sleep_kick(&s_sleep);   // 进关清一次静止计时

    s_n_stars = s_level->n_stars;
    for (int i = 0; i < s_n_stars && i < 2; i++) {
        s_star_px[i] = maze_cell_center(s_level->stars[i]);
        s_star_got[i] = false;
    }
    s_home_hinted = false;

    s_trap_active = (s_level->trap.col >= 0);
    if (s_trap_active) s_trap_px = maze_cell_center(s_level->trap);

    s_n_haz = s_level->n_hazards;
    if (s_n_haz > MAZE_HAZARDS) s_n_haz = MAZE_HAZARDS;
    for (int i = 0; i < s_n_haz; i++) {
        const hazard_t *d = &s_level->hazards[i];
        hazard_run_t *h = &s_haz[i];
        h->n = (d->n_pts > MAZE_HAZARD_PTS) ? MAZE_HAZARD_PTS : d->n_pts;
        h->loop = d->loop;
        if (h->n < 2) {   // 数据不全:本槽位当没怪(verify_mazes.py 会先拦下,这里只兜底)
            ESP_LOGE(TAG, "L%d 怪%d 只有 %d 个拐点,忽略", s_level->id, i + 1, h->n);
            h->n = 0;
            continue;
        }
        for (int k = 0; k < h->n; k++) h->px[k] = maze_cell_center(d->pts[k]);
        h->seg = 0;
        h->t = 0.0f;
        h->dir = 1.0f;
        h->dwell = 0.0f;
        h->pos = h->px[0];   // 重进本关时怪一律回 pts[0](与球回起点同步,失败后局面可复现)
    }

    ledstrip_fx_set_base(LED_BASE_AMBIENT);
    bsp_display_brightness_set(s_play_bright);

    s_state = ST_PLAY;
    s_frame = 0;
    ESP_LOGI(TAG, "PLAY: L%d", s_level->id);
}

// 踩陷阱/撞巡逻怪:退回本关起点(2026-07-27 放弃零失败,§14 用户拍板"只退本关"而非
// 生命值/连续惩罚)。直接复用 start_play 重进同一关:球回起点、星星重新可收、
// 巡逻怪回端点 A、家动画重置——全部状态一次性归零,不用另写一套局部重置逻辑。
//
// 2026-08-18「死得有戏」:重进本关的动作推迟到 FAIL_HOLD_MS 之后,中间是 ST_FAIL 定格。
// 原来 emit 完同一帧就 start_play,演出和重置撞在一起——屏上只剩"球忽然回到起点",
// 输了这件事根本没被看见。ST_FAIL 期间不跑物理、不动怪,纯放演出。
static void trigger_fail(float x, float y)
{
    render_fail_burst(x, y);              // 视觉直调(同 render_collect_star 的先例):
                                          // 走反馈队列会晚几毫秒,定格的第一帧就对不齐
    feedback_emit_fail(x, y);             // 音/震/灯仍走反馈队列,不阻塞本任务
    ledstrip_fx_set_base(LED_BASE_OFF);   // 演出期间灯带熄:紫闪 → 全黑 → 重生亮回暖色
    s_state = ST_FAIL;
    s_frame = 0;
}

static void fail_tick(void)
{
    if (++s_frame >= FAIL_HOLD_FRAMES) start_play(s_level_idx);   // 灯带基础模式也在这里恢复
}

static void enter_win(void)
{
    s_state = ST_WIN;
    s_frame = 0;
    feedback_emit_win();
    ESP_LOGI(TAG, "WIN: L%d 到家!", s_level->id);
}

// ── 洗牌袋选关 ───────────────────────────────────────────────────────
// 一轮内每关恰好出现一次(顺序随机),打完一轮重洗。
// 独立随机(esp_random()%n)在小样本下重复感强(生日悖论),故用洗牌袋。
#define LEVEL_BAG_MAX 16
static int s_bag[LEVEL_BAG_MAX];
static int s_bag_pos, s_bag_n;   // s_bag_n=0 表示袋空

static int bag_clamp_n(void)
{
    int n = maze_level_count();
    if (n < 1) n = 1;
    if (n > LEVEL_BAG_MAX) n = LEVEL_BAG_MAX;
    return n;
}

static void shuffle_bag(int n)
{
    for (int i = 0; i < n; i++) s_bag[i] = i;
    for (int i = n - 1; i > 0; i--) {          // Fisher–Yates
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int t = s_bag[i]; s_bag[i] = s_bag[j]; s_bag[j] = t;
    }
    s_bag_n = n;
    s_bag_pos = 0;
}

static int next_level_from_bag(void)
{
    int n = bag_clamp_n();
    if (n == 1) return 0;

    if (s_bag_n != n || s_bag_pos >= s_bag_n) {
        shuffle_bag(n);
        // 新一轮首关不与刚玩完的那关背靠背重复
        if (s_bag[0] == s_level_idx) {
            int t = s_bag[0]; s_bag[0] = s_bag[n - 1]; s_bag[n - 1] = t;
        }
    }
    return s_bag[s_bag_pos++];
}

// 开局关:16 张同难度(2026-07-08 取消难度渐进),不再固定 L1 起手,
// 直接洗一轮新袋随机开局。
static int first_level_from_bag(void)
{
    int n = bag_clamp_n();
    if (n == 1) return 0;

    shuffle_bag(n);
    return s_bag[s_bag_pos++];
}

// ── 各状态每帧 ───────────────────────────────────────────────────────
static int near_level(vec2_t pos)
{
    float dx = pos.x - s_home_px.x, dy = pos.y - s_home_px.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 1.1f * MAZE_CELL) return 3;
    if (d < 1.9f * MAZE_CELL) return 2;
    if (d < 2.8f * MAZE_CELL) return 1;
    return 0;
}

static void attract_tick(const imu_accel_t *acc)
{
    static imu_accel_t ref;
    if (s_frame == 0 && acc) ref = *acc;

    float y = PLAY_H / 2 + 14.0f * sinf(s_frame * 0.12f);
    render_ball_set_pos(PLAY_W / 2, y);

    if (acc && s_frame > 15) {
        float dev = fabsf(acc->x - ref.x) + fabsf(acc->y - ref.y) + fabsf(acc->z - ref.z);
        // 绝对零点免校准(§20.9):触发即开玩;开局 L1 并计入第一轮洗牌袋
        if (dev > ATTRACT_TILT_THRESH) { start_play(first_level_from_bag()); return; }
    }
    s_frame++;
}

static void play_tick(const imu_accel_t *acc)
{
    physics_step(&s_phys, acc, PHYS_DT);
    maze_collision_t c = maze_resolve_collision(s_level, &s_phys.pos, &s_phys.vel, BALL_R);
    render_ball_update(s_phys.pos.x, s_phys.pos.y, s_phys.vel.x, s_phys.vel.y);

    if (c.hit && c.speed >= BUMP_MIN_SPEED) {
        feedback_emit_bump(c.speed, s_phys.pos.x, s_phys.pos.y);
    }

    // 静态陷阱格:踩中退回本关起点(2026-07-27 放弃零失败)
    if (s_trap_active) {
        float dx = s_phys.pos.x - s_trap_px.x, dy = s_phys.pos.y - s_trap_px.y;
        float hit = BALL_R + TRAP_R;
        if (dx * dx + dy * dy < hit * hit) {
            trigger_fail(s_trap_px.x, s_trap_px.y);
            return;
        }
    }

    // 巡逻怪:沿 pts 直角折线走,碰到退回本关起点。两种走法(2026-08-03 七写加绕圈,
    // 为什么绕圈怪能站在必经路上而直线怪不能,见 maze.h 顶注):
    //   loop=false —— pts[0]…pts[n-1] 往返(triangle wave),两端各停一下,节奏靠停顿预判;
    //   loop=true  —— pts[n-1] 接回 pts[0] 单向绕圈、不停,节奏靠"一圈恒定时长"预判。
    for (int i = 0; i < s_n_haz; i++) {
        hazard_run_t *h = &s_haz[i];
        if (h->n < 2) continue;

        int nxt = h->loop ? (h->seg + 1) % h->n : h->seg + 1;
        vec2_t a = h->px[h->seg], b = h->px[nxt];
        float sx = b.x - a.x, sy = b.y - a.y;
        float seg_len = sqrtf(sx * sx + sy * sy);

        if (h->dwell > 0.0f) {
            h->dwell -= PHYS_PERIOD_MS;
        } else if (seg_len > 0.001f) {
            float speed = h->loop ? HAZARD_LOOP_SPEED : HAZARD_SPEED;
            h->t += h->dir * (speed * PHYS_DT / seg_len);
            if (h->t >= 1.0f) {
                if (h->loop) {                        // 绕圈:进下一段,不停
                    h->t -= 1.0f;
                    h->seg = nxt;
                } else if (h->seg + 2 < h->n) {       // 往返:折线中途,进下一段
                    h->t -= 1.0f;
                    h->seg++;
                } else {                              // 往返:到尾端,停一下再回头
                    h->t = 1.0f; h->dir = -1.0f; h->dwell = HAZARD_DWELL_MS;
                }
            } else if (h->t <= 0.0f) {
                if (h->seg > 0) {                     // 往返:折线中途,退回上一段
                    h->t += 1.0f;
                    h->seg--;
                } else {                              // 往返:到首端,停一下再往前
                    h->t = 0.0f; h->dir = 1.0f; h->dwell = HAZARD_DWELL_MS;
                }
            }
            a = h->px[h->seg];
            nxt = h->loop ? (h->seg + 1) % h->n : h->seg + 1;
            b = h->px[nxt];
            sx = b.x - a.x; sy = b.y - a.y;
        }
        h->pos.x = a.x + sx * h->t;
        h->pos.y = a.y + sy * h->t;
        render_hazard_update(i, h->pos.x, h->pos.y);

        float dx = s_phys.pos.x - h->pos.x, dy = s_phys.pos.y - h->pos.y;
        float hit = BALL_R + HAZARD_R;
        if (dx * dx + dy * dy < hit * hit) {
            trigger_fail(h->pos.x, h->pos.y);
            return;
        }
    }

    // 收集星(顺路经过即收;2026-07-27 起过关必须全收,见下方到家判定,§4.5)
    for (int i = 0; i < s_n_stars && i < 2; i++) {
        if (s_star_got[i]) continue;
        float dx = s_phys.pos.x - s_star_px[i].x, dy = s_phys.pos.y - s_star_px[i].y;
        float hit = BALL_R + STAR_R;
        if (dx * dx + dy * dy < hit * hit) {
            s_star_got[i] = true;
            feedback_emit_collect(s_star_px[i].x, s_star_px[i].y);
            render_collect_star(i);
        }
    }

    int nl = near_level(s_phys.pos);
    if (nl != s_last_near) {
        feedback_emit_near(nl);
        render_home_excited(nl >= 2);   // 越近脉动越快(§5.2)
        s_last_near = nl;
    }

    // 到家:星星现在是过关必收(2026-07-27 放弃零失败,原为可选加分项)。
    // 差星星时给"还差星星"提示(上升沿一次:轻音+轻震+把没收的星闪几下指路),
    // 不算失败、不重置——2026-07-27 实机反馈"到家没反应像坏了",故必须给明确反馈;
    // 离开家门口(超出 GOAL_R)清标志,再回来会重新提示一次。
    if (maze_reached_home(s_level, s_phys.pos)) {
        bool all_stars = true;
        for (int i = 0; i < s_n_stars && i < 2; i++) {
            if (!s_star_got[i]) { all_stars = false; break; }
        }
        if (all_stars) { enter_win(); return; }
        if (!s_home_hinted) {
            s_home_hinted = true;
            feedback_emit_need_stars();
        }
    } else {
        s_home_hinted = false;
    }
    // 打盹/深度省电/唤醒由 core2_sleep 在 game_task 里统一编排,这里不再检测
}

static void win_tick(void)
{
    s_frame++;
    if (s_frame >= WIN_HOLD_FRAMES) {
        start_play(next_level_from_bag());
    }
}

// 休眠/唤醒:停掉(恢复)家的无限脉动动画。game_task 只是跳过自己的逻辑与渲染,
// LVGL 那条动画时间线不归它管,不显式删就会穿透打盹一直重绘。
static void on_sleep_stage(core2_sleep_stage_t from, core2_sleep_stage_t to)
{
    (void)from;
    render_set_sleeping(to != CORE2_SLEEP_AWAKE);
}

// ── 主任务 ───────────────────────────────────────────────────────────
static void game_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    enter_attract();

    for (;;) {
        imu_accel_t acc;
        bool have = (imu_mpu6886_read_accel(&acc) == ESP_OK);
        const imu_accel_t *pa = have ? &acc : NULL;

        if (s_paused) {
            core2_sleep_pace(&last, PHYS_PERIOD_MS);
            continue;
        }

        // 两级省电编排(打盹→深度省电→去抖唤醒)交给 core2_sleep;深度省电时返回值自动降频。
        // 🔴 ATTRACT 也必须算"可打盹":它是开机初始态,从 launcher 点进来没真去玩就放下
        // 会一直卡在这儿,曾导致永不打盹、一路耗干电池(2026-08-12 修)。
        // 也不再要求本帧读到 IMU:IMU 掉线时反而更该让它睡,否则传感器故障=耗干电池。
        int delay_ms = core2_sleep_feed(&s_sleep,
                                        have ? (float[]){ acc.x, acc.y, acc.z } : NULL,
                                        s_state == ST_PLAY || s_state == ST_ATTRACT);
        if (core2_sleep_stage(&s_sleep) == CORE2_SLEEP_AWAKE) {
            switch (s_state) {
                case ST_ATTRACT: attract_tick(pa);   break;
                case ST_PLAY:    if (have) play_tick(pa); break;
                case ST_FAIL:    fail_tick();        break;
                case ST_WIN:     win_tick();         break;
            }
        }
        // 🔴 帧节拍走 core2_sleep_pace,不许裸 vTaskDelayUntil(逾期会攒时间欠债 →
        // 之后连跑几百帧还债 = 全场对象突然加速,因果见 core2_sleep.h 文件头)
        core2_sleep_pace(&last, delay_ms);
    }
}

void game_state_start(void)
{
    vec2_t start = maze_cell_center(maze_get_level(0)->start);
    physics_init(&s_phys, start.x, start.y);

    // 两级省电:全部用 tuning.h 的定案值(组件默认值即同一套,这里显式传以便调参)
    core2_sleep_cfg_t scfg = CORE2_SLEEP_CFG_DEFAULT;
    scfg.nap_after_ms     = IDLE_TIMEOUT_MS;
    scfg.deep_after_ms    = DEEP_IDLE_TIMEOUT_MS;
    scfg.awake_brightness = PLAY_BRIGHTNESS;
    scfg.nap_brightness   = IDLE_BRIGHTNESS;
    scfg.frame_ms         = PHYS_PERIOD_MS;
    scfg.deep_poll_ms     = DEEP_IDLE_POLL_MS;
    scfg.wake_thresh      = IDLE_WAKE_THRESH;
    scfg.wake_frames      = WAKE_DEBOUNCE_FRAMES;
    scfg.on_stage_change  = on_sleep_stage;
    core2_sleep_init(&s_sleep, &scfg);

    s_level_idx = 0;
    xTaskCreate(game_task, "game", 4096, NULL, 5, NULL);
}

// ── 家长菜单接口 ─────────────────────────────────────────────────────
void game_state_set_paused(bool paused) { s_paused = paused; }

void game_state_set_play_brightness(int pct)
{
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    s_play_bright = pct;
    // 清醒态立即生效;休眠态由组件记住,唤醒时恢复到新值
    core2_sleep_set_awake_brightness(&s_sleep, pct);
}
