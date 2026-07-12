#include "scene.h"
#include "tuning.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"
#include "esp_random.h"

// 版面几何:栅栏内墙 = 动物活动边界;鸡窝/池塘紧贴栅栏内侧从边缘中段探进场地;
// 四角灌木圆心内缩 BUSH_INSET,半径 CORNER_BUSH_R 较大,允许圆的一部分探出屏幕外
// (只是让角落变斜坡,不需要整圆都可见)。
const rect_t PLAY_BOUNDS = {
    FENCE_THICK + ANIMAL_R, FENCE_THICK + ANIMAL_R,
    PLAY_W - FENCE_THICK - ANIMAL_R, PLAY_H - FENCE_THICK - ANIMAL_R,
};
const rect_t HOUSE_RECT = {
    FENCE_THICK, PLAY_H / 2 - HOUSE_H / 2,
    FENCE_THICK + HOUSE_W, PLAY_H / 2 + HOUSE_H / 2,
};
const rect_t POND_RECT = {
    PLAY_W - FENCE_THICK - POND_W, PLAY_H / 2 - POND_H / 2,
    PLAY_W - FENCE_THICK, PLAY_H / 2 + POND_H / 2,
};

// 门区(SPEC §5.2):高 GATE_W=44(≈动物直径×2.75,容差给够),居中于家的墙面;
// 向场内伸 GATE_DEPTH=10 > ANIMAL_R=8 → 动物中心必然先进门区、判定先于家墙碰撞触发;
// 向墙内嵌 6px:软分离偶尔把动物横着挤到墙面线内侧时仍在门区豁免范围里,不会被墙推着乱跳。
const rect_t HOUSE_GATE = {
    FENCE_THICK + HOUSE_W - 6, PLAY_H / 2 - GATE_W / 2.0f,
    FENCE_THICK + HOUSE_W + GATE_DEPTH, PLAY_H / 2 + GATE_W / 2.0f,
};
const rect_t POND_GATE = {
    PLAY_W - FENCE_THICK - POND_W - GATE_DEPTH, PLAY_H / 2 - GATE_W / 2.0f,
    PLAY_W - FENCE_THICK - POND_W + 6, PLAY_H / 2 + GATE_W / 2.0f,
};

const circ_t CORNER_BUSH[4] = {
    { BUSH_INSET,            BUSH_INSET,            CORNER_BUSH_R },
    { PLAY_W - BUSH_INSET,   BUSH_INSET,            CORNER_BUSH_R },
    { BUSH_INSET,            PLAY_H - BUSH_INSET,   CORNER_BUSH_R },
    { PLAY_W - BUSH_INSET,   PLAY_H - BUSH_INSET,   CORNER_BUSH_R },
};

// ── 探头小脸 / 派对对象句柄 ───────────────────────────────────────────
#define PEEK_MAX      5     // 每家最多 5 张探头小脸(5 鸡 + 5 鸭)
#define PEEK_SZ       7     // 小脸直径(px)
#define EYE_SIGN_COL  0x3A3A38   // 家门口脸招牌的眼睛色(与动物眼睛同色)

static lv_obj_t *s_peek[2][PEEK_MAX];       // [kind][i],hidden 预建,归家时显示
static lv_obj_t *s_house_body, *s_house_roof, *s_house_base;
static lv_obj_t *s_pond_water, *s_pond_sheen, *s_pond_rim_t, *s_pond_rim_b;

// ── 绘制小工具(仿 tilt_maze render.c 的 make_box)────────────────────
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *circle(lv_obj_t *parent, int cx, int cy, int r, uint32_t color)
{
    return box(parent, cx - r, cy - r, r * 2, r * 2, color, LV_RADIUS_CIRCLE);
}

// ── 动画回调(仿 tilt_maze render.c)─────────────────────────────────
static void cb_ty(void *o, int32_t v)  { lv_obj_set_style_translate_y((lv_obj_t *)o, v, 0); }
static void cb_y(void *o, int32_t v)   { lv_obj_set_y((lv_obj_t *)o, v); }
static void cb_delete(lv_anim_t *a)    { lv_obj_delete((lv_obj_t *)a->var); }

