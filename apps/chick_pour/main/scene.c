#include "scene.h"
#include "flock.h"      // animal_kind_t(ANIMAL_CHICK/ANIMAL_DUCK 索引 homes[])——纯枚举,无 LVGL 依赖
#include "tuning.h"

#include <math.h>
#include "bsp/m5stack_core_2.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "scene";

// 图纸 homes[] 的下标必须与 animal_kind_t 一致(layout.h 不 include flock.h,靠这条卡住)
_Static_assert((int)ANIMAL_CHICK == LAYOUT_HOME_CHICK && (int)ANIMAL_DUCK == LAYOUT_HOME_DUCK,
               "layout.h 的 LAYOUT_HOME_* 与 flock.h 的 animal_kind_t 对不上");

// ── 探头小脸 / 派对对象句柄(每次 scene_redraw 重建,句柄跟着刷新)────────
#define PEEK_MAX      5     // 每家最多 5 张探头小脸(5 鸡 + 5 鸭)
#define PEEK_SZ       7     // 小脸直径(px)
#define EYE_SIGN_COL  0x3A3A38   // 家门口脸招牌/探头小脸的眼睛色(与动物眼睛同色)

static lv_obj_t *s_peek[2][PEEK_MAX];       // [kind][i],hidden 预建,归家时显示
static lv_obj_t *s_house_body, *s_house_roof, *s_house_base;
static lv_obj_t *s_pond_water, *s_pond_sheen, *s_pond_rim_t, *s_pond_rim_b;
static lv_obj_t *s_layout;   // 容器:图纸相关静态件(家×2+灌木×4+探头小脸),整体清空重画

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

// 招牌脸(仿 tools/preview.py draw_face:圆脸 + 两眼 + 喙,按半径 r 等比缩放,通用于
// 任意 SIGN_R,不再用 φ20 时代手调的固定像素偏移——SIGN_R 10→12 这批就是靠这点通用)。
static void draw_face_c(lv_obj_t *parent, float cx, float cy, float r, uint32_t body, uint32_t beak)
{
    circle(parent, (int)cx, (int)cy, (int)r, body);
    float e = fmaxf(1.0f, r * 0.20f);
    float ex0 = cx - r * 0.42f - e, ex1 = cx - r * 0.42f + e;
    float ey0 = cy - r * 0.30f - e, ey1 = cy - r * 0.30f + e;
    box(parent, (int)ex0, (int)ey0, (int)(ex1 - ex0), (int)(ey1 - ey0), EYE_SIGN_COL, (int)e);
    float fx0 = cx + r * 0.42f - e, fx1 = cx + r * 0.42f + e;
    box(parent, (int)fx0, (int)ey0, (int)(fx1 - fx0), (int)(ey1 - ey0), EYE_SIGN_COL, (int)e);
    float bx0 = cx - r * 0.28f, bx1 = cx + r * 0.28f;
    float by0 = cy + r * 0.18f, by1 = cy + r * 0.58f;
    box(parent, (int)bx0, (int)by0, (int)(bx1 - bx0), (int)(by1 - by0), beak, (int)(r * 0.15f));
}

