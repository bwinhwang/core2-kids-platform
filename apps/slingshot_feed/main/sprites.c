// slingshot_feed —— 精灵创建 + 装扮实现(SPEC.md §5/§7,仿 busy_bus/chain_lab 的
// plain()/doll_part() 组合式打法:全部扁平不透明色块,子对象自动裁剪,不做任何运行时
// 旋转贴图或 alpha 混合)。
#include "sprites.h"

#include "bsp/m5stack_core_2.h"

#include "tuning.h"

#define EYE_COL   0x453A2C
#define EYE_SZ    ANIMAL_EYE_SZ   // 单一来源在 tuning.h(render_all 眨眼/锁定放大共用)
#define CHEEK_COL 0xF2A0B8
#define CHEEK_SZ  5

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

// ── 皮筋(弹弓兜)────────────────────────────────────────────────────────
#define POUCH_SZ 8
#define POUCH_COL 0xD8A24A
#define BAND_DOT_SZ 4
#define BAND_DOT_COL 0xB07A2E

void sprites_band_create(lv_obj_t *parent, band_sprite_t *out)
{
    out->pouch = plain(parent, POUCH_SZ, POUCH_SZ, POUCH_COL, LV_RADIUS_CIRCLE);
    for (int i = 0; i < BAND_DOTS_MAX; i++) {
        out->dot_l[i] = plain(parent, BAND_DOT_SZ, BAND_DOT_SZ, BAND_DOT_COL, LV_RADIUS_CIRCLE);
        out->dot_r[i] = plain(parent, BAND_DOT_SZ, BAND_DOT_SZ, BAND_DOT_COL, LV_RADIUS_CIRCLE);
    }
}

// ── 果子(色相着色)──────────────────────────────────────────────────────
#define FRUIT_LEAF_SZ  5
#define FRUIT_LEAF_COL 0x4E9052