void scene_init(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // 栅栏木色打底铺满整屏,再在内侧画一块缩进 FENCE_THICK 的草地——
    // 剩下露出来的外圈木色边框就是"四周木栅栏",省掉画四条独立边框的麻烦。
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x8A5A3C), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    box(scr, (int)FENCE_THICK, (int)FENCE_THICK,
        (int)(PLAY_W - 2 * FENCE_THICK), (int)(PLAY_H - 2 * FENCE_THICK), 0x9ED97A, 6);

    // 鸡窝(左边缘中段)—— 2026-07-12 实机反馈"家不够明显"加强批:
    // 木屋身 + 红屋顶 + 大拱门(奶油门框 + 深棕门洞,跨骑 HOUSE_GATE 全高,"大黑洞"
    // 一眼可读)+ 门口沙色门垫(伸进场地,与池塘浅滩对仗的"从这儿进"地面邀请)+
    // 屋身大号小鸡脸招牌(不识字 → 语义靠住户的脸,§2)。与 tilt_maze 配色刻意区分。
    int hx0 = (int)HOUSE_RECT.x0, hy0 = (int)HOUSE_RECT.y0;
    int hw  = (int)(HOUSE_RECT.x1 - HOUSE_RECT.x0), hh = (int)(HOUSE_RECT.y1 - HOUSE_RECT.y0);
    // 立面对称三段式(实机反馈"门上下两边不一致"修正):屋顶带 13 + 墙身 44(=GATE_W,
    // 与 HOUSE_GATE 严格同高)+ 底座带 13 = HOUSE_H 70。门洞占满墙身段、门框上下各只出
    // 2px 压在屋顶/底座上 —— 上下肩膀天然等宽,门再也不"戳屋顶、剩下摆"。
    s_house_body = box(scr, hx0, hy0, hw, hh, 0xE8C79A, 6);
    s_house_roof = box(scr, hx0 - 2, hy0, hw + 4, 13, 0xD9483A, 6);
    s_house_base = box(scr, hx0 - 2, hy0 + hh - 13, hw + 4, 13, 0xB4855A, 6);
    box(scr, (int)HOUSE_RECT.x1, (int)HOUSE_GATE.y0 + 2,
        (int)GATE_DEPTH + 4, (int)GATE_W - 4, 0xE6CC92, 4);                       // 门垫
    box(scr, (int)HOUSE_RECT.x1 - 17, (int)HOUSE_GATE.y0 - 2, 17, (int)GATE_W + 4, 0xFFF1CE, 8);  // 门框
    box(scr, (int)HOUSE_RECT.x1 - 13, (int)HOUSE_GATE.y0, 13, (int)GATE_W, 0x452F1D, 8);          // 门洞
    // 小鸡脸招牌(与场上小鸡同款配色,眼/喙做 sign 的子对象),垂直对齐门洞中线
    lv_obj_t *sign = circle(scr, hx0 + 15, (int)(PLAY_H / 2), 10, 0xF7C233);
    box(sign, 5, 6, 3, 3, EYE_SIGN_COL, LV_RADIUS_CIRCLE);
    box(sign, 12, 6, 3, 3, EYE_SIGN_COL, LV_RADIUS_CIRCLE);
    box(sign, 7, 12, 6, 4, 0xF0A030, 2);

    // 池塘(右边缘中段)—— 与鸡窝**镜像同构**(实机反馈"两边不一致"二次修正):
    // 同一套三段式(沙沿带 13 + 水面 44 = POND_GATE 全高 + 沙沿带 13)、同一套入口
    // 三件套(门垫 + 框 + 深色洞口),逐件对应鸡窝的(门垫 + 奶油木框 + 深棕门洞),
    // 只换材质:木屋 → 沙沿水塘。孩子看到的是"左右两栋对称的家",只是住户/颜色不同。
    int px0 = (int)POND_RECT.x0, py0 = (int)POND_RECT.y0;
    int pw  = (int)(POND_RECT.x1 - POND_RECT.x0), ph = (int)(POND_RECT.y1 - POND_RECT.y0);
    s_pond_water = box(scr, px0, py0, pw, ph, 0x5FB6DC, 6);                        // 全高水体
    s_pond_rim_t = box(scr, px0 - 2, py0, pw + 4, 13, 0xD9BC7E, 6);                // 上沙沿带
    s_pond_rim_b = box(scr, px0 - 2, py0 + ph - 13, pw + 4, 13, 0xD9BC7E, 6);      // 下沙沿带
    box(scr, (int)(POND_RECT.x0 - GATE_DEPTH - 4), (int)POND_GATE.y0 + 2,
        (int)GATE_DEPTH + 4, (int)GATE_W - 4, 0xE6CC92, 4);                        // 门垫(同色镜像)
    box(scr, px0, (int)POND_GATE.y0 - 2, 17, (int)GATE_W + 4, 0xEADBA8, 8);        // 沙框
    box(scr, px0, (int)POND_GATE.y0, 13, (int)GATE_W, 0x2F6E92, 8);                // 深水缺口
    s_pond_sheen = box(scr, px0 + 22, py0 + 17, pw - 28, 6, 0xC5ECF7, 999);        // 水面高光(避开框/招牌)
    // 小鸭脸招牌:镜像鸡窝招牌(右侧居中,垂直对齐缺口中线)
    lv_obj_t *dsign = circle(scr, px0 + pw - 15, (int)(PLAY_H / 2), 10, 0xF2F2ED);
    box(dsign, 5, 6, 3, 3, EYE_SIGN_COL, LV_RADIUS_CIRCLE);
    box(dsign, 12, 6, 3, 3, EYE_SIGN_COL, LV_RADIUS_CIRCLE);
    box(dsign, 7, 12, 6, 4, 0xF2C14E, 2);

    // 四角灌木:深绿主体 + 偏中心一颗浅绿高光,烘进静态层(付一次,不每帧算)。
    for (int i = 0; i < 4; i++) {
        circle(scr, (int)CORNER_BUSH[i].x, (int)CORNER_BUSH[i].y, (int)CORNER_BUSH[i].r, 0x4E8F4A);
    }
    for (int i = 0; i < 4; i++) {
        int hlx = (int)CORNER_BUSH[i].x + (CORNER_BUSH[i].x < PLAY_W / 2 ? 8 : -14);
        int hly = (int)CORNER_BUSH[i].y + (CORNER_BUSH[i].y < PLAY_H / 2 ? 8 : -14);
        circle(scr, hlx, hly, 12, 0x6FB86A);
    }

    // 探头小脸(SPEC §5.3 计数显示):预建 hidden,归家时显示前 n 个,改一次不逐帧。
    // 两家镜像同位:各自顶带里一条深色横条(鸡窝天窗 / 池塘水草条),小脑袋从条里
    // 冒出来。每家 5 个 7px 圆一排,间距 9px,y 完全一致。
    box(scr, hx0 + 2, hy0 + 2, hw - 4, 9, 0x8A6238, 3);     // 鸡窝天窗条(屋顶带内)
    box(scr, px0 + 2, py0 + 2, pw - 4, 9, 0xA98953, 3);     // 池塘水草条(沙沿带内,镜像)
    for (int i = 0; i < PEEK_MAX; i++) {
        s_peek[0][i] = circle(scr, hx0 + 7 + i * 9, hy0 + 6, PEEK_SZ / 2, 0xF7C233);    // 鸡头
        lv_obj_add_flag(s_peek[0][i], LV_OBJ_FLAG_HIDDEN);
        s_peek[1][i] = circle(scr, px0 + 7 + i * 9, py0 + 6, PEEK_SZ / 2, 0xF2F2ED);    // 鸭头
        lv_obj_add_flag(s_peek[1][i], LV_OBJ_FLAG_HIDDEN);
    }

    bsp_display_unlock();
}

