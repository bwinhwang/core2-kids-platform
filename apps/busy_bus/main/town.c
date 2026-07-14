// busy_bus —— 静态层实现(SPEC.md §2/§7)。几何手工编排(非随机生成),
// 三栋房 + 树/喷泉障碍 + 5 个站牌点(每轮随机选 3 个)+ 左下角车库。
#include "town.h"

#include "bsp/m5stack_core_2.h"

#include "tuning.h"

#define BG_COL        0xE8EFD8   // 草地暖绿
#define ROAD_COL      0xE0D8B8   // 浅色环形马路(装饰)
#define HEDGE_COL     0x5E9450   // 树篱围边(比树深一号,与障碍区分)
#define HOUSE_ROOF_ACCENT_DK 0x3A3A38
#define WINDOW_DARK   0x6B6357
#define WINDOW_LIT    0xFFD976
#define DOOR_COL      0x6B4A2E
#define TREE_COL      0x6FBF73
#define TREE_TRUNK    0x8A5A32
#define FOUNTAIN_COL  0x7FC7E0
#define FOUNTAIN_RIM  0xEAE6DA
#define GARAGE_COL    0xC6CDBE   // 车库地垫:冷灰绿,原暖棕调与巴士车身暖橙(0xFB8B24)几乎同色,开局车身"藏"进车库里
#define GARAGE_MARK   0xF3EFE4   // 车位描边:浅米,像地面漆线,同时跟新地垫拉开明度

#define HOUSE_W   56
#define HOUSE_H   40
#define ROOF_H    14

// 房子:0=RED(上左,门朝下) 1=BLUE(上右,门朝下) 2=GREEN(下中,门朝上)
static const rect_t HOUSE_BODY[TOWN_HOUSES] = {
    { 58,  22, HOUSE_W, HOUSE_H },
    { 226, 22, HOUSE_W, HOUSE_H },
    { 140, 178, HOUSE_W, HOUSE_H },
};
static const rect_t HOUSE_DOOR[TOWN_HOUSES] = {
    { 62,  64, DOOR_ZONE_W, DOOR_ZONE_H },   // RED:门垫在房下方
    { 230, 64, DOOR_ZONE_W, DOOR_ZONE_H },   // BLUE:门垫在房下方
    { 144, 146, DOOR_ZONE_W, DOOR_ZONE_H },  // GREEN:门垫在房上方
};
static const uint32_t HOUSE_COLOR[TOWN_HOUSES] = { 0xE6533C, 0x4FB0D8, 0x6FBF73 };
static const uint32_t HOUSE_ROOF[TOWN_HOUSES]  = { 0xB8402E, 0x3A87A8, 0x4E9052 };

static const vec2_t STOPS[TOWN_STOPS] = {
    { 160,  22 }, { 26, 100 }, { 296,  60 }, { 296, 190 }, { 112, 228 },
};

static const circle_t OBSTACLES[TOWN_OBSTACLES] = {
    { 160, 120, 18 },   // 中央喷泉
    { 100, 155, 13 },   // 树 1
    { 220,  90, 13 },   // 树 2
};

static const vec2_t GARAGE_POS = { 44, 205 };

// ── LVGL 对象(仅"亮灯"两态需要保留引用)────────────────────────────────
static lv_obj_t *s_house_window[TOWN_HOUSES];
static bool      s_house_lit[TOWN_HOUSES];

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

