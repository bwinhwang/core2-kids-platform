// sprites —— fish_pond 精灵构件实现(SPEC.md §6.6;手法见 sprites.h 顶注)
#include "sprites.h"

#include "tuning.h"

lv_obj_t *fpond_box(lv_obj_t *parent, int w, int h, uint32_t color, int radius)
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

// x 几何按"面向右"计算;面向左时整体水平镜像(前后一致,复用同一套百分比)
static int mirrored_x(int x, int part_w, int w, fish_face_t face)
{
    return (face == FISH_FACE_RIGHT) ? x : (w - x - part_w);
}

void fpond_fish_sprite_create(fpond_fish_sprite_t *s, lv_obj_t *parent,
                               int w, int h, uint32_t body_col, uint32_t belly_col)
{
    s->w = w;
    s->h = h;

    s->root = fpond_box(parent, w, h, 0, 0);
    lv_obj_set_style_bg_opa(s->root, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s->root, LV_OBJ_FLAG_SCROLLABLE);

    s->tail = fpond_box(s->root, w / 4, h / 2, body_col, LV_RADIUS_CIRCLE);
    s->body = fpond_box(s->root, w, h, body_col, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s->body, 0, 0);

    // 下腹浅色斑:静态装饰,create 时摆一次,不随状态/朝向变(前后对称的位置,无需镜像)
    int belly_w = w * 46 / 100, belly_h = h * 34 / 100;
    s->belly = fpond_box(s->root, belly_w, belly_h, belly_col, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s->belly, (w - belly_w) / 2, h * 58 / 100);

    s->eye_l = fpond_box(s->root, 6, 6, COL_EYE, LV_RADIUS_CIRCLE);
    s->eye_r = fpond_box(s->root, 6, 6, COL_EYE, LV_RADIUS_CIRCLE);
    s->mouth = fpond_box(s->root, 6, 6, COL_MOUTH, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(s->mouth, LV_OBJ_FLAG_HIDDEN);

    fpond_fish_sprite_set_state(s, FISH_VIS_PATROL_A, FISH_FACE_RIGHT);
    lv_obj_add_flag(s->root, LV_OBJ_FLAG_HIDDEN);
}

void fpond_fish_sprite_set_state(fpond_fish_sprite_t *s, fish_vis_t vis, fish_face_t face)
{
    int w = s->w, h = s->h;

    // 尾鳍:游动 A/B 两帧上下轻摆(氛围档,~2fps)
    int tail_w = w * 22 / 100, tail_h = h * 55 / 100;
    int tail_x = mirrored_x(-tail_w / 2, tail_w, w, face);
    int tail_y = (h - tail_h) / 2 + ((vis == FISH_VIS_PATROL_B) ? h * 6 / 100 : -(h * 6 / 100));
    lv_obj_set_size(s->tail, tail_w, tail_h);
    lv_obj_set_pos(s->tail, tail_x, tail_y);

    // 双眼:NOTICE/APPROACH_BIG/CHOMP 放大("瞪眼")
    bool wide_eye = (vis == FISH_VIS_NOTICE || vis == FISH_VIS_APPROACH_BIG || vis == FISH_VIS_CHOMP);
    int eye_sz = wide_eye ? (h * 15 / 100) : (h * 9 / 100);
    lv_obj_set_size(s->eye_l, eye_sz, eye_sz);
    lv_obj_set_pos(s->eye_l, mirrored_x(w * 60 / 100, eye_sz, w, face), h * 16 / 100);
    lv_obj_set_size(s->eye_r, eye_sz, eye_sz);
    lv_obj_set_pos(s->eye_r, mirrored_x(w * 70 / 100, eye_sz, w, face), h * 30 / 100);

    // 嘴:两级张开(APPROACH_SMALL/BIG)+ 咬合(CHOMP,HOOKED 沿用);其余隐藏
    int mouth_w = 0, mouth_h = 0;
    switch (vis) {
    case FISH_VIS_APPROACH_SMALL: mouth_w = w * 10 / 100; mouth_h = h * 12 / 100; break;
    case FISH_VIS_APPROACH_BIG:   mouth_w = w * 16 / 100; mouth_h = h * 20 / 100; break;
    case FISH_VIS_CHOMP:          mouth_w = w * 22 / 100; mouth_h = h * 30 / 100; break;
    default: break;
    }
    if (mouth_w > 0 && mouth_h > 0) {
        lv_obj_set_size(s->mouth, mouth_w, mouth_h);
        lv_obj_set_pos(s->mouth, mirrored_x(w * 74 / 100, mouth_w, w, face), h * 46 / 100);
        lv_obj_remove_flag(s->mouth, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s->mouth, LV_OBJ_FLAG_HIDDEN);
    }
}

void fpond_fish_sprite_set_pos(fpond_fish_sprite_t *s, int cx, int cy)
{
    lv_obj_set_pos(s->root, cx - s->w / 2, cy - s->h / 2);
}

void fpond_fish_sprite_show(fpond_fish_sprite_t *s, bool show)
{
    if (show) lv_obj_remove_flag(s->root, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(s->root, LV_OBJ_FLAG_HIDDEN);
}

// ── 船(96×40:船体 + 舱房,静态外形,paddle 帧只做小幅上下晃)────────────────
void fpond_boat_sprite_create(fpond_boat_sprite_t *s, lv_obj_t *parent)
{
    s->hull  = fpond_box(parent, BOAT_W, BOAT_H * 6 / 10, COL_BOAT_HULL, 10);
    s->cabin = fpond_box(parent, BOAT_W * 4 / 10, BOAT_H * 5 / 10, COL_BOAT_CABIN, 6);
}

void fpond_boat_sprite_set_pos(fpond_boat_sprite_t *s, int cx, int top_y, bool paddle)
{
    int bob = paddle ? 2 : 0;
    int hull_y = top_y + BOAT_H * 4 / 10;
    lv_obj_set_pos(s->hull, cx - BOAT_W / 2, hull_y);
    lv_obj_set_pos(s->cabin, cx - (BOAT_W * 4 / 10) / 2, top_y + bob);
}

// ── 饵(浆果,64×64:主体 + 高光)───────────────────────────────────────────
void fpond_bait_sprite_create(fpond_bait_sprite_t *s, lv_obj_t *parent)
{
    s->body      = fpond_box(parent, BAIT_SIZE, BAIT_SIZE, COL_BAIT, LV_RADIUS_CIRCLE);
    s->highlight = fpond_box(parent, BAIT_SIZE / 4, BAIT_SIZE / 4, COL_BAIT_HL, LV_RADIUS_CIRCLE);
}

void fpond_bait_sprite_set_pos(fpond_bait_sprite_t *s, int cx, int cy)
{
    lv_obj_set_pos(s->body, cx - BAIT_SIZE / 2, cy - BAIT_SIZE / 2);
    lv_obj_set_pos(s->highlight, cx - BAIT_SIZE / 2 + BAIT_SIZE / 6,
                   cy - BAIT_SIZE / 2 + BAIT_SIZE / 6);
}

lv_obj_t *fpond_dot_create(lv_obj_t *parent, int d, uint32_t color)
{
    lv_obj_t *o = fpond_box(parent, d, d, color, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}
