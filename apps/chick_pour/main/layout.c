// 后院版面几何 + 图纸表 + 加载校验(SPEC §13)。**不碰 LVGL**,主机可直编:
//   apps/chick_pour/tools/verify_host.sh
#include "layout.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "layout";

// ── 图纸无关的恒定几何(栅栏/活动边界)────────────────────────────────
const rect_t PLAY_BOUNDS = {
    FENCE_THICK + ANIMAL_R, FENCE_THICK + ANIMAL_R,
    PLAY_W - FENCE_THICK - ANIMAL_R, PLAY_H - FENCE_THICK - ANIMAL_R,
};

// ── 图纸相关(随 layout_apply 写入,scene.c 照着画 / flock.c 照着碰)───────
rect_t      HOUSE_RECT, POND_RECT, HOUSE_GATE, POND_GATE;
home_face_t HOUSE_FACE, POND_FACE;
circ_t      CORNER_BUSH[4];

// ── 图纸表(= tools/preview.py BLUEPRINTS 里 landed=True 的三张,数值逐一对抄)───
static const blueprint_t BP_A = {
    .name  = "A 对门",
    .homes = {
        [LAYOUT_HOME_CHICK] = { HOME_FACE_RIGHT, 56.0f,  120.0f },
        [LAYOUT_HOME_DUCK]  = { HOME_FACE_LEFT,  264.0f, 120.0f },
    },
    .bushes = {
        { 26.0f, 26.0f, CORNER_BUSH_R }, { 294.0f, 26.0f, CORNER_BUSH_R },
        { 26.0f, 214.0f, CORNER_BUSH_R }, { 294.0f, 214.0f, CORNER_BUSH_R },
    },
};

static const blueprint_t BP_C = {
    .name  = "C 对角",
    .homes = {
        [LAYOUT_HOME_CHICK] = { HOME_FACE_RIGHT, 56.0f,  78.0f },
        [LAYOUT_HOME_DUCK]  = { HOME_FACE_LEFT,  264.0f, 162.0f },
    },
    .bushes = {
        { 18.0f, 18.0f, 22.0f }, { 294.0f, 26.0f, CORNER_BUSH_R },
        { 26.0f, 214.0f, CORNER_BUSH_R }, { 302.0f, 222.0f, 22.0f },
    },
};

// ⚠️ 两个家都 face=RIGHT(同侧同向)——flock.c 的门弹出方向必须按各自 face 算,
// 不能沿用"鸡窝恒朝右、池塘恒朝左"的旧写死假设,这张图纸就是专门压这一条的。
static const blueprint_t BP_B = {
    .name  = "B 同侧上下",
    .homes = {
        [LAYOUT_HOME_CHICK] = { HOME_FACE_RIGHT, 56.0f, 72.0f },
        [LAYOUT_HOME_DUCK]  = { HOME_FACE_RIGHT, 56.0f, 168.0f },
    },
    .bushes = {
        { 14.0f, 14.0f, 18.0f }, { 294.0f, 26.0f, CORNER_BUSH_R },
        { 14.0f, 226.0f, 18.0f }, { 294.0f, 214.0f, CORNER_BUSH_R },
    },
};

static const blueprint_t *const s_blueprints[SCENE_BLUEPRINT_COUNT] = { &BP_A, &BP_C, &BP_B };

static int s_bp_cur;   // 当前生效图纸下标(layout_apply 写,含校验回退后的真实值)

int layout_count(void) { return SCENE_BLUEPRINT_COUNT; }
int layout_current(void) { return s_bp_cur; }

