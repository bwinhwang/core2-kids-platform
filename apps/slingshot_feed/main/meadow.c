// slingshot_feed —— 静态层实现(SPEC.md §2/§5.5/§7)。天空+草坡两块扁平色带(避免运行时
// 渐变 banding,CLAUDE.md §6.3)+ 云×2 树×2 篱笆装饰 + 弹弓 Y 叉(make_bar 一次性摆角度,
// 运行时不再旋转,与 town.c 45°屋顶同款静态旋转手法);动物位置表手工编排(非随机生成),
// 加载时用 SPEC §5.5 的真实可达包络(safety parabola)准则校验可达性——不是简化圆形距离
// 判据(那个已被 SPEC 明令禁止,对高处目标会高估可达性)。皮筋/果子/动物是动态精灵,归 sprites.c。
#include "meadow.h"

#include <math.h>

#include "bsp/m5stack_core_2.h"
#include "esp_log.h"
#include "esp_random.h"

#include "tuning.h"

static const char *TAG = "meadow";

#define SKY_COL     0xBEE3F5   // 天空浅蓝
#define GRASS_COL   0xDCEEC8   // 草坡暖绿
#define HORIZON_Y   110        // 天/草分界线(装饰性,不做碰撞)
#define CLOUD_COL   0xFFFFFF
#define TREE_COL    0x6FBF73
#define TREE_TRUNK  0x8A5A32
#define FENCE_COL   0x8A5A32
#define FLOWER_PETAL 0xE38FC2
#define FLOWER_CENTER 0xF5D742

// ── 动物位置候选表(手工编排;§5.5 校验准则 = 满拉状态下仅靠角度可命中)──────────
typedef struct { float x, y; const char *label; } spot_def_t;

// 弹弓居中(x=160)后,位置表左右对称铺开:近/低(易)→ 中 → 高(难),两侧各一套 + 顶心。
// 校验用 SPEC §5.5 真实可达包络(fabsf(dx) 天生对称),满拉可达且需一点瞄准的才留。
static const spot_def_t RAW_SPOTS[] = {
    {  55, 170, "left_low"    },
    {  35, 120, "left_mid"    },
    {  85,  62, "left_high"   },
    { 265, 170, "right_low"   },
    { 285, 120, "right_mid"   },
    { 235,  62, "right_high"  },
    { 160,  40, "top_center"  },
};
#define RAW_SPOT_COUNT (int)(sizeof(RAW_SPOTS) / sizeof(RAW_SPOTS[0]))

static vec2_t s_spots[RAW_SPOT_COUNT];
static int    s_spot_count;

// ── miss 小花对象池(SPEC §7:落定即一次性烘进静态层,之后零成本)─────────────
static lv_obj_t *s_flower_petal[MISS_FLOWER_MAX];
static lv_obj_t *s_flower_center[MISS_FLOWER_MAX];
static int       s_flower_count;

// ── "好朋友"聚集排(改进 B):喂饱的动物蹦到草地边攒成一排,凑够 ANIMAL_QUOTA 只 → 派对群跳。
// 左右分布避开正中弹弓(x≈130~190);交替填充,越攒越对称。身体色 = 所喂物种。
#define FRIEND_EYE_COL  0x453A2C
_Static_assert(ANIMAL_QUOTA == 4, "FRIEND_X 摆位表按 4 个好朋友手排,改 ANIMAL_QUOTA 需同步这张表");
static const int FRIEND_X[ANIMAL_QUOTA] = { 60, 260, 24, 296 };   // 填充序:左内→右内→左外→右外
static lv_obj_t *s_friend_body[ANIMAL_QUOTA];
static lv_obj_t *s_friend_eye_l[ANIMAL_QUOTA];
static lv_obj_t *s_friend_eye_r[ANIMAL_QUOTA];
static int       s_friend_count;