void sprites_fruit_create(lv_obj_t *parent, fruit_sprite_t *out)
{
    out->body = plain(parent, FRUIT_SZ, FRUIT_SZ, 0xE6533C, LV_RADIUS_CIRCLE);
    out->leaf = plain(parent, FRUIT_LEAF_SZ, FRUIT_LEAF_SZ - 2, FRUIT_LEAF_COL, 2);
    lv_obj_add_flag(out->body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(out->leaf, LV_OBJ_FLAG_HIDDEN);
}

void sprites_fruit_set_color(const fruit_sprite_t *f, uint8_t r, uint8_t g, uint8_t b)
{
    bsp_display_lock(0);
    lv_obj_set_style_bg_color(f->body, lv_color_make(r, g, b), 0);
    bsp_display_unlock();
}

// ── 动物造型表(改进 C:每种独立体型/五官/特征件/配色 —— 不再共享同一套眼嘴,读起来
// 是不同的动物而非"换色的同一只")。特征件 A/B 垫在身体后(只能露出身体轮廓外的耳/冠/斑)。
typedef struct {
    uint32_t body_col;
    uint8_t  body_sz;               // 体型(物种差异)
    uint8_t  elx, erx, ey;          // 眼睛基准(container 局部坐标)
    uint8_t  mx, my, mw;            // 嘴基准 + 宽
    uint32_t mouth_col;
    int8_t   aw, ah, ax, ay;        // 特征件 A(耳/冠/斑)
    int8_t   bw, bh, bx, by;        // 特征件 B(0=不显示)
    uint32_t part_col;
} animal_look_t;

static const animal_look_t LOOKS[ANIMAL_SPECIES] = {
    // 熊:暖棕,圆耳 × 2
    { 0xC98B54, 26,  9,21,19,  12,30,10, 0x5C3A1E,   7,7, 3,3,   7,7,24,3,  0xA9723F },
    // 小鸡:黄身,红冠(单件),橙宽喙,眼距近
    { 0xF5C242, 24, 10,19,18,  11,28,12, 0xF08A24,   8,7,13,1,   0,0, 0,0,  0xE6533C },
    // 青蛙:绿身,大眼分得开,宽嘴,白斑 × 2(顶角)
    { 0x8FBF4F, 28,  7,22,16,   9,31,16, 0x2E5C2E,   9,9, 2,1,   9,9,23,1,  0xFFFFFF },
    // 兔:米白身,高耳 × 2,粉嘴
    { 0xE8DFD0, 23, 10,19,20,  13,29, 8, 0xE38FA0,   5,13,9,0,   5,13,20,0, 0xFAF4EA },
    // 猫:灰身,小尖耳 × 2(近似小方),粉嘴
    { 0x9AA7B0, 25,  9,20,19,  13,29, 8, 0xD98FA0,   7,6, 4,3,   7,6,23,3,  0x7E8B95 },
};

static void doll_part(lv_obj_t *o, int w, int h, int x, int y, uint32_t col)
{
    if (w <= 0) { lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    lv_obj_set_style_radius(o, (w == h) ? LV_RADIUS_CIRCLE : 3, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void sprites_animal_create(lv_obj_t *parent, animal_sprite_t *out)
{
    out->container = plain(parent, ANIMAL_W, ANIMAL_H, 0, 0);
    lv_obj_set_style_bg_opa(out->container, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(out->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(out->container, ANIMAL_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(out->container, ANIMAL_H / 2, 0);

    // 特征件先建(垫在身体后),身体/眼/嘴/腮红后建(盖在上面);尺寸/位置/色由 dress 按物种落
    out->parta = plain(out->container, 8, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);
    out->partb = plain(out->container, 8, 8, 0xFFFFFF, LV_RADIUS_CIRCLE);
    out->body  = plain(out->container, ANIMAL_BODY_SZ, ANIMAL_BODY_SZ, 0xCCCCCC, LV_RADIUS_CIRCLE);
    out->eye_l = plain(out->container, EYE_SZ, EYE_SZ, EYE_COL, LV_RADIUS_CIRCLE);
    out->eye_r = plain(out->container, EYE_SZ, EYE_SZ, EYE_COL, LV_RADIUS_CIRCLE);
    out->mouth = plain(out->container, 10, MOUTH_OPEN_H, 0xFFFFFF, 2);
    out->cheek_l = plain(out->container, CHEEK_SZ, CHEEK_SZ, CHEEK_COL, LV_RADIUS_CIRCLE);
    out->cheek_r = plain(out->container, CHEEK_SZ, CHEEK_SZ, CHEEK_COL, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(out->cheek_l, 3, 25);
    lv_obj_set_pos(out->cheek_r, ANIMAL_BODY_SZ, 25);
    lv_obj_add_flag(out->cheek_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(out->cheek_r, LV_OBJ_FLAG_HIDDEN);

    out->species = 0;
    lv_obj_add_flag(out->container, LV_OBJ_FLAG_HIDDEN);
}

void sprites_animal_dress(animal_sprite_t *a, int species)
{
    if (species < 0 || species >= ANIMAL_SPECIES) return;
    const animal_look_t *lk = &LOOKS[species];
    a->species = species;

    int body_x = (ANIMAL_W - lk->body_sz) / 2;
    int body_y = 12;

    bsp_display_lock(0);
    lv_obj_set_size(a->body, lk->body_sz, lk->body_sz);
    lv_obj_set_pos(a->body, body_x, body_y);
    lv_obj_set_style_bg_color(a->body, lv_color_hex(lk->body_col), 0);
    doll_part(a->parta, lk->aw, lk->ah, lk->ax, lk->ay, lk->part_col);
    doll_part(a->partb, lk->bw, lk->bh, lk->bx, lk->by, lk->part_col);

    a->eye_lx = lk->elx; a->eye_ly = lk->ey;
    a->eye_rx = lk->erx; a->eye_ry = lk->ey;
    lv_obj_set_size(a->eye_l, EYE_SZ, EYE_SZ);
    lv_obj_set_size(a->eye_r, EYE_SZ, EYE_SZ);
    lv_obj_set_pos(a->eye_l, lk->elx, lk->ey);
    lv_obj_set_pos(a->eye_r, lk->erx, lk->ey);

    a->mouth_x = lk->mx; a->mouth_y = lk->my; a->mouth_w = lk->mw;
    lv_obj_set_size(a->mouth, lk->mw, MOUTH_OPEN_H);
    lv_obj_set_pos(a->mouth, lk->mx, lk->my);
    lv_obj_set_style_bg_color(a->mouth, lv_color_hex(lk->mouth_col), 0);

    lv_obj_remove_flag(a->container, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

void sprites_animal_mouth_open(animal_sprite_t *a, bool open)
{
    bsp_display_lock(0);
    lv_obj_set_size(a->mouth, a->mouth_w, open ? MOUTH_OPEN_H : MOUTH_CLOSED_H);
    lv_obj_set_pos(a->mouth, a->mouth_x, a->mouth_y);
    bsp_display_unlock();
}

uint32_t sprites_species_body_color(int species)
{
    if (species < 0 || species >= ANIMAL_SPECIES) return 0xCCCCCC;
    return LOOKS[species].body_col;
}

void sprites_animal_cheeks(const animal_sprite_t *a, bool show)
{
    bsp_display_lock(0);
    if (show) {
        lv_obj_remove_flag(a->cheek_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->cheek_r, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(a->cheek_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(a->cheek_r, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}
