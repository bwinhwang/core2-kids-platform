#include "game_state.h"
#include "tuning.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp/m5stack_core_2.h"
#include "imu_mpu6886.h"
#include "physics.h"
#include "maze.h"
#include "render.h"
#include "feedback.h"
#include "ledstrip_fx.h"
#include "haptics.h"
#include "power.h"

static const char *TAG = "game";

typedef enum { ST_ATTRACT, ST_CALIBRATE, ST_PLAY, ST_WIN, ST_IDLE, ST_DEEP_IDLE } state_t;

static state_t          s_state;
static physics_t        s_phys;
static int              s_level_idx;
static const level_t   *s_level;
static vec2_t           s_home_px;
static int              s_last_near;

// 收集星(本关)
static vec2_t           s_star_px[2];
static bool             s_star_got[2];
static int              s_n_stars;

static int    s_frame;                 // 进入当前状态后的帧数
static double s_cx, s_cy, s_cz;        // 校准累加
static int    s_cn;

// M6 共享状态
static volatile bool s_paused;
static volatile bool s_recal_req;
static volatile int  s_max_levels  = 4;
static volatile int  s_play_bright = PLAY_BRIGHTNESS;

// 动作检测(帧间加速度变化)
static imu_accel_t s_prev;
static bool        s_prev_valid;
static float       s_motion;
static int         s_still_frames;
static int         s_motion_frames;   // 连续"明显动作"帧数(唤醒去抖)

#define WIN_HOLD_FRAMES    (WIN_HOLD_MS / PHYS_PERIOD_MS)
#define IDLE_TIMEOUT_FRM   (IDLE_TIMEOUT_MS / PHYS_PERIOD_MS)
#define DEEP_IDLE_TIMEOUT_FRM (DEEP_IDLE_TIMEOUT_MS / PHYS_PERIOD_MS)   // 在 ST_IDLE 里按 16ms 帧计

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

static void enter_calibrate(void)
{
    s_state = ST_CALIBRATE;
    s_frame = 0;
    s_cx = s_cy = s_cz = 0;
    s_cn = 0;
    render_show_splash(0xCDE9F0);
    render_ball_set_pos(PLAY_W / 2, PLAY_H / 2);
    ESP_LOGI(TAG, "CALIBRATE:端平别动…");
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
    s_still_frames = 0;

    s_n_stars = s_level->n_stars;
    for (int i = 0; i < s_n_stars && i < 2; i++) {
        s_star_px[i] = maze_cell_center(s_level->stars[i]);
        s_star_got[i] = false;
    }

    ledstrip_fx_set_base(LED_BASE_AMBIENT);
    bsp_display_brightness_set(s_play_bright);

    s_state = ST_PLAY;
    s_frame = 0;
    ESP_LOGI(TAG, "PLAY: L%d (tier %d)", s_level->id, s_level->tier);
}

static void enter_win(void)
{
    s_state = ST_WIN;
    s_frame = 0;
    feedback_emit_win();
    ESP_LOGI(TAG, "WIN: L%d 到家!", s_level->id);
}

static void enter_idle(void)
{
    s_state = ST_IDLE;
    s_frame = 0;
    s_motion_frames = 0;
    bsp_display_brightness_set(IDLE_BRIGHTNESS);   // 降亮省电
    ledstrip_fx_set_base(LED_BASE_IDLE);
    ESP_LOGI(TAG, "IDLE:打盹(动一下唤醒)");
}

// 深度省电:打盹后仍长时间无动作 → 关屏 + 灯带熄 + 切 M-Bus 5V + 降频轮询(§14)
static void enter_deep_idle(void)
{
    s_state = ST_DEEP_IDLE;
    s_frame = 0;
    s_motion_frames = 0;
    bsp_display_brightness_set(0);          // 先把 DCDC3 电压降到最低
    power_backlight(false);                  // 再真正断 DCDC3 → 背光全灭(brightness 0% 不熄屏)
    ledstrip_fx_set_base(LED_BASE_OFF);     // 灯带熄
    power_bus_5v(false);                     // 切 M-Bus 5V(断灯带 + SY7088 静态电流)
    ESP_LOGI(TAG, "DEEP_IDLE:关屏关灯带深度省电(动一下唤醒)");
}

static void wake_from_idle(void)
{
    power_bus_5v(true);                       // 先恢复 5V(若来自深度省电)
    power_backlight(true);                     // 重新使能 DCDC3(深度省电时被断开)
    bsp_display_brightness_set(s_play_bright);
    ledstrip_fx_set_base(LED_BASE_AMBIENT);
    haptics_play(HAPTIC_WAKE);
    s_still_frames = 0;
    s_motion_frames = 0;
    s_state = ST_PLAY;
    s_frame = 0;
    ESP_LOGI(TAG, "唤醒 → 回 PLAY");
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
        if (dev > ATTRACT_TILT_THRESH) { enter_calibrate(); return; }
    }
    s_frame++;
}

static void calibrate_tick(const imu_accel_t *acc)
{
    if (acc) { s_cx += acc->x; s_cy += acc->y; s_cz += acc->z; s_cn++; }
    s_frame++;

    if (s_cn >= CALIB_FRAMES) {
        imu_accel_t avg = { (float)(s_cx / s_cn), (float)(s_cy / s_cn), (float)(s_cz / s_cn) };
        physics_calibrate(&s_phys, &avg);
        ESP_LOGI(TAG, "校准零点: x=%.3f y=%.3f z=%.3f", avg.x, avg.y, avg.z);
        start_play(s_level_idx);   // 重校准后留在当前关;首次 s_level_idx=0
    }
}