const blueprint_t *layout_get(int idx)
{
    if (idx < 0 || idx >= SCENE_BLUEPRINT_COUNT) idx = 0;
    return s_blueprints[idx];
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ── 图纸 → 几何派生(校验与提交共用同一套算式)────────────────────────
static rect_t layout_home_rect(const home_spec_t *h)
{
    rect_t r;
    if (h->face == HOME_FACE_RIGHT) { r.x0 = h->anchor_x - HOUSE_W; r.x1 = h->anchor_x; }
    else                            { r.x0 = h->anchor_x;           r.x1 = h->anchor_x + HOUSE_W; }
    r.y0 = h->cy - HOUSE_H / 2.0f;
    r.y1 = h->cy + HOUSE_H / 2.0f;
    return r;
}

static rect_t layout_home_gate(const home_spec_t *h)
{
    float gy0 = h->cy - GATE_W / 2.0f, gy1 = h->cy + GATE_W / 2.0f;
    if (h->face == HOME_FACE_RIGHT) {
        return (rect_t){ h->anchor_x - GATE_INSET, gy0, h->anchor_x + GATE_DEPTH, gy1 };
    }
    return (rect_t){ h->anchor_x - GATE_DEPTH, gy0, h->anchor_x + GATE_INSET, gy1 };
}

// 门外「进场点」:动物中心要能到这里,门才算不被堵死(tools/preview.py Home.approach)。
static void home_approach(const home_spec_t *h, float *ax, float *ay)
{
    float d = GATE_DEPTH + ANIMAL_R + 2.0f;
    *ax = (h->face == HOME_FACE_RIGHT) ? h->anchor_x + d : h->anchor_x - d;
    *ay = h->cy;
}

// ── 加载时校验(仿 tools/preview.py check_layout)────────────────────────
// 步长用整数字面量(不直接算 PLAY_W/FLOOD_STEP):数组维度需要真正的整数常量表达式,
// float 转 int 的 cast 在 -std=gnu23 下不算 ICE,编译期数组大小会报"storage size isn't
// constant"。320/240 与 tuning.h 的 PLAY_W/PLAY_H 保持一致(屏固定 320×240,不会变)。
#define FLOOD_STEP  4
#define FLOOD_COLS  (320 / FLOOD_STEP + 1)   // 81
#define FLOOD_ROWS  (240 / FLOOD_STEP + 1)   // 61
#define FLOOD_CELLS (FLOOD_COLS * FLOOD_ROWS)

// 场地被切两半时的告警线:低于此比例说明有一整块空地与门口不连通(动物散到那儿就
// 回不了家)。**只告警不判失败** —— 网格是 4px 粗离散,角落里三五格的假死角很常见,
// 为它把一张本来能玩的图纸判死,就是又一次"校验器自己制造失败"。
#define FLOOD_COVER_WARN_PCT 80

_Static_assert(FLOOD_CELLS <= 65535, "flood 栈用 uint16_t 存下标,格数不能超 65535");

// 点是否落在栅栏外 / 家里 / 灌木里(不查门区——flood fill 要把门区本身当"能走到"的地,
// 门区判定属于运行时逻辑,这里只查地形硬障碍,仿 preview.py blocked(clear_gates=False))。
static bool point_blocked(float x, float y, const rect_t homes[2], const circ_t bushes[4])
{
    if (x < PLAY_BOUNDS.x0 || x > PLAY_BOUNDS.x1) return true;
    if (y < PLAY_BOUNDS.y0 || y > PLAY_BOUNDS.y1) return true;
    for (int i = 0; i < 2; i++) {
        if (x >= homes[i].x0 && x <= homes[i].x1 && y >= homes[i].y0 && y <= homes[i].y1) return true;
    }
    for (int i = 0; i < 4; i++) {
        float dx = x - bushes[i].x, dy = y - bushes[i].y;
        if (dx * dx + dy * dy < bushes[i].r * bushes[i].r) return true;
    }
    return false;
}

bool layout_verify(const blueprint_t *bp)
{
    rect_t homes[2] = { layout_home_rect(&bp->homes[0]), layout_home_rect(&bp->homes[1]) };
    rect_t gates[2] = { layout_home_gate(&bp->homes[0]), layout_home_gate(&bp->homes[1]) };
    float ax[2], ay[2];
    home_approach(&bp->homes[0], &ax[0], &ay[0]);
    home_approach(&bp->homes[1], &ax[1], &ay[1]);

    // 1) 家不出栅栏 / 门区不出屏 / 门外进场点在场地内(门朝着场地,不能顶着栅栏)
    for (int i = 0; i < 2; i++) {
        if (!(FENCE_THICK <= homes[i].x0 && homes[i].x1 <= PLAY_W - FENCE_THICK)) {
            ESP_LOGE(TAG, "图纸[%s]家%d横向出栅栏", bp->name, i); return false;
        }
        if (!(FENCE_THICK <= homes[i].y0 && homes[i].y1 <= PLAY_H - FENCE_THICK)) {
            ESP_LOGE(TAG, "图纸[%s]家%d纵向出栅栏", bp->name, i); return false;
        }
        if (!(0.0f < gates[i].x0 && gates[i].x1 < PLAY_W)) {
            ESP_LOGE(TAG, "图纸[%s]家%d门区出屏", bp->name, i); return false;
        }
        if (!(PLAY_BOUNDS.x0 <= ax[i] && ax[i] <= PLAY_BOUNDS.x1)) {
            ESP_LOGE(TAG, "图纸[%s]家%d门外进场点落在栅栏里(门朝向错了)", bp->name, i); return false;
        }
    }

    // 2) 两个家外墙不重叠
    if (homes[0].x0 < homes[1].x1 && homes[1].x0 < homes[0].x1 &&
        homes[0].y0 < homes[1].y1 && homes[1].y0 < homes[0].y1) {
        ESP_LOGE(TAG, "图纸[%s]两个家的外墙重叠", bp->name); return false;
    }

    // 3) 灌木不压家 / 不堵门外进场点
    for (int b = 0; b < 4; b++) {
        const circ_t *c = &bp->bushes[b];
        for (int i = 0; i < 2; i++) {
            float nx = clampf(c->x, homes[i].x0, homes[i].x1);
            float ny = clampf(c->y, homes[i].y0, homes[i].y1);
            float dx = c->x - nx, dy = c->y - ny;
            if (sqrtf(dx * dx + dy * dy) < c->r) {
                ESP_LOGE(TAG, "图纸[%s]灌木%d压住家%d", bp->name, b, i); return false;
            }
            dx = c->x - ax[i]; dy = c->y - ay[i];
            if (sqrtf(dx * dx + dy * dy) < c->r + ANIMAL_R) {
                ESP_LOGE(TAG, "图纸[%s]灌木%d堵死家%d门外进场点", bp->name, b, i); return false;
            }
        }
    }

    // 4) 粗网格 flood fill:从家0的门外进场点出发,要走得到家1的门外进场点
    //    (tilt_maze BFS 可解性校验的开阔场地降级版)。静态缓冲存 BSS,不占
    //    game_task 的栈 —— 本函数会在 party_tick() 里被调用。
    static uint8_t  free_cell[FLOOD_CELLS];
    static uint8_t  seen[FLOOD_CELLS];
    static uint16_t stk[FLOOD_CELLS];
    memset(seen, 0, sizeof(seen));

    // 🔴 这两层循环必须把整张网格填满,别再往外层塞提前退出条件。
    // 2026-08-13 修的就是这里:原来外层写作 `c < FLOOD_COLS && start < 0`,本意是
    // "找到第一个空地格就不用再找种子了",实际把**填表**也一起终止了 —— 后面 74 列
    // 保持 BSS 的 0(= 障碍),flood fill 只能在最左那一条 x=24 的窄缝里爬,于是
    // A/C/B 三张图纸全判"门走不到"、全部静默回退图纸 A。屏幕上的表现是"玩多久都
    // 只有一张后院",与图纸怎么选(随机还是轮换)毫无关系,查了三轮才逮到。
    int free_total = 0;
    for (int c = 0; c < FLOOD_COLS; c++) {
        for (int r = 0; r < FLOOD_ROWS; r++) {
            bool blk = point_blocked(c * FLOOD_STEP, r * FLOOD_STEP, homes, bp->bushes);
            free_cell[c * FLOOD_ROWS + r] = blk ? 0 : 1;
            if (!blk) free_total++;
        }
    }
    if (free_total == 0) { ESP_LOGE(TAG, "图纸[%s]场地被堵成没有空地", bp->name); return false; }

    // 种子取家0的门外进场点(而不是"扫描序里第一个空地格"):要验的性质本来就是
    // "从这个门口能不能走到那个门口",拿门口当起点最贴题;拿扫描序第一格当起点则
    // 可能落进某个角落死水洼,把本来能玩的图纸判死 —— 同上那类"校验器自己制造失败"。
    int ai[2];
    for (int i = 0; i < 2; i++) {
        int tc = (int)(ax[i] / FLOOD_STEP + 0.5f), tr = (int)(ay[i] / FLOOD_STEP + 0.5f);
        if (tc < 0 || tc >= FLOOD_COLS || tr < 0 || tr >= FLOOD_ROWS) {
            ESP_LOGE(TAG, "图纸[%s]家%d门外进场点出网格", bp->name, i); return false;
        }
        ai[i] = tc * FLOOD_ROWS + tr;
        if (!free_cell[ai[i]]) {
            ESP_LOGE(TAG, "图纸[%s]家%d门外进场点本身就是障碍", bp->name, i); return false;
        }
    }

    int sp = 0, reached = 0;
    stk[sp++] = (uint16_t)ai[0];
    seen[ai[0]] = 1;
    static const int dc[4] = { 1, -1, 0, 0 }, dr[4] = { 0, 0, 1, -1 };
    while (sp > 0) {
        int idx = stk[--sp];
        reached++;
        int c = idx / FLOOD_ROWS, r = idx % FLOOD_ROWS;
        for (int k = 0; k < 4; k++) {
            int nc = c + dc[k], nr = r + dr[k];
            if (nc < 0 || nc >= FLOOD_COLS || nr < 0 || nr >= FLOOD_ROWS) continue;
            int nidx = nc * FLOOD_ROWS + nr;
            if (!free_cell[nidx] || seen[nidx]) continue;
            seen[nidx] = 1;
            stk[sp++] = (uint16_t)nidx;
        }
    }

    if (!seen[ai[1]]) {
        ESP_LOGE(TAG, "图纸[%s]两个家的门口互相走不到(被地形封死)", bp->name);
        return false;
    }
    int cover = reached * 100 / free_total;
    if (cover < FLOOD_COVER_WARN_PCT) {
        ESP_LOGW(TAG, "图纸[%s]场地只有 %d%% 与门口连通,余下那块里的动物回不了家", bp->name, cover);
    }
    return true;
}

int layout_apply(int idx)
{
    if (idx < 0 || idx >= SCENE_BLUEPRINT_COUNT) idx = 0;
    const blueprint_t *bp = s_blueprints[idx];

    if (!layout_verify(bp)) {
        ESP_LOGE(TAG, "回退到图纸[%s]", s_blueprints[0]->name);
        bp  = s_blueprints[0];  // 图纸 A 是基准图纸,不再二次校验(防递归失败,§2 原则 1)
        idx = 0;                // 🔴 回退后下标要跟着回退,否则派对后的 +1 轮换会从一个
                                //    没真正生效的下标算起
    }
    s_bp_cur = idx;

    HOUSE_RECT = layout_home_rect(&bp->homes[LAYOUT_HOME_CHICK]);
    POND_RECT  = layout_home_rect(&bp->homes[LAYOUT_HOME_DUCK]);
    HOUSE_GATE = layout_home_gate(&bp->homes[LAYOUT_HOME_CHICK]);
    POND_GATE  = layout_home_gate(&bp->homes[LAYOUT_HOME_DUCK]);
    HOUSE_FACE = bp->homes[LAYOUT_HOME_CHICK].face;
    POND_FACE  = bp->homes[LAYOUT_HOME_DUCK].face;
    for (int i = 0; i < 4; i++) CORNER_BUSH[i] = bp->bushes[i];

    return idx;
}

int layout_selftest(void)
{
    int pass = 0;
    for (int i = 0; i < SCENE_BLUEPRINT_COUNT; i++) {
        if (layout_verify(s_blueprints[i])) pass++;
        else ESP_LOGE(TAG, "图纸自检:#%d [%s] 不可用,运行时会回退图纸 A",
                      i, s_blueprints[i]->name);
    }
    if (pass == SCENE_BLUEPRINT_COUNT) {
        ESP_LOGI(TAG, "图纸自检:%d/%d 通过", pass, SCENE_BLUEPRINT_COUNT);
    } else {
        ESP_LOGE(TAG, "🔴 图纸自检:只有 %d/%d 通过 —— 后院不会轮换,先修校验/图纸再谈玩法",
                 pass, SCENE_BLUEPRINT_COUNT);
    }
    return pass;
}
