#include "flock.h"
#include "scene.h"
#include "tuning.h"

#include <math.h>
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "flock";

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool in_rect(float x, float y, rect_t r)
{
    return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

// ── 共享板级倾斜滤波(§8.3 同款低通 + 减除式死区)────────────────────────
// 板子只有一个倾斜姿态,10 只动物共用同一份滤波状态;每只的差异只在后面的增益抖动。
static imu_accel_t s_a_filt;
static bool        s_filt_init;

// 把 IMU 三轴(g)映射到屏幕平面(x→右,y→下)—— 与 tilt_maze 同一台机器同一标定,
// 数值原样抄 tilt_maze/main/tuning.h(TILT_SWAP_XY/INVERT_X/INVERT_Y)。
static void map_axes(const imu_accel_t *a, float *sx_out, float *sy_out)
{
#if TILT_SWAP_XY
    float sx = a->y, sy = a->x;
#else
    float sx = a->x, sy = a->y;
#endif
#if TILT_INVERT_X
    sx = -sx;
#endif
#if TILT_INVERT_Y
    sy = -sy;
#endif
    *sx_out = sx;
    *sy_out = sy;
}

// 每帧只算一次:低通滤波 → 映射 → 减除式死区,得到共享倾斜向量。
static void shared_tilt(const imu_accel_t *accel_raw, float *tx_out, float *ty_out)
{
    if (!s_filt_init) {
        s_a_filt = *accel_raw;
        s_filt_init = true;
    }
    const float a = TILT_ALPHA;
    s_a_filt.x = s_a_filt.x * (1 - a) + accel_raw->x * a;
    s_a_filt.y = s_a_filt.y * (1 - a) + accel_raw->y * a;
    s_a_filt.z = s_a_filt.z * (1 - a) + accel_raw->z * a;

    float tx, ty;
    map_axes(&s_a_filt, &tx, &ty);

    // 减除式死区(tilt_maze §8.3 step4 同款):只把"超出死区的部分"交给增益,
    // 硬截断会造成悬崖(零点稍污染就吃全额恒力,单向匀速爬),减除式下阻尼压得住。
    float tmag = sqrtf(tx * tx + ty * ty);
    if (tmag < DEADZONE) {
        tx = 0;
        ty = 0;
    } else {
        float k = (tmag - DEADZONE) / tmag;
        tx *= k;
        ty *= k;
    }
    *tx_out = tx;
    *ty_out = ty;
}

// ── 静态障碍滑行碰撞(仿 tilt_maze maze.c 的 AABB-vs-圆 / physics.c 的边界钳位)──────
// 统一写回 a->bumped / a->bump_speed(取本帧多次碰撞里最大的法向速度)。

static void resolve_bounds(animal_t *a)
{
    if (a->x < PLAY_BOUNDS.x0) {
        a->x = PLAY_BOUNDS.x0;
        if (a->vx < 0) {
            float speed = -a->vx;
            if (speed > a->bump_speed) a->bump_speed = speed;
            a->vx = -a->vx * WALL_RESTITUTION;
            a->bumped = true;
        }
    } else if (a->x > PLAY_BOUNDS.x1) {
        a->x = PLAY_BOUNDS.x1;
        if (a->vx > 0) {
            float speed = a->vx;
            if (speed > a->bump_speed) a->bump_speed = speed;
            a->vx = -a->vx * WALL_RESTITUTION;
            a->bumped = true;
        }
    }
    if (a->y < PLAY_BOUNDS.y0) {
        a->y = PLAY_BOUNDS.y0;
        if (a->vy < 0) {
            float speed = -a->vy;
            if (speed > a->bump_speed) a->bump_speed = speed;
            a->vy = -a->vy * WALL_RESTITUTION;
            a->bumped = true;
        }
    } else if (a->y > PLAY_BOUNDS.y1) {
        a->y = PLAY_BOUNDS.y1;
        if (a->vy > 0) {
            float speed = a->vy;
            if (speed > a->bump_speed) a->bump_speed = speed;
            a->vy = -a->vy * WALL_RESTITUTION;
            a->bumped = true;
        }
    }
}

// 圆(动物)vs 矩形(鸡窝/池塘外墙)滑行碰撞:矩形上离圆心最近的点做法线。
static void resolve_rect(animal_t *a, rect_t r)
{
    float qx = clampf(a->x, r.x0, r.x1);
    float qy = clampf(a->y, r.y0, r.y1);
    float dx = a->x - qx, dy = a->y - qy;
    float d2 = dx * dx + dy * dy;
    if (d2 >= ANIMAL_R * ANIMAL_R) return;

    float nx, ny, pen;
    float d = sqrtf(d2);
    if (d > 0.0001f) {
        nx = dx / d; ny = dy / d; pen = ANIMAL_R - d;
    } else {
        // 圆心已经陷进矩形内部(极端情况):沿最小穿透面推出。
        float pl = a->x - r.x0, pr = r.x1 - a->x, pt = a->y - r.y0, pb = r.y1 - a->y;
        float mh = fminf(pl, pr), mv = fminf(pt, pb);
        if (mh < mv) { nx = (pl < pr) ? -1 : 1; ny = 0; pen = mh + ANIMAL_R; }
        else         { nx = 0; ny = (pt < pb) ? -1 : 1; pen = mv + ANIMAL_R; }
    }
    a->x += nx * pen;
    a->y += ny * pen;

    float vn = a->vx * nx + a->vy * ny;
    if (vn < 0) {
        float speed = -vn;
        if (speed > a->bump_speed) a->bump_speed = speed;
        a->vx -= (1.0f + WALL_RESTITUTION) * vn * nx;
        a->vy -= (1.0f + WALL_RESTITUTION) * vn * ny;
        a->bumped = true;
    }
}

// 家外墙(P2):动物中心在门区内 → 墙碰撞豁免,交给门判定接管(SPEC §5.2)。
// 正常路径下门判定先触发(GATE_DEPTH > ANIMAL_R),这里是软分离把动物横着挤进
// 门区深处等边角情况的保护,防"门里被墙推出来"。
static void resolve_home(animal_t *a, rect_t wall, rect_t gate)
{
    if (in_rect(a->x, a->y, gate)) return;
    resolve_rect(a, wall);
}

// 圆(动物)vs 圆(灌木)滑行碰撞,同样只推位置 + 抵消法向速度,不硬弹。
static void resolve_circle(animal_t *a, circ_t c)
{
    float dx = a->x - c.x, dy = a->y - c.y;
    float d2 = dx * dx + dy * dy;
    float rr = ANIMAL_R + c.r;
    if (d2 >= rr * rr || d2 < 1e-6f) return;

    float d = sqrtf(d2);
    float nx = dx / d, ny = dy / d, pen = rr - d;
    a->x += nx * pen;
    a->y += ny * pen;

    float vn = a->vx * nx + a->vy * ny;
    if (vn < 0) {
        float speed = -vn;
        if (speed > a->bump_speed) a->bump_speed = speed;
        a->vx -= (1.0f + WALL_RESTITUTION) * vn * nx;
        a->vy -= (1.0f + WALL_RESTITUTION) * vn * ny;
        a->bumped = true;
    }
}

static void resolve_obstacles(animal_t *a)
{
    resolve_bounds(a);
    resolve_home(a, HOUSE_RECT, HOUSE_GATE);
    resolve_home(a, POND_RECT, POND_GATE);
    for (int i = 0; i < 4; i++) resolve_circle(a, CORNER_BUSH[i]);
}

// ── 门判定(SPEC §5.2:单向 + 语义匹配)──────────────────────────────
// 动物中心进入门区:种类匹配 → 捕获(从物理仿真移除,位置停在原地当动画起点);
// 不匹配 → 沿门法线温柔弹出(GATE_BOUNCE_SPEED;鸡窝法线 +x,池塘法线 -x),
// 切向速度减半("温柔");位置推到门区外沿,防下一帧立刻再触发。
// 弹出反馈的节流(BOUNCE_SND_COOLDOWN_MS,每只)由 game_state 做,物理弹出每次照做。
static void gate_check(animal_t *a)
{
    if (in_rect(a->x, a->y, HOUSE_GATE)) {
        if (a->kind == ANIMAL_CHICK) {
            a->active = false;
            a->gate_event = GATE_EV_CAPTURE;
        } else {
            a->x  = HOUSE_GATE.x1 + 1.0f;
            a->vx = GATE_BOUNCE_SPEED;
            a->vy *= 0.5f;
            a->gate_event = GATE_EV_BOUNCE;
        }
    } else if (in_rect(a->x, a->y, POND_GATE)) {
        if (a->kind == ANIMAL_DUCK) {
            a->active = false;
            a->gate_event = GATE_EV_CAPTURE;
        } else {
            a->x  = POND_GATE.x0 - 1.0f;
            a->vx = -GATE_BOUNCE_SPEED;
            a->vy *= 0.5f;
            a->gate_event = GATE_EV_BOUNCE;
        }
    }
}

// ── 布点 ─────────────────────────────────────────────────────────────
// 预置网格(init 与 §5.4 兜底共用):场地中段两行,带 ±10px 抖动,构造上安全永不失败。
// y 偏移 ±34:行离门区上下边(y=98/142)≥12px,动物出生时不会踩进门区触发判定
// (P1 时是 ±26,P2 引入门区后上调,理由即此)。
static void grid_layout(float xs[], float ys[], int n)
{
    const int cols = (n + 1) / 2;
    for (int i = 0; i < n; i++) {
        int col = i % cols;
        int row = i / cols;
        float base_x = PLAY_BOUNDS.x0 + (PLAY_BOUNDS.x1 - PLAY_BOUNDS.x0) * (col + 1) / (cols + 1);
        float base_y = PLAY_H / 2 + (row == 0 ? -34.0f : 34.0f);
        float jx = (float)(esp_random() % 21) - 10.0f;   // ±10px,别对齐成阅兵队列
        float jy = (float)(esp_random() % 21) - 10.0f;
        xs[i] = clampf(base_x + jx, PLAY_BOUNDS.x0, PLAY_BOUNDS.x1);
        ys[i] = clampf(base_y + jy, PLAY_BOUNDS.y0, PLAY_BOUNDS.y1);
    }
}

// 点到矩形的距离(在矩形内 = 0)
static float pt_rect_dist(float x, float y, rect_t r)
{
    float dx = fmaxf(fmaxf(r.x0 - x, 0), x - r.x1);
    float dy = fmaxf(fmaxf(r.y0 - y, 0), y - r.y1);
    return sqrtf(dx * dx + dy * dy);
}

// 单点合法性(§5.4 约束):栅栏内(由采样范围保证)、离两家门区 ≥SCATTER_GATE_CLEAR、
// 不踩两家外墙(≥ANIMAL_R+2)、避开四角灌木、与已布点两两 ≥SCATTER_MIN_GAP。
static bool scatter_pt_ok(float x, float y, const float xs[], const float ys[], int placed)
{
    if (pt_rect_dist(x, y, HOUSE_GATE) < SCATTER_GATE_CLEAR) return false;
    if (pt_rect_dist(x, y, POND_GATE) < SCATTER_GATE_CLEAR) return false;
    if (pt_rect_dist(x, y, HOUSE_RECT) < ANIMAL_R + 2) return false;
    if (pt_rect_dist(x, y, POND_RECT) < ANIMAL_R + 2) return false;
    for (int b = 0; b < 4; b++) {
        float dx = x - CORNER_BUSH[b].x, dy = y - CORNER_BUSH[b].y;
        float rr = CORNER_BUSH[b].r + ANIMAL_R + 2;
        if (dx * dx + dy * dy < rr * rr) return false;
    }
    for (int j = 0; j < placed; j++) {
        float dx = x - xs[j], dy = y - ys[j];
        if (dx * dx + dy * dy < (float)SCATTER_MIN_GAP * SCATTER_MIN_GAP) return false;
    }
    return true;
}

static float rand_range(float lo, float hi)
{
    return lo + (esp_random() % 10001) / 10000.0f * (hi - lo);
}

// 布完的校验断言(§5.4"verify 纪律"):点数由调用方保证,这里查界内 + 两两间距。
static bool scatter_validate(const float xs[], const float ys[], int n)
{
    for (int i = 0; i < n; i++) {
        if (xs[i] < PLAY_BOUNDS.x0 || xs[i] > PLAY_BOUNDS.x1 ||
            ys[i] < PLAY_BOUNDS.y0 || ys[i] > PLAY_BOUNDS.y1) return false;
        for (int j = i + 1; j < n; j++) {
            float dx = xs[i] - xs[j], dy = ys[i] - ys[j];
            // 网格兜底自身间距 ≥27px > SCATTER_MIN_GAP,同一把尺子两条路径都量得过
            if (dx * dx + dy * dy < (float)SCATTER_MIN_GAP * SCATTER_MIN_GAP) return false;
        }
    }
    return true;
}

// ── 公开接口 ─────────────────────────────────────────────────────────

void flock_init(animal_t animals[], int n)
{
    s_filt_init = false;

    float xs[ANIMAL_COUNT], ys[ANIMAL_COUNT];
    grid_layout(xs, ys, n);

    for (int i = 0; i < n; i++) {
        animals[i].x  = xs[i];
        animals[i].y  = ys[i];
        animals[i].vx = 0;
        animals[i].vy = 0;
        animals[i].kind = (i % 2 == 0) ? ANIMAL_CHICK : ANIMAL_DUCK;   // 交替分配,5:5,视觉上混着散

        int jitter_pct = (int)(esp_random() % (2 * GAIN_JITTER_PCT + 1)) - GAIN_JITTER_PCT;
        animals[i].gain_mul = 1.0f + jitter_pct / 100.0f;

        animals[i].active     = true;
        animals[i].gate_event = GATE_EV_NONE;
        animals[i].bumped     = false;
        animals[i].bump_speed = 0;
    }
}

void flock_step(animal_t animals[], int n, const imu_accel_t *accel_raw, float dt)
{
    float tx, ty;
    shared_tilt(accel_raw, &tx, &ty);

    // ⓪ 事件字段每帧清零重写(inactive 的也清,防旧事件被重复消费)
    for (int i = 0; i < n; i++) {
        animals[i].gate_event = GATE_EV_NONE;
        animals[i].bumped     = false;
        animals[i].bump_speed = 0;
    }

    // ① 每只 active:增益抖动 → 加速度 → 阻尼 → 封顶 → 积分(tilt_maze §8.3 同套,
    //    只是 TILT_GAIN 按各自 gain_mul 再缩放一道 —— 同一倾斜下各自快慢略不同,
    //    是群体自然散开、不结成一坨的关键,SPEC §3)。
    for (int i = 0; i < n; i++) {
        animal_t *a = &animals[i];
        if (!a->active) continue;
        a->vx += tx * TILT_GAIN * a->gain_mul * dt;
        a->vy += ty * TILT_GAIN * a->gain_mul * dt;
        a->vx *= DAMPING;
        a->vy *= DAMPING;

        float sp = sqrtf(a->vx * a->vx + a->vy * a->vy);
        if (sp > VEL_MAX) {
            float k = VEL_MAX / sp;
            a->vx *= k;
            a->vy *= k;
        }

        a->x += a->vx * dt;
        a->y += a->vy * dt;
    }

    // ② 两两软分离(active 对;≤45 对,O(N²) 在此规模可忽略):不做真刚体碰撞,
    //    只沿连线各推一半(SPEC §5.1②:"挤挤挨挨本身是可爱的",不处理速度)。
    const float min_dist = 2 * ANIMAL_R + SEP_PAD;
    for (int i = 0; i < n; i++) {
        if (!animals[i].active) continue;
        for (int j = i + 1; j < n; j++) {
            if (!animals[j].active) continue;
            float dx = animals[j].x - animals[i].x;
            float dy = animals[j].y - animals[i].y;
            float d2 = dx * dx + dy * dy;
            if (d2 >= min_dist * min_dist || d2 < 1e-6f) continue;

            float d = sqrtf(d2);
            float pen = (min_dist - d) * (SEP_CORRECT_PCT / 100.0f);
            float nx = dx / d, ny = dy / d;
            animals[i].x -= nx * pen * 0.5f;
            animals[i].y -= ny * pen * 0.5f;
            animals[j].x += nx * pen * 0.5f;
            animals[j].y += ny * pen * 0.5f;
        }
    }

    // ③ 门判定(§5.2,先于墙碰撞:捕获的本帧即移除,弹出的位置/速度已改)。
    for (int i = 0; i < n; i++) {
        if (animals[i].active) gate_check(&animals[i]);
    }

    // ④ 栅栏 / 灌木 / 家外墙(门区段豁免):滑行碰撞(不粘住),写回 bumped/bump_speed。
    for (int i = 0; i < n; i++) {
        if (animals[i].active) resolve_obstacles(&animals[i]);
    }
}

bool flock_scatter(animal_t animals[], int n)
{
    float xs[ANIMAL_COUNT], ys[ANIMAL_COUNT];
    bool random_ok = true;

    // 约束随机 + 拒绝采样(§5.4):每只至多 SCATTER_MAX_TRIES 次;任一只放不下
    // 整批退化为预置网格(永不失败)。
    for (int i = 0; i < n && random_ok; i++) {
        bool placed = false;
        for (int t = 0; t < SCATTER_MAX_TRIES; t++) {
            float x = rand_range(PLAY_BOUNDS.x0, PLAY_BOUNDS.x1);
            float y = rand_range(PLAY_BOUNDS.y0, PLAY_BOUNDS.y1);
            if (scatter_pt_ok(x, y, xs, ys, i)) {
                xs[i] = x;
                ys[i] = y;
                placed = true;
                break;
            }
        }
        if (!placed) random_ok = false;
    }

    // 校验断言兜底(verify 纪律):拒绝采样成功也要复核;不过再走网格。
    if (random_ok && !scatter_validate(xs, ys, n)) {
        ESP_LOGE(TAG, "重散校验失败(不该发生),退预置网格");
        random_ok = false;
    }
    if (!random_ok) {
        ESP_LOGW(TAG, "重散走预置网格兜底");
        grid_layout(xs, ys, n);
    }

    for (int i = 0; i < n; i++) {
        animals[i].x  = xs[i];
        animals[i].y  = ys[i];
        animals[i].vx = 0;
        animals[i].vy = 0;
        animals[i].active     = true;    // 全体复活;gain_mul/kind 保留(init 定死)
        animals[i].gate_event = GATE_EV_NONE;
        animals[i].bumped     = false;
        animals[i].bump_speed = 0;
    }
    return random_ok;
}