// ── 一个家的完整绘制(鸡窝/池塘逐件镜像同构,只按 face 镜像 x 方向,§2 加强批)────
static void draw_home_layer(animal_kind_t kind, const home_spec_t *hs, rect_t rect, rect_t gate)
{
    int x0 = (int)rect.x0, y0 = (int)rect.y0, x1 = (int)rect.x1, y1 = (int)rect.y1;
    int hw = x1 - x0, hh = y1 - y0;
    int out = (hs->face == HOME_FACE_RIGHT) ? 1 : -1;   // 场地方向
    float door_x = hs->anchor_x;
    int gy0 = (int)gate.y0, gy1 = (int)gate.y1;

    uint32_t body_c, band_t_c, band_b_c, frame_c, hole_c, bar_c, sign_body_c, sign_beak_c;
    if (kind == ANIMAL_CHICK) {
        body_c = 0xE8C79A; band_t_c = 0xD9483A; band_b_c = 0xB4855A;
        frame_c = 0xFFF1CE; hole_c = 0x452F1D; bar_c = 0x8A6238;
        sign_body_c = 0xF7C233; sign_beak_c = 0xF0A030;
    } else {
        body_c = 0x5FB6DC; band_t_c = 0xD9BC7E; band_b_c = 0xD9BC7E;
        frame_c = 0xEADBA8; hole_c = 0x1F3340; bar_c = 0xA98953;
        sign_body_c = 0xE8F0F5; sign_beak_c = 0xF2C14E;   // 冷白(2026-08-10 美术批,见交付说明)
    }

    lv_obj_t *body   = box(s_layout, x0, y0, hw, hh, body_c, 6);
    lv_obj_t *band_t = box(s_layout, x0 - 2, y0, hw + 4, (int)BAND, band_t_c, 6);
    lv_obj_t *band_b = box(s_layout, x0 - 2, y1 - (int)BAND, hw + 4, (int)BAND, band_b_c, 6);
    if (kind == ANIMAL_CHICK) { s_house_roof = band_t; s_house_body = body; s_house_base = band_b; }
    else                      { s_pond_rim_t = band_t; s_pond_water = body; s_pond_rim_b = band_b; }

    if (kind == ANIMAL_DUCK) {
        // 水面高光(避开门框/招牌):🔴 2026-08-10 修正镜像 —— 旧公式固定按"门在 x0 侧"
        // 写(只对 face=LEFT 成立);图纸 B 鸭门 face=RIGHT,不镜像会被门框盖掉大半
        // (实测只剩 ~7px 可见,见交付说明)。镜像后恒定"22px 让开门、6px 让开内墙"。
        float p1 = door_x - out * 22.0f;
        float p2 = door_x - out * ((float)hw - 6.0f);
        int sx0 = (int)fminf(p1, p2), sx1 = (int)fmaxf(p1, p2);
        s_pond_sheen = box(s_layout, sx0, y0 + 17, sx1 - sx0, 6, 0xC5ECF7, 999);
    }

    // 门三件套:门垫(伸进场地)/ 框(嵌墙 DOOR_FRAME_D=17)/ 洞(嵌墙 13),按 out 镜像
    float m_a = door_x, m_b = door_x + out * (GATE_DEPTH + 4.0f);
    int mx0 = (int)fminf(m_a, m_b), mx1 = (int)fmaxf(m_a, m_b);
    box(s_layout, mx0, gy0 + 2, mx1 - mx0, (gy1 - 2) - (gy0 + 2), 0xE6CC92, 4);

    float f_a = door_x, f_b = door_x - out * DOOR_FRAME_D;
    int fx0 = (int)fminf(f_a, f_b), fx1 = (int)fmaxf(f_a, f_b);
    box(s_layout, fx0, gy0 - 2, fx1 - fx0, (gy1 + 2) - (gy0 - 2), frame_c, 8);

    float h_a = door_x, h_b = door_x - out * 13.0f;
    int hx0 = (int)fminf(h_a, h_b), hx1 = (int)fmaxf(h_a, h_b);
    box(s_layout, hx0, gy0, hx1 - hx0, gy1 - gy0, hole_c, 8);

    // 天窗条 / 水草条(探头小脸从这儿冒):恒在沿边轴的"顶"侧,与 face 无关(面朝左右
    // 只改变进深轴朝向,不改变沿边轴的上下)。
    box(s_layout, x0 + 2, y0 + 2, hw - 4, 9, bar_c, 3);
    uint32_t peek_col = (kind == ANIMAL_CHICK) ? 0xF7C233 : 0xE8F0F5;
    for (int i = 0; i < PEEK_MAX; i++) {
        s_peek[kind][i] = circle(s_layout, x0 + 7 + i * 9, y0 + 6, PEEK_SZ / 2, peek_col);
        lv_obj_add_flag(s_peek[kind][i], LV_OBJ_FLAG_HIDDEN);
    }

    // 招牌脸:门面的另一侧(门占了朝场地那面墙),垂直对齐门洞中线;46 进深下 SIGN_R=12
    // 是硬上限(见 scene.h 注释),这里 hw 恒等于 HOUSE_W/POND_W=46,不随图纸变。
    float sx = door_x - out * ((float)hw - SIGN_R - 5.0f);
    draw_face_c(s_layout, sx, hs->cy, SIGN_R, sign_body_c, sign_beak_c);
}