static void play_tick(const imu_accel_t *acc)
{
    physics_step(&s_phys, acc, PHYS_DT);
    maze_collision_t c = maze_resolve_collision(s_level, &s_phys.pos, &s_phys.vel, BALL_R);
    render_ball_update(s_phys.pos.x, s_phys.pos.y, s_phys.vel.x, s_phys.vel.y);

    if (c.hit && c.speed >= BUMP_MIN_SPEED) {
        feedback_emit_bump(c.speed, s_phys.pos.x, s_phys.pos.y);
    }

    // 收集星(顺路经过即收,非过关必经,§4.5)
    for (int i = 0; i < s_n_stars && i < 2; i++) {
        if (s_star_got[i]) continue;
        float dx = s_phys.pos.x - s_star_px[i].x, dy = s_phys.pos.y - s_star_px[i].y;
        float hit = BALL_R + 9.0f;
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

    if (maze_reached_home(s_level, s_phys.pos)) { enter_win(); return; }

    // 打盹检测:仅当"机身基本没动 且 球基本停住"持续 IDLE_TIMEOUT 才打盹。
    // 用明显动作(>唤醒阈值)或球速来清零,避免 IMU 噪声把计数打断而永远不打盹。
    // 打盹判据:"无人看管"= 机身没被动(motion),而非"球停住"(sp)。
    // 球在轻微静态倾斜下会永远以 ~20px/s 慢爬(sp>8),拿它当活动信号会永不打盹(实测坑)。
    if (s_motion > IDLE_WAKE_THRESH) s_still_frames = 0;
    else                             s_still_frames++;
    if (s_still_frames > IDLE_TIMEOUT_FRM) enter_idle();
}

static void win_tick(void)
{
    s_frame++;
    if (s_frame >= WIN_HOLD_FRAMES) {
        int n = s_max_levels;
        if (n < 1) n = 1;
        start_play((s_level_idx + 1) % n);
    }
}

// 唤醒去抖:连续 WAKE_DEBOUNCE_FRAMES 帧都是明显动作才算真的"动了"。
// 单帧 IMU 噪声尖峰(平放时偶发 > IDLE_WAKE_THRESH)会被忽略——否则 60s 里只要
// 有一帧尖峰就会误唤醒、把打盹→深度省电的计时作废,深度省电几乎永远熬不满。
static bool woke_up(void)
{
    if (s_motion > IDLE_WAKE_THRESH) s_motion_frames++;
    else                             s_motion_frames = 0;
    return s_motion_frames >= WAKE_DEBOUNCE_FRAMES;
}

static void idle_tick(void)
{
    if (woke_up()) { wake_from_idle(); return; }
    s_frame++;   // 单帧噪声尖峰不打断计时:woke_up() 已忽略,s_frame 继续累加
    if (s_frame > DEEP_IDLE_TIMEOUT_FRM) enter_deep_idle();   // 打盹够久 → 深度省电
}

static void deep_idle_tick(void)
{
    if (woke_up()) wake_from_idle();
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

        // 帧间动作量(用于打盹/唤醒)
        s_motion = 0;
        if (have) {
            if (s_prev_valid) {
                s_motion = fabsf(acc.x - s_prev.x) + fabsf(acc.y - s_prev.y) + fabsf(acc.z - s_prev.z);
            }
            s_prev = acc;
            s_prev_valid = true;
        }

        if (s_paused) {
            vTaskDelayUntil(&last, pdMS_TO_TICKS(PHYS_PERIOD_MS));
            continue;
        }
        if (s_recal_req) { s_recal_req = false; enter_calibrate(); }

        switch (s_state) {
            case ST_ATTRACT:   attract_tick(pa);   break;
            case ST_CALIBRATE: calibrate_tick(pa); break;
            case ST_PLAY:      if (have) play_tick(pa); break;
            case ST_WIN:       win_tick();         break;
            case ST_IDLE:      idle_tick();        break;
            case ST_DEEP_IDLE: deep_idle_tick();   break;
        }
        // 深度省电时降频轮询(少唤醒 CPU 省电);其余状态维持 60Hz 固定步长
        TickType_t period = (s_state == ST_DEEP_IDLE)
                          ? pdMS_TO_TICKS(DEEP_IDLE_POLL_MS)
                          : pdMS_TO_TICKS(PHYS_PERIOD_MS);
        vTaskDelayUntil(&last, period);
    }
}

void game_state_start(void)
{
    vec2_t start = maze_cell_center(maze_get_level(0)->start);
    physics_init(&s_phys, start.x, start.y);
    s_level_idx = 0;
    xTaskCreate(game_task, "game", 4096, NULL, 5, NULL);
}

// ── 家长菜单接口 ─────────────────────────────────────────────────────
void game_state_set_paused(bool paused) { s_paused = paused; }

void game_state_request_recalibrate(void) { s_recal_req = true; }

void game_state_set_level_band(int max_levels)
{
    if (max_levels < 1) max_levels = 1;
    if (max_levels > maze_level_count()) max_levels = maze_level_count();
    s_max_levels = max_levels;
    ESP_LOGI(TAG, "难度档:可玩 %d 关", s_max_levels);
}

void game_state_set_play_brightness(int pct)
{
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    s_play_bright = pct;
    if (s_state != ST_IDLE && s_state != ST_DEEP_IDLE) bsp_display_brightness_set(pct);
}
