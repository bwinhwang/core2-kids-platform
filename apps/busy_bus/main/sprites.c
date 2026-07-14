// busy_bus —— 精灵烘焙 + 装扮实现(SPEC.md §5/§7)。
//
// 巴士:LVGL 无旋转贴图基元(运行时转会是逐帧 CPU 仿射变换,违反 §6 渲染红线),
// 所以跟 tilt_maze 的五角星一样,init 时把"车身基图"按 8 个朝向、每个朝向逐像素
// 4×4 超采样、通过反向旋转采样点回本地坐标系测试形状,烘成 8 张 ARGB8888 图。
// 之后运行时只换图 + 挪坐标,零运行时旋转变换。字节序 B,G,R,A(lv_color32_t)。
#include "sprites.h"

#include <math.h>

#include "bsp/m5stack_core_2.h"

#include "tuning.h"

// ── 巴士本地形状(朝向 0 = 车头朝 +x/右)─────────────────────────────────
#define BODY_HW   10.0f
#define BODY_HH    6.0f
#define BODY_R     3.0f
#define WIN_CX     5.0f
#define WIN_HW     3.0f
#define WIN_HH     4.0f
#define LIGHT_X   10.0f
#define LIGHT_Y    4.0f
#define LIGHT_R    1.6f

// B,G,R(0xRRGGBB 拆出的字节序,匹配 lv_color32_t 内存布局)
static const uint8_t BODY_COL[3]  = { 0x24, 0x8B, 0xFB };   // 车身:暖橙 0xFB8B24
static const uint8_t WIN_COL[3]   = { 0xFA, 0xF3, 0xDF };   // 车窗:浅蓝玻璃 0xDFF3FA
static const uint8_t LIGHT_COL[3] = { 0xD8, 0xF6, 0xFF };   // 车灯:暖白 0xFFF6D8

static uint8_t        s_bus_px[HEADING_COUNT][BUS_SPRITE_PX * BUS_SPRITE_PX * 4];
static lv_image_dsc_t s_bus_dsc[HEADING_COUNT];

static bool in_rounded_rect(float x, float y, float hw, float hh, float r)
{
    float ax = fabsf(x), ay = fabsf(y);
    if (ax > hw || ay > hh) return false;
    if (ax <= hw - r || ay <= hh - r) return true;
    float dx = ax - (hw - r), dy = ay - (hh - r);
    return dx * dx + dy * dy <= r * r;
}

static bool in_rect(float x, float y, float cx, float cy, float hw, float hh)
{
    return fabsf(x - cx) <= hw && fabsf(y - cy) <= hh;
}