void scene_set_home_count(int kind, int n)
{
    if (kind < 0 || kind > 1) return;
    if (n < 0) n = 0;
    if (n > PEEK_MAX) n = PEEK_MAX;

    bsp_display_lock(0);
    for (int i = 0; i < PEEK_MAX; i++) {
        if (i < n) lv_obj_remove_flag(s_peek[kind][i], LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag(s_peek[kind][i], LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

// 派对两家弹跳:translate_y 0→-8 往返,重复 5 次 ≈ 3s(≈PARTY_HOLD_MS),
// 自然停回 0;动 translate 不动 y,不破坏对象的布局坐标。
static void bounce_one(lv_obj_t *o, int delay_ms)
{
    if (!o) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb_ty);
    lv_anim_set_values(&a, 0, -8);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_reverse_duration(&a, 300);
    lv_anim_set_repeat_count(&a, 5);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_start(&a);   // 同 var+cb 会替换旧动画
}

void scene_party_bounce(void)
{
    bsp_display_lock(0);
    bounce_one(s_house_roof, 0);       // 顶带先跳、墙身/底带跟拍 —— 两家同一套错拍
    bounce_one(s_house_body, 60);
    bounce_one(s_house_base, 60);
    bounce_one(s_pond_rim_t, 0);
    bounce_one(s_pond_water, 60);
    bounce_one(s_pond_rim_b, 60);
    bounce_one(s_pond_sheen, 60);
    bsp_display_unlock();
}

// 限量彩纸(SPEC §6/§7:CONFETTI_N ≤8 片,§6.5 庆祝档):仿 tilt_maze
// render_win_celebrate,纯色小块从顶部飘落到底后自删,零 alpha、柔和非频闪。
void scene_confetti(void)
{
    static const uint32_t cols[5] = { 0xFF8FB0, 0xFFD23F, 0x7FD0C0, 0x9FD06A, 0xFF9E80 };
    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    for (int i = 0; i < CONFETTI_N; i++) {
        int x  = 14 + (esp_random() % 292);
        int sz = 8 + (esp_random() % 7);
        lv_obj_t *d = box(scr, x, -14, sz, sz, cols[i % 5], 999);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, d);
        lv_anim_set_exec_cb(&a, cb_y);
        lv_anim_set_values(&a, -14, (int)PLAY_H + 14);
        lv_anim_set_duration(&a, 900 + (esp_random() % 500));
        lv_anim_set_delay(&a, i * 60);
        lv_anim_set_completed_cb(&a, cb_delete);
        lv_anim_start(&a);
    }
    bsp_display_unlock();
}