static lv_obj_t *plain(lv_obj_t *parent, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void make_cloud(lv_obj_t *parent, int cx, int cy)
{
    lv_obj_t *c = plain(parent, 30, 16, CLOUD_COL, 8);
    lv_obj_set_pos(c, cx - 15, cy - 8);
    lv_obj_t *bump = plain(parent, 18, 18, CLOUD_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(bump, cx - 4, cy - 13);
}

static void make_tree(lv_obj_t *parent, int cx, int groundy, int crown_r)
{
    lv_obj_t *trunk = plain(parent, 6, 16, TREE_TRUNK, 2);
    lv_obj_set_pos(trunk, cx - 3, groundy - 4);
    lv_obj_t *crown = plain(parent, crown_r * 2, crown_r * 2, TREE_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(crown, cx - crown_r, groundy - crown_r * 2 + 10);
}

// 弹弓 prong/handle:一次性画一根"棒"(从 (x0,y0) 转到指向 (x1,y1)),仅 init 调用一次
// (运行时不再旋转,CLAUDE.md §6 渲染红线;与 town.c 45°屋顶同款静态旋转手法)。
static void make_bar(lv_obj_t *parent, float x0, float y0, float x1, float y1, int th, uint32_t col)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) len = 1.0f;
    float ang = atan2f(dy, dx) * 180.0f / (float)M_PI;

    lv_obj_t *bar = plain(parent, (int)len, th, col, th / 2);
    lv_obj_set_pos(bar, (int)x0, (int)(y0 - th / 2.0f));
    lv_obj_set_style_transform_pivot_x(bar, 0, 0);
    lv_obj_set_style_transform_pivot_y(bar, th / 2, 0);
    lv_obj_set_style_transform_angle(bar, (int32_t)(ang * 10.0f), 0);
}

static void make_fork(lv_obj_t *parent)
{
    make_bar(parent, SLING_ANCHOR_X, SLING_ANCHOR_Y, FORK_TIP_L_X, FORK_TIP_L_Y, 5, FENCE_COL);
    make_bar(parent, SLING_ANCHOR_X, SLING_ANCHOR_Y, FORK_TIP_R_X, FORK_TIP_R_Y, 5, FENCE_COL);
    make_bar(parent, SLING_ANCHOR_X, SLING_ANCHOR_Y, SLING_ANCHOR_X, FORK_HANDLE_BOTTOM_Y, 6, FENCE_COL);
}

static void make_fence(lv_obj_t *parent)
{
    for (int x = 4; x < SCREEN_W - 4; x += 26) {
        lv_obj_t *post = plain(parent, 5, 14, FENCE_COL, 1);
        lv_obj_set_pos(post, x, SCREEN_H - 14);
    }
    lv_obj_t *rail = plain(parent, SCREEN_W, 4, FENCE_COL, 0);
    lv_obj_set_pos(rail, 0, SCREEN_H - 10);
}

// ── 动物位置表加载校验(SPEC §5.5:真实可达包络 safety parabola,已修正,取代
// 曾用过的简化圆形判据"距锚点 ≤ 0.9×v²/G"——圆形判据对高处目标(树杈/云朵)会高估约
// 一倍可达性,2026-07-15 实算发现误判后 SPEC 已改判据,这里同步跟改)───────────────
// 以弹弓锚点为原点、y 向上:满拉初速 v=LAUNCH_POWER 时可达 ⇔ y ≤ v²/(2G) − G·x²/(2v²)。
// 屏幕坐标 y 向下,锚点更靠下(SLING_ANCHOR_Y 较大)、目标越靠屏幕上方 y 越小 → 物理 y
// (向上为正)= 锚点 y − 目标 y。留 ~10% 距离余量:水平距离按 1/0.9 放大再判(与曾用的
// 圆形判据"×0.9"是同一手法的直接移植,只是现在乘在距离维上)。
#define ENVELOPE_DIST_MARGIN 0.9f   // 留 ~10% 距离余量(SPEC §5.5)

static void validate_spots(void)
{
    const float v2 = (float)LAUNCH_POWER * (float)LAUNCH_POWER;
    const float g  = (float)GRAVITY;
    const float anchor_x = SLING_ANCHOR_X, anchor_y = SLING_ANCHOR_Y;

    s_spot_count = 0;
    for (int i = 0; i < RAW_SPOT_COUNT; i++) {
        float dx    = RAW_SPOTS[i].x - anchor_x;
        float y_up  = anchor_y - RAW_SPOTS[i].y;               // 屏幕 y 向下 → 物理 y 向上换算
        float dx_eff = fabsf(dx) / ENVELOPE_DIST_MARGIN;        // 留 10% 距离余量
        float envelope_y = v2 / (2.0f * g) - g * dx_eff * dx_eff / (2.0f * v2);
        if (y_up <= envelope_y) {
            s_spots[s_spot_count++] = (vec2_t){ RAW_SPOTS[i].x, RAW_SPOTS[i].y };
        } else {
            ESP_LOGW(TAG, "动物位置 %s 超出满拉可达包络(y_up=%.1fpx > 包络上限 %.1fpx,dx=%.0fpx),已剔除",
                     RAW_SPOTS[i].label, y_up, envelope_y, dx);
        }
    }
    if (s_spot_count == 0) {
        ESP_LOGE(TAG, "动物位置表全部校验失败!兜底保留第 0 个位置(游戏仍可运行,难度未经校验)");
        s_spots[0] = (vec2_t){ RAW_SPOTS[0].x, RAW_SPOTS[0].y };
        s_spot_count = 1;
    }
    ESP_LOGI(TAG, "动物位置表校验通过 %d/%d(真实包络,LAUNCH_POWER=%d GRAVITY=%d)",
             s_spot_count, RAW_SPOT_COUNT, LAUNCH_POWER, GRAVITY);
}

void meadow_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(GRASS_COL), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    lv_obj_t *sky = plain(parent, SCREEN_W, HORIZON_Y, SKY_COL, 0);
    lv_obj_set_pos(sky, 0, 0);

    // 装饰重摆:配合居中弹弓 + 左右对称位置表(树垫在两侧中/高位动物后,云在顶部)
    make_cloud(parent, 55, 34);
    make_cloud(parent, 160, 20);
    make_cloud(parent, 265, 30);

    make_tree(parent, 42, 152, 20);
    make_tree(parent, 285, 152, 20);

    make_fence(parent);
    make_fork(parent);

    validate_spots();

    for (int i = 0; i < MISS_FLOWER_MAX; i++) {
        s_flower_petal[i]  = plain(parent, 10, 10, FLOWER_PETAL, LV_RADIUS_CIRCLE);
        s_flower_center[i] = plain(parent, 4, 4, FLOWER_CENTER, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(s_flower_petal[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_flower_center[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_flower_count = 0;

    for (int i = 0; i < ANIMAL_QUOTA; i++) {
        s_friend_body[i]  = plain(parent, FRIEND_SZ, FRIEND_SZ, 0xCCCCCC, LV_RADIUS_CIRCLE);
        s_friend_eye_l[i] = plain(parent, 3, 3, FRIEND_EYE_COL, LV_RADIUS_CIRCLE);
        s_friend_eye_r[i] = plain(parent, 3, 3, FRIEND_EYE_COL, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(s_friend_body[i],  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_friend_eye_l[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_friend_eye_r[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_friend_count = 0;
}

vec2_t meadow_sling_anchor(void) { return (vec2_t){ SLING_ANCHOR_X, SLING_ANCHOR_Y }; }

int meadow_spot_count(void) { return s_spot_count; }

vec2_t meadow_spot_pos(int idx)
{
    if (idx < 0 || idx >= s_spot_count) return (vec2_t){ SCREEN_W / 2.0f, SCREEN_H / 2.0f };
    return s_spots[idx];
}

int meadow_pick_spot(int exclude_idx)
{
    if (s_spot_count <= 1) return 0;
    int idx;
    do {
        idx = (int)(esp_random() % (uint32_t)s_spot_count);
    } while (idx == exclude_idx);
    return idx;
}

// 🔴 从 game_task(enter_miss/tick_party)调用,与 LVGL 渲染任务并发 → 必须持锁再动 LVGL 对象
// (CLAUDE.md §6.2:所有 LVGL 操作包 bsp_display_lock/unlock)。漏锁会踩烂 LVGL 失效区链表,
// 表现为 lv_inv_area 死循环 → 轮询任务不喂狗 → task_wdt(2026-07-15 实机确诊,首个 miss 即触发)。
void meadow_flower_add(float x, float y)
{
    if (s_flower_count >= MISS_FLOWER_MAX) return;   // 满了/换批清场前不再增加(SPEC §7)
    int i = s_flower_count++;
    bsp_display_lock(0);
    lv_obj_set_pos(s_flower_petal[i], (int)(x - 5), (int)(y - 5));
    lv_obj_set_pos(s_flower_center[i], (int)(x - 2), (int)(y - 2));
    lv_obj_remove_flag(s_flower_petal[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_flower_center[i], LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

void meadow_flower_clear(void)
{
    bsp_display_lock(0);
    for (int i = 0; i < s_flower_count; i++) {
        lv_obj_add_flag(s_flower_petal[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_flower_center[i], LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
    s_flower_count = 0;
}

// ── "好朋友"聚集排(改进 B)──────────────────────────────────────────────────
// 🔴 同 flower:从 game_task 调用,必须自带锁再动 LVGL(CLAUDE.md §6.2,别重蹈 miss 花漏锁看门狗)。
static void friend_place(int i, int dy)
{
    int fx = FRIEND_X[i];
    lv_obj_set_pos(s_friend_body[i],  fx - FRIEND_SZ / 2, FRIEND_Y - FRIEND_SZ / 2 + dy);
    lv_obj_set_pos(s_friend_eye_l[i], fx - 4, FRIEND_Y - 3 + dy);
    lv_obj_set_pos(s_friend_eye_r[i], fx + 2, FRIEND_Y - 3 + dy);
}

int meadow_friend_count(void) { return s_friend_count; }

void meadow_friend_add(uint32_t body_col)
{
    if (s_friend_count >= ANIMAL_QUOTA) return;
    int i = s_friend_count++;
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_friend_body[i], lv_color_hex(body_col), 0);
    friend_place(i, 0);
    lv_obj_remove_flag(s_friend_body[i],  LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_friend_eye_l[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_friend_eye_r[i], LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

void meadow_friends_bob(int dy)   // 派对群跳:整排一起上下(仅 PARTY 态低频调用)
{
    bsp_display_lock(0);
    for (int i = 0; i < s_friend_count; i++) friend_place(i, dy);
    bsp_display_unlock();
}

void meadow_friends_clear(void)
{
    bsp_display_lock(0);
    for (int i = 0; i < s_friend_count; i++) {
        lv_obj_add_flag(s_friend_body[i],  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_friend_eye_l[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_friend_eye_r[i], LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
    s_friend_count = 0;
}