static bool in_circle(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

void sprites_bake_bus(void)
{
    const int N = BUS_SPRITE_PX;
    const float half = N / 2.0f;

    for (int h = 0; h < HEADING_COUNT; h++) {
        float ang = h * (2.0f * (float)M_PI / HEADING_COUNT);
        float ca = cosf(ang), sa = sinf(ang);

        for (int py = 0; py < N; py++) {
            for (int px = 0; px < N; px++) {
                int cnt_body = 0, cnt_win = 0, cnt_light = 0;
                for (int ssy = 0; ssy < 4; ssy++) {
                    for (int ssx = 0; ssx < 4; ssx++) {
                        float wx = px + (ssx + 0.5f) / 4.0f - half;
                        float wy = py + (ssy + 0.5f) / 4.0f - half;
                        // 反向旋转:世界采样点 → 本地(未旋转)坐标系
                        float lx =  wx * ca + wy * sa;
                        float ly = -wx * sa + wy * ca;

                        if (in_circle(lx, ly, LIGHT_X, -LIGHT_Y, LIGHT_R) ||
                            in_circle(lx, ly, LIGHT_X,  LIGHT_Y, LIGHT_R)) {
                            cnt_light++;
                        } else if (in_rect(lx, ly, WIN_CX, 0, WIN_HW, WIN_HH)) {
                            cnt_win++;
                        } else if (in_rounded_rect(lx, ly, BODY_HW, BODY_HH, BODY_R)) {
                            cnt_body++;
                        }
                    }
                }
                int total = cnt_body + cnt_win + cnt_light;
                uint8_t *o = &s_bus_px[h][(py * N + px) * 4];
                if (total == 0) {
                    o[0] = o[1] = o[2] = o[3] = 0;
                    continue;
                }
                for (int c = 0; c < 3; c++) {
                    o[c] = (uint8_t)((BODY_COL[c] * cnt_body + WIN_COL[c] * cnt_win +
                                       LIGHT_COL[c] * cnt_light) / total);
                }
                o[3] = (uint8_t)(total * 255 / 16);
            }
        }

        s_bus_dsc[h].header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_bus_dsc[h].header.cf     = LV_COLOR_FORMAT_ARGB8888;
        s_bus_dsc[h].header.w      = N;
        s_bus_dsc[h].header.h      = N;
        s_bus_dsc[h].header.stride = N * 4;
        s_bus_dsc[h].data_size     = (uint32_t)(N * N * 4);
        s_bus_dsc[h].data          = s_bus_px[h];
    }
}

const lv_image_dsc_t *sprites_bus(int heading)
{
    if (heading < 0) heading = 0;
    if (heading >= HEADING_COUNT) heading = HEADING_COUNT - 1;
    return &s_bus_dsc[heading];
}

int sprites_bus_size(void) { return BUS_SPRITE_PX; }

// ── 乘客造型表(仿 chain_lab PRIZE_LOOK:身体圆 + 特征件 A/B + 双眼 + 嘴)───────────
typedef struct {
    int8_t   aw, ah, ax, ay;    // 特征件 A(耳/冠/呆毛/凸眼)
    int8_t   bw, bh, bx, by;    // 特征件 B(第二只耳朵;0 表示不显示)
    uint32_t part_col;
    int8_t   elx, erx, ey;      // 双眼(圆点)
    int8_t   mw, mh, mx, my;    // 嘴/喙
    uint32_t mouth_col;
    uint32_t body_col;
} passenger_look_t;

#define EYE_COL 0x453A2C

// 体色故意避开三栋房子色(红 0xE6533C / 蓝 0x4FB0D8 / 绿 0x6FBF73):乘客自身体色只是
// 装扮,真正目的地信号是头顶图泡(SPEC.md §决策链"认图泡颜色→开去对应房子");但撞色
// 会让孩子误抓"体色=目的地"这条假线索,故熊(原同红)、蛙(原同绿)改色,鸡/兔本就不撞。
static const passenger_look_t LOOKS[PASSENGER_LOOKS] = {
    // 熊:圆耳 × 2(暖棕,原体色与红房子撞色已改)
    {  6, 6,  0, 6,   6, 6, 12, 6,  0x8B5A2E,  6,12,18,  6,3, 7,22,  0x5C3A1E, 0xA9723F },
    // 小鸡:红冠(单件)+ 橙喙
    {  6, 6,  7, 0,   0, 0,  0, 0,  0xE6533C,  6,12,17,  6,4, 7,20,  0xF08A24, 0xF5C242 },
    // 青蛙:白凸眼 × 2(瞳孔叠在凸眼上;黄绿,原体色与绿房子/树撞色已改)
    {  8, 8,  1, 3,   8, 8, 12, 3,  0xFFFFFF,  4,16, 7,  8,2, 6,23,  0x5C7A2E, 0x8FBF4F },
    // 兔:长耳 × 2
    {  5,11,  2, 0,   5,11, 13, 0,  0xE3BCEB,  6,12,19,  4,2, 8,23,  0x7A4184, 0xC77DD1 },
};

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

// 通用装扮:w<=0 隐藏(乘客身上有些造型只用单件特征,如小鸡的冠)
static void doll_part(lv_obj_t *o, int w, int h, int x, int y, uint32_t col)
{
    if (w <= 0) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_radius(o, (w == h) ? LV_RADIUS_CIRCLE : 3, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void sprites_passenger_create(lv_obj_t *parent, passenger_sprite_t *out)
{
    out->container = plain(parent, PASSENGER_W, PASSENGER_H, 0, 0);
    lv_obj_set_style_bg_opa(out->container, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(out->container, LV_OBJ_FLAG_SCROLLABLE);

    // 特征件先建(垫在身体后,耳朵从脑后长出),身体/眼/嘴后建(盖在上面)
    out->parta = plain(out->container, 8, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);
    out->partb = plain(out->container, 8, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);
    out->body  = plain(out->container, PASSENGER_H - 12, PASSENGER_H - 12, 0xCCCCCC, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(out->body, 3, 12);
    out->eye_l = plain(out->container, 3, 3, EYE_COL, LV_RADIUS_CIRCLE);
    out->eye_r = plain(out->container, 3, 3, EYE_COL, LV_RADIUS_CIRCLE);
    out->mouth = plain(out->container, 6, 3, 0xFFFFFF, 1);

    lv_obj_add_flag(out->container, LV_OBJ_FLAG_HIDDEN);
}

void sprites_passenger_dress(const passenger_sprite_t *p, int look)
{
    if (look < 0 || look >= PASSENGER_LOOKS) return;
    const passenger_look_t *lk = &LOOKS[look];

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(p->body, lv_color_hex(lk->body_col), 0);
    doll_part(p->parta, lk->aw, lk->ah, lk->ax, lk->ay, lk->part_col);
    doll_part(p->partb, lk->bw, lk->bh, lk->bx, lk->by, lk->part_col);
    doll_part(p->eye_l, 3, 3, lk->elx, lk->ey, EYE_COL);
    doll_part(p->eye_r, 3, 3, lk->erx, lk->ey, EYE_COL);
    doll_part(p->mouth, lk->mw, lk->mh, lk->mx, lk->my, lk->mouth_col);
    bsp_display_unlock();
}

// ── 图泡(房形+颜色):方身 + 45°旋转小方块当屋顶,扁平色 ─────────────────────
void sprites_bubble_create(lv_obj_t *parent, bubble_sprite_t *out)
{
    out->container = plain(parent, BUBBLE_W, BUBBLE_H, 0, 0);
    lv_obj_set_style_bg_opa(out->container, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(out->container, LV_OBJ_FLAG_SCROLLABLE);

    out->body = plain(out->container, 12, 9, 0xFFFFFF, 3);
    lv_obj_set_pos(out->body, 1, 7);

    out->roof = plain(out->container, 8, 8, 0xFFFFFF, 2);
    lv_obj_set_pos(out->roof, 3, 0);
    lv_obj_set_style_transform_pivot_x(out->roof, 4, 0);
    lv_obj_set_style_transform_pivot_y(out->roof, 4, 0);
    lv_obj_set_style_transform_angle(out->roof, 450, 0);   // 450 = 45.0°(LVGL 0.1°单位)

    lv_obj_add_flag(out->container, LV_OBJ_FLAG_HIDDEN);
}

void sprites_bubble_set_color(const bubble_sprite_t *b, uint32_t color)
{
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(b->body, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b->roof, lv_color_hex(color), 0);
    bsp_display_unlock();
}