static lv_obj_t *outline(lv_obj_t *parent, int w, int h, uint32_t border, int bw, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(o, bw, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// 门朝向:false=朝下(贴房身底沿),true=朝上(贴屋檐下沿)。必须跟 HOUSE_DOOR 的送达
// 判定区同侧——GREEN 的判定区在房子上方(喷泉那一侧,巴士够得到),下方紧贴屏幕下树篱、
// 碰撞半径根本挤不进那条缝(算过:房身下沿到树篱仅 16px,巴士碰撞半径 14px 进不去)。
// 原先视觉门不分朝向一律画在底沿,GREEN 玩家会对着一扇够不到的门,送不进乘客。
static const bool HOUSE_DOOR_UP[TOWN_HOUSES] = { false, false, true };

static void make_house(lv_obj_t *parent, int idx)
{
    const rect_t *b = &HOUSE_BODY[idx];

    lv_obj_t *body = plain(parent, (int)b->w, (int)b->h - ROOF_H, HOUSE_COLOR[idx], 6);
    lv_obj_set_pos(body, (int)b->x, (int)b->y + ROOF_H);

    lv_obj_t *roof = plain(parent, (int)b->w + 6, ROOF_H, HOUSE_ROOF[idx], 4);
    lv_obj_set_pos(roof, (int)b->x - 3, (int)b->y);

    // 窗+门竖直预算 = HOUSE_H-ROOF_H(26px);16px+12px=28px 天生重叠,两者各改窄矮
    // (14×12)分居屋檐下沿/底沿两端,留 2px 缝,不再重叠、也不再互相遮挡点亮反馈。
    bool door_up = HOUSE_DOOR_UP[idx];
    int  door_x  = (int)(b->x + b->w / 2 - 7);
    int  eave_y  = (int)b->y + ROOF_H + 2;         // 贴屋檐下沿
    int  sill_y  = (int)(b->y + b->h - 12);        // 贴房身底沿

    lv_obj_t *win = plain(parent, 14, 12, WINDOW_DARK, 3);
    lv_obj_set_pos(win, door_x, door_up ? sill_y : eave_y);
    s_house_window[idx] = win;
    s_house_lit[idx] = false;

    lv_obj_t *door = plain(parent, 14, 12, DOOR_COL, 2);
    lv_obj_set_pos(door, door_x, door_up ? eave_y : sill_y);
}

static void make_garage(lv_obj_t *parent)
{
    lv_obj_t *pad = plain(parent, 40, 30, GARAGE_COL, 8);
    lv_obj_set_pos(pad, (int)(GARAGE_POS.x - 20), (int)(GARAGE_POS.y - 15));
    lv_obj_t *mark = outline(parent, 26, 18, GARAGE_MARK, 3, 4);
    lv_obj_set_pos(mark, (int)(GARAGE_POS.x - 13), (int)(GARAGE_POS.y - 9));
}

static void make_obstacles(lv_obj_t *parent)
{
    // 喷泉(索引 0):外圈 + 内圈水色
    const circle_t *f = &OBSTACLES[0];
    lv_obj_t *rim = plain(parent, (int)(f->r * 2), (int)(f->r * 2), FOUNTAIN_RIM, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(rim, (int)(f->cx - f->r), (int)(f->cy - f->r));
    lv_obj_t *water = plain(rim, (int)(f->r * 2 - 8), (int)(f->r * 2 - 8), FOUNTAIN_COL, LV_RADIUS_CIRCLE);
    lv_obj_center(water);

    // 树(索引 1,2):树冠 + 树干
    for (int i = 1; i < TOWN_OBSTACLES; i++) {
        const circle_t *t = &OBSTACLES[i];
        lv_obj_t *trunk = plain(parent, 6, 10, TREE_TRUNK, 2);
        lv_obj_set_pos(trunk, (int)(t->cx - 3), (int)(t->cy + t->r - 6));
        lv_obj_t *crown = plain(parent, (int)(t->r * 2), (int)(t->r * 2), TREE_COL, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(crown, (int)(t->cx - t->r), (int)(t->cy - t->r));
    }
}

void town_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(BG_COL), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // 树篱围边(四条,纯装饰,不约束行驶)
    plain(parent, SCREEN_W, 6, HEDGE_COL, 0);                                  // 上
    lv_obj_t *bo = plain(parent, SCREEN_W, 6, HEDGE_COL, 0);
    lv_obj_set_pos(bo, 0, SCREEN_H - 6);                                       // 下
    lv_obj_t *lo = plain(parent, 6, SCREEN_H, HEDGE_COL, 0);
    lv_obj_set_pos(lo, 0, 0);                                                  // 左
    lv_obj_t *ro = plain(parent, 6, SCREEN_H, HEDGE_COL, 0);
    lv_obj_set_pos(ro, SCREEN_W - 6, 0);                                       // 右

    // 环形马路(装饰性描边,纯氛围,不约束行驶)
    lv_obj_t *road = outline(parent, 250, 176, ROAD_COL, 14, 88);
    lv_obj_align(road, LV_ALIGN_CENTER, 0, 0);

    make_obstacles(parent);
    make_garage(parent);
    for (int i = 0; i < TOWN_HOUSES; i++) make_house(parent, i);
}

void town_house_set_lit(int idx, bool lit)
{
    if (idx < 0 || idx >= TOWN_HOUSES || s_house_lit[idx] == lit) return;
    s_house_lit[idx] = lit;
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_house_window[idx], lv_color_hex(lit ? WINDOW_LIT : WINDOW_DARK), 0);
    bsp_display_unlock();
}

// v: 255→白(眨的一瞬),过阈值后回暗窗色;单条 lv_anim 当"延时后自动归位"用,
// 不需要完成回调、不需要记 idx(var 就是窗对象自身)。
static void cb_window_blink(void *var, int32_t v)
{
    lv_obj_t *win = (lv_obj_t *)var;
    lv_obj_set_style_bg_color(win, lv_color_hex(v > 128 ? 0xFFFFFF : WINDOW_DARK), 0);
}

void town_house_blink(int idx)
{
    if (idx < 0 || idx >= TOWN_HOUSES) return;
    // 亮着的窗不借用暗色眨眼(会误读成"熄灯"),只有暗窗才眨白一下再退回原色
    if (s_house_lit[idx]) return;
    bsp_display_lock(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_house_window[idx]);
    lv_anim_set_exec_cb(&a, cb_window_blink);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_duration(&a, 180);
    lv_anim_start(&a);
    bsp_display_unlock();
}

uint32_t town_house_color(int idx)
{
    return (idx >= 0 && idx < TOWN_HOUSES) ? HOUSE_COLOR[idx] : 0xFFFFFF;
}

vec2_t town_house_center(int idx)
{
    if (idx < 0 || idx >= TOWN_HOUSES) return (vec2_t){ 0, 0 };
    const rect_t *b = &HOUSE_BODY[idx];
    return (vec2_t){ b->x + b->w / 2, b->y + b->h / 2 };
}

rect_t town_house_door(int idx)
{
    return (idx >= 0 && idx < TOWN_HOUSES) ? HOUSE_DOOR[idx] : (rect_t){ 0, 0, 0, 0 };
}

rect_t town_house_body(int idx)
{
    return (idx >= 0 && idx < TOWN_HOUSES) ? HOUSE_BODY[idx] : (rect_t){ 0, 0, 0, 0 };
}

vec2_t town_stop_point(int idx)
{
    return (idx >= 0 && idx < TOWN_STOPS) ? STOPS[idx] : (vec2_t){ 0, 0 };
}

vec2_t town_garage_pos(void) { return GARAGE_POS; }

const circle_t *town_obstacles(int *count)
{
    if (count) *count = TOWN_OBSTACLES;
    return OBSTACLES;
}