// 整体重画图纸相关静态件(家×2 + 灌木×4 + 探头小脸),仅在 scene_apply_blueprint 内调,
// 合 §6.1"整屏重绘只允许进关/换场景"。
static void scene_redraw(const blueprint_t *bp)
{
    bsp_display_lock(0);
    lv_obj_clean(s_layout);   // LVGL 删子对象时自动清掉挂在它们身上的 lv_anim,无需手动 delete

    draw_home_layer(ANIMAL_CHICK, &bp->homes[ANIMAL_CHICK], HOUSE_RECT, HOUSE_GATE);
    draw_home_layer(ANIMAL_DUCK,  &bp->homes[ANIMAL_DUCK],  POND_RECT,  POND_GATE);

    // 四角灌木:深绿主体 + 偏中心一颗浅绿高光(2026-08-10 降对比,碰撞半径不动只降视觉权重)
    for (int i = 0; i < 4; i++) {
        circle(s_layout, (int)CORNER_BUSH[i].x, (int)CORNER_BUSH[i].y, (int)CORNER_BUSH[i].r, 0x7CB86F);
    }
    for (int i = 0; i < 4; i++) {
        int hlx = (int)CORNER_BUSH[i].x + (CORNER_BUSH[i].x < PLAY_W / 2 ? 8 : -14);
        int hly = (int)CORNER_BUSH[i].y + (CORNER_BUSH[i].y < PLAY_H / 2 ? 8 : -14);
        circle(s_layout, hlx, hly, 12, 0x8EC980);
    }

    bsp_display_unlock();
}

int scene_blueprint_count(void)   { return layout_count(); }
int scene_current_blueprint(void) { return layout_current(); }

void scene_apply_blueprint(int idx)
{
    const blueprint_t *bp = layout_get(layout_apply(idx));   // 校验+提交几何(失败回退 A)
    scene_redraw(bp);
    ESP_LOGI(TAG, "图纸切换 → [%s]", bp->name);
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

    // 栅栏木色打底铺满整屏,再在内侧画一块缩进 FENCE_THICK 的草地——剩下露出来的外圈
    // 木色边框就是"四周木栅栏",省掉画四条独立边框的麻烦。图纸无关,恒定,只画一次
    // (PLAY_BOUNDS/FENCE_THICK 不随图纸变,不用跟着 scene_apply_blueprint 重画)。
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x8A5A3C), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    box(scr, (int)FENCE_THICK, (int)FENCE_THICK,
        (int)(PLAY_W - 2 * FENCE_THICK), (int)(PLAY_H - 2 * FENCE_THICK), 0x9ED97A, 6);

    // 图纸相关静态件的容器:透明、铺满屏,后续每次图纸切换只 clean 它、不碰上面的
    // 栅栏/草地(它是 scr 的子对象,建在栅栏之后、动物精灵[critters_init]之前,
    // z-order 天然夹在两者中间,不受 lv_obj_clean(s_layout) 反复调用影响)。
    s_layout = lv_obj_create(scr);
    lv_obj_remove_style_all(s_layout);
    lv_obj_set_size(s_layout, (int)PLAY_W, (int)PLAY_H);
    lv_obj_set_pos(s_layout, 0, 0);
    lv_obj_set_style_bg_opa(s_layout, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_layout, LV_OBJ_FLAG_SCROLLABLE);

    bsp_display_unlock();

    // 🔴 开机把三张图纸全跑一遍校验并打一行日志。校验器一旦自己写错(2026-08-13 就
    // 发生过:flood fill 填表循环提前退出 → 三张全判失败 → 每次都静默回退图纸 A),
    // 屏幕上的表现与"设计如此"一模一样,只有这行日志能当场戳穿。
    layout_selftest();

    scene_apply_blueprint(0);   // 开机恒图纸 A(基准图纸);之后每轮派对换下一张
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
