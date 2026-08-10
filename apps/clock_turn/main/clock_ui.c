#include "clock_ui.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "bsp/m5stack_core_2.h"

#include "clock_model.h"
#include "tuning.h"

static const char *TAG = "clock_ui";

// ── 动态层对象(两针 = 各一条"描边线"+"本体线",中心帽,共 5 个 lv_obj)────────
//
// 🔴 关键设计(根 CLAUDE.md §11 平台坑 + §6.2 帧预算):
// lv_line 的自身尺寸由 LVGL 按"points 数组里坐标的 max(x)/max(y)"自动算(LV_SIZE_CONTENT),
// 不是"按 obj 位置偏移再画"。如果直接把屏幕绝对坐标塞进 points(比如中心 101,120),
// 算出来的包围盒会是 (0,0)-(101,120) 这种"从屏幕左上角铺到指针尖"的大方块 —— 脏矩形直接
// 从两三千像素膨胀成几万像素,SPEC §6.2 的预算表当场作废。
// 正确做法:points 用"以本段线两端点分量最小值为原点"的**相对坐标**,再把 obj 本身
// lv_obj_set_pos 到那个原点——这样 obj 的自适应尺寸恰好只等于线段包围盒,配合 LVGL 的
// lv_obj_move_to()(挪位置时自动"先invalidate旧区域、再invalidate新区域")拿到真正的
// 小脏矩形。两针都挂在一个固定的 clock_group 容器下(容器覆盖整个钟面、开
// LV_OBJ_FLAG_OVERFLOW_VISIBLE 兜底),避免"父容器裁子对象"那个坑(CLAUDE.md §11)。
typedef struct {
    lv_obj_t *edge;                    // 描边(钟面色,更宽,在下层)
    lv_obj_t *line;                    // 本体(暖黑,在上层)
    lv_point_precise_t edge_pts[2];
    lv_point_precise_t pts[2];
} hand_view_t;

static lv_obj_t  *s_clock_group;       // 固定尺寸/固定位置的容器,只装两针 + 中心帽
static hand_view_t s_hour, s_min;
static lv_obj_t   *s_cap;
static int         s_last_t = -1;      // -1 = 从未画过,强制首帧落针

// 静态刻度线的持久点数组(lv_line 只存指针,数组必须常驻;60 条刻度线一次性画完不再碰)
static lv_point_precise_t s_tick_pts[60][2];
// 12 刻度数字标签(win 庆祝时临时点亮为 C_QUIZ,下一题开始时收回 C_NUM;§6.3)
static lv_obj_t *s_num_labels[12];
static bool       s_numbers_lit;

// ── 庆祝泛光环(SPEC §6.3):单次柔和 opa 淡出,非连闪 ────────────────────
static lv_obj_t *s_glow;

// ── 信息区:状态条①两位模式开关 + 连接点 + 长按进度条 ─────────────────────
static lv_obj_t *s_mode_a_hi, *s_mode_b_hi;          // 选中槽底(C_CARD_HI)
static lv_obj_t *s_mode_a_head, *s_mode_a_body;      // 槽A:人形(头+肩)
static lv_obj_t *s_mode_b_border, *s_mode_b_l1, *s_mode_b_l2;  // 槽B:屏形(边框+两条线)
static lv_obj_t *s_link_dot;
static lv_obj_t *s_hold_bar;
static lv_obj_t *s_mode_hotspot;
static bool       s_mode_quiz = false;      // 应用层当前模式的 UI 侧镜像(no-op 判断用)
static bool       s_linked = true;
static clock_ui_mode_toggle_cb_t s_mode_cb;
static void      *s_mode_cb_user;
static uint32_t    s_press_start_tick;
static bool         s_pressing;
static bool         s_hold_fired;

// ── 信息区:② 数字钟读数(占位点 / 数字) ───────────────────────────────
static lv_obj_t *s_panel_card;
static lv_obj_t *s_panel_label;
static lv_obj_t *s_panel_dots[3];
static bool       s_panel_show_last = false;
static int        s_panel_t_last = -1;
static bool       s_panel_quiz_last = false;
static bool       s_panel_correct_last = false;

// ── 信息区:③ 反馈脸(idle/yay/huh) ─────────────────────────────────────
static lv_obj_t *s_face_dyn;                 // 容器:装当前表情的眼/嘴/眉,换表情时 lv_obj_clean 重建
static clock_ui_face_t s_face_last = (clock_ui_face_t)-1;   // 强制首次绘制

// ── 渐进提示弧(SPEC §5.6) ───────────────────────────────────────────────
static lv_obj_t *s_hint_arc;
static int        s_hint_width_last = 0;

// 0° = 12 点方向,顺时针为正 —— 与 tools/preview.py::pol()/hand_angles() 同式(SPEC §5.1)
static inline void polar(float cx, float cy, float r, float deg, float *x, float *y)
{
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;
    *x = cx + r * cosf(rad);
    *y = cy + r * sinf(rad);
}

// ── 通用小工具:剥掉默认样式、关掉点击/滚动(信息区一大堆纯装饰对象共用)────────
static lv_obj_t *make_plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_hand_segment(lv_obj_t *parent, int width, uint32_t color_hex)
{
    lv_obj_t *o = lv_line_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(o, width, LV_PART_MAIN);
    lv_obj_set_style_line_color(o, lv_color_hex(color_hex), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(o, true, LV_PART_MAIN);   // 两端圆头(等价 preview.py 的手绘圆头)
    lv_obj_set_style_line_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    return o;
}

// 把 (cx,cy)-(tx,ty) 这条线段,以"两端点分量最小值"为原点重新表达成相对坐标,
// 摆到 obj 身上(见文件顶注)。cx,cy/tx,ty 都是 clock_group 的本地坐标(中心恒为
// CLOCK_R,CLOCK_R,不是屏幕绝对坐标)。
static void place_segment(lv_obj_t *obj, lv_point_precise_t pts[2],
                          float cx, float cy, float tx, float ty)
{
    float ox = (cx < tx) ? cx : tx;
    float oy = (cy < ty) ? cy : ty;
    pts[0].x = (lv_value_precise_t)lroundf(cx - ox);
    pts[0].y = (lv_value_precise_t)lroundf(cy - oy);
    pts[1].x = (lv_value_precise_t)lroundf(tx - ox);
    pts[1].y = (lv_value_precise_t)lroundf(ty - oy);
    lv_obj_set_pos(obj, (int32_t)lroundf(ox), (int32_t)lroundf(oy));
    lv_line_set_points_mutable(obj, pts, 2);
}

// ── 一个通用的"只画一段弧"小部件(反馈脸五官 + 提示弧共用)──────────────────
// 0°=3点钟方向,顺时针为正 —— 与 PIL ImageDraw.arc 同一约定(与两针的"12点基准"不同,
// 调用方若从 clock_model 的角度换算过来,需要自己先 -90,见 clock_ui_set_hint)。
static lv_obj_t *make_arc_widget(lv_obj_t *parent, int size, uint32_t color_hex, int width)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_set_size(a, size, size);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);       // 背景圆环不显示
    lv_obj_set_style_arc_color(a, lv_color_hex(color_hex), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(a, 0, 360);
    return a;
}

// ── 静态层:钟面圆盘 + 边框 + 60 刻度 + 12 数字(scr 的直接子节点,绝对屏幕坐标,
//    进场画一次、之后永不重画——见 CLAUDE.md §6.1 三层渲染模型)────────────────
static void create_static_face(lv_obj_t *scr)
{
    lv_obj_t *face = lv_obj_create(scr);
    lv_obj_remove_style_all(face);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(face, CLOCK_R * 2, CLOCK_R * 2);
    lv_obj_set_pos(face, CLOCK_CX - CLOCK_R, CLOCK_CY - CLOCK_R);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(C_FACE), 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(face, lv_color_hex(C_RIM), 0);
    lv_obj_set_style_border_width(face, FACE_BORDER_W, 0);

    for (int i = 0; i < 60; i++) {
        float deg = i * 6.0f;
        bool major = (i % 5) == 0;
        float r_in = major ? TICK_MAJ_IN : TICK_MIN_IN;
        float x1, y1, x2, y2;
        polar(CLOCK_CX, CLOCK_CY, r_in, deg, &x1, &y1);
        polar(CLOCK_CX, CLOCK_CY, TICK_OUT, deg, &x2, &y2);

        s_tick_pts[i][0].x = (lv_value_precise_t)lroundf(x1);
        s_tick_pts[i][0].y = (lv_value_precise_t)lroundf(y1);
        s_tick_pts[i][1].x = (lv_value_precise_t)lroundf(x2);
        s_tick_pts[i][1].y = (lv_value_precise_t)lroundf(y2);

        lv_obj_t *tick = lv_line_create(scr);
        lv_obj_remove_style_all(tick);
        lv_obj_remove_flag(tick, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_line_width(tick, major ? TICK_MAJ_W : TICK_MIN_W, LV_PART_MAIN);
        lv_obj_set_style_line_color(tick, lv_color_hex(major ? C_TICK_MAJ : C_TICK_MIN), LV_PART_MAIN);
        lv_obj_set_style_line_rounded(tick, true, LV_PART_MAIN);
        lv_obj_set_style_line_opa(tick, LV_OPA_COVER, LV_PART_MAIN);
        // 静态、绝对屏幕坐标、画完不再碰:不需要 place_segment 那套"相对坐标省脏矩形"
        // 的把戏(那是给每帧要动的对象用的),直接绝对坐标即可,视觉结果一致。
        lv_line_set_points(tick, s_tick_pts[i], 2);
    }

    // 🔴 montserrat_18 的数字实高 ≈13px,才等于 preview.py 的 NUM_H=13。
    //    别退回 LV_FONT_DEFAULT —— 那是 montserrat_14,数字实高只有 ≈10px(矮 ~23%)。
    //    该字体由 sdkconfig.defaults 的 CONFIG_LV_FONT_MONTSERRAT_18 编进来。
    const lv_font_t *num_font = &lv_font_montserrat_18;
    for (int n = 1; n <= 12; n++) {
        float x, y;
        polar(CLOCK_CX, CLOCK_CY, NUM_RING_R, n * 30.0f, &x, &y);

        lv_obj_t *anchor = lv_obj_create(scr);
        lv_obj_remove_style_all(anchor);
        lv_obj_remove_flag(anchor, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(anchor, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(anchor, 32, 20);
        lv_obj_set_pos(anchor, (int32_t)lroundf(x) - 16, (int32_t)lroundf(y) - 10);

        lv_obj_t *lbl = lv_label_create(anchor);
        lv_obj_set_style_text_font(lbl, num_font, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_NUM), 0);
        lv_label_set_text_fmt(lbl, "%d", n);
        lv_obj_center(lbl);
        s_num_labels[n - 1] = lbl;         // 存指针:win 庆祝时临时点亮用(§6.3)
    }
}

// ── 庆祝泛光环(SPEC §6.3):创建于面盘之下、之前(z-order 更早),常态不可见 ─────
static void create_glow_ring(lv_obj_t *scr)
{
    int r = CLOCK_R + 12;
    s_glow = lv_obj_create(scr);
    lv_obj_remove_style_all(s_glow);
    lv_obj_remove_flag(s_glow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_glow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_glow, r * 2, r * 2);
    lv_obj_set_pos(s_glow, CLOCK_CX - r, CLOCK_CY - r);
    lv_obj_set_style_radius(s_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_glow, LV_OPA_TRANSP, 0);          // 只要边框,不要底色
    lv_obj_set_style_border_width(s_glow, 4, 0);
    lv_obj_set_style_border_color(s_glow, lv_color_hex(C_QUIZ), 0);
    lv_obj_set_style_border_opa(s_glow, LV_OPA_TRANSP, 0);      // 常态透明,win 时才淡入淡出
}

static void glow_set_opa(void *obj, int32_t v)
{
    lv_obj_set_style_border_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

// ── 动态层:两针(edge 在下、line 在上)+ 中心帽(整体建在 clock_group 里,
//    本地坐标系以 clock_group 左上角为原点,钟心恒为 (CLOCK_R, CLOCK_R))──────────
static void create_dynamic_hands(void)
{
    // 🔴 创建顺序 = z-order,不可交换(2026-08-06 实机截图实证,同 preview.py::frame):
    //    分针描边宽 HAND_MIN_W+2*HAND_EDGE = 9 > 时针本体宽 HAND_HOUR_W = 8。分针若在上,
    //    两针重合时(12:00 最典型)那圈钟面色描边把时针整条抹掉,屏上只剩一条 5px 细线,
    //    "粗短=时针"这条区分线索当场失效(SPEC §5.3.1)。故**分针在下、时针在上**,
    //    重合姿态得到「粗短桩 + 细长尖」,照样读得出来。
    //    ⚠️ 2026-08-10 时针改用 C_HAND_HOUR(砖红)后本条**依然有效**:描边是钟面色,
    //    与两针各自什么颜色无关,分针在上照样能把时针抹掉一整条。别因为"现在有颜色了"
    //    就以为画序可以随便换。
    s_min.edge  = make_hand_segment(s_clock_group, HAND_MIN_W  + HAND_EDGE * 2, C_FACE);
    s_min.line  = make_hand_segment(s_clock_group, HAND_MIN_W,                  C_HAND);
    s_hour.edge = make_hand_segment(s_clock_group, HAND_HOUR_W + HAND_EDGE * 2, C_FACE);
    s_hour.line = make_hand_segment(s_clock_group, HAND_HOUR_W,                 C_HAND_HOUR);

    s_cap = lv_obj_create(s_clock_group);          // 中心帽:创建顺序在两针之后 → z-order 最上
    lv_obj_remove_style_all(s_cap);
    lv_obj_remove_flag(s_cap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_cap, CAP_R * 2, CAP_R * 2);
    lv_obj_set_pos(s_cap, CLOCK_R - CAP_R, CLOCK_R - CAP_R);   // 固定在本地中心,永不移动
    lv_obj_set_style_radius(s_cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_cap, lv_color_hex(C_HAND), 0);
    lv_obj_set_style_bg_opa(s_cap, LV_OPA_COVER, 0);
}

// ── 信息区:三块底卡(静态层,进场画一次;②的橙描边在换模式时才改样式,§5.3)────
static void rrect_card(lv_obj_t *card, int x0, int y0, int x1, int y1)
{
    lv_obj_remove_style_all(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, x0, y0);
    lv_obj_set_size(card, x1 - x0, y1 - y0);
    lv_obj_set_style_radius(card, CARD_R, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
}

static void create_info_cards(lv_obj_t *scr)
{
    lv_obj_t *stat = lv_obj_create(scr);
    rrect_card(stat, INFO_X0, Z_STAT_Y0, INFO_X1, Z_STAT_Y1);
    lv_obj_set_style_bg_color(stat, lv_color_hex(C_CARD), 0);

    s_panel_card = lv_obj_create(scr);
    rrect_card(s_panel_card, INFO_X0, Z_PANEL_Y0, INFO_X1, Z_PANEL_Y1);
    lv_obj_set_style_bg_color(s_panel_card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_width(s_panel_card, 0, 0);

    lv_obj_t *face_card = lv_obj_create(scr);
    rrect_card(face_card, INFO_X0, Z_FACE_Y0, INFO_X1, Z_FACE_Y1);
    lv_obj_set_style_bg_color(face_card, lv_color_hex(C_CARD), 0);
}

// ── 状态条①:两位模式开关(人形=FREE/屏形=QUIZ)+ 连接点 + 电量壳 + 长按进度条 ──
static void create_mode_icon_person(lv_obj_t *scr, int slot_x0)
{
    int cx = slot_x0 + MODE_SLOT_W / 2;
    int cy = MODE_SLOT_Y0 + MODE_SLOT_H / 2;

    s_mode_a_head = make_plain(scr);
    lv_obj_set_size(s_mode_a_head, 7, 7);
    lv_obj_set_pos(s_mode_a_head, cx - 3, cy - 8);
    lv_obj_set_style_radius(s_mode_a_head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_mode_a_head, LV_OPA_COVER, 0);

    s_mode_a_body = make_plain(scr);
    lv_obj_set_size(s_mode_a_body, 15, 8);
    lv_obj_set_pos(s_mode_a_body, cx - 7, cy);
    lv_obj_set_style_radius(s_mode_a_body, 4, 0);
    lv_obj_set_style_bg_opa(s_mode_a_body, LV_OPA_COVER, 0);
}

static void create_mode_icon_screen(lv_obj_t *scr, int slot_x0)
{
    int cx = slot_x0 + MODE_SLOT_W / 2;
    int cy = MODE_SLOT_Y0 + MODE_SLOT_H / 2;

    s_mode_b_border = make_plain(scr);
    lv_obj_set_size(s_mode_b_border, 17, 13);
    lv_obj_set_pos(s_mode_b_border, cx - 8, cy - 6);
    lv_obj_set_style_radius(s_mode_b_border, 3, 0);
    lv_obj_set_style_bg_opa(s_mode_b_border, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_mode_b_border, 2, 0);

    s_mode_b_l1 = make_plain(scr);
    lv_obj_set_size(s_mode_b_l1, 8, 2);
    lv_obj_set_pos(s_mode_b_l1, cx - 4, cy - 2);
    lv_obj_set_style_bg_opa(s_mode_b_l1, LV_OPA_COVER, 0);

    s_mode_b_l2 = make_plain(scr);
    lv_obj_set_size(s_mode_b_l2, 5, 2);
    lv_obj_set_pos(s_mode_b_l2, cx - 4, cy + 2);
    lv_obj_set_style_bg_opa(s_mode_b_l2, LV_OPA_COVER, 0);
}

static void mode_hotspot_event_cb(lv_event_t *e)
{
    // 全部在 LVGL 任务上下文执行(事件分发发生在 lv_timer_handler 里),不能也不需要再
    // bsp_display_lock(同 apps/tilt_maze/main/parent_menu.c 的既有约定)。
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESSED:
        s_press_start_tick = lv_tick_get();
        s_pressing = true;
        s_hold_fired = false;
        lv_bar_set_value(s_hold_bar, 0, LV_ANIM_OFF);
        lv_obj_remove_flag(s_hold_bar, LV_OBJ_FLAG_HIDDEN);
        break;
    case LV_EVENT_PRESSING: {
        if (!s_pressing || s_hold_fired) {
            break;
        }
        uint32_t elapsed = lv_tick_elaps(s_press_start_tick);
        int32_t v = (int32_t)((uint64_t)elapsed * 1000u / MODE_HOLD_MS);
        if (v > 1000) {
            v = 1000;
        }
        lv_bar_set_value(s_hold_bar, v, LV_ANIM_OFF);
        if (elapsed >= (uint32_t)MODE_HOLD_MS) {
            s_hold_fired = true;
            lv_obj_add_flag(s_hold_bar, LV_OBJ_FLAG_HIDDEN);
            if (s_mode_cb) {
                s_mode_cb(s_mode_cb_user);
            }
        }
        break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        s_pressing = false;
        lv_obj_add_flag(s_hold_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_hold_bar, 0, LV_ANIM_OFF);
        break;
    default:
        break;
    }
}

static void create_status_bar(lv_obj_t *scr)
{
    // 选中槽底(建在图标之前,z-order 在图标之下)
    s_mode_a_hi = make_plain(scr);
    rrect_card(s_mode_a_hi, MODE_A_X0, MODE_SLOT_Y0, MODE_A_X0 + MODE_SLOT_W, MODE_SLOT_Y0 + MODE_SLOT_H);
    lv_obj_set_style_bg_color(s_mode_a_hi, lv_color_hex(C_CARD_HI), 0);
    lv_obj_set_style_radius(s_mode_a_hi, 5, 0);

    s_mode_b_hi = make_plain(scr);
    rrect_card(s_mode_b_hi, MODE_B_X0, MODE_SLOT_Y0, MODE_B_X0 + MODE_SLOT_W, MODE_SLOT_Y0 + MODE_SLOT_H);
    lv_obj_set_style_bg_color(s_mode_b_hi, lv_color_hex(C_CARD_HI), 0);
    lv_obj_set_style_radius(s_mode_b_hi, 5, 0);

    create_mode_icon_person(scr, MODE_A_X0);
    create_mode_icon_screen(scr, MODE_B_X0);

    // 旋钮连接点(绿=在/红=拔了)
    s_link_dot = make_plain(scr);
    lv_obj_set_size(s_link_dot, LINK_R * 2, LINK_R * 2);
    lv_obj_set_pos(s_link_dot, LINK_CX - LINK_R, LINK_CY - LINK_R);
    lv_obj_set_style_radius(s_link_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_link_dot, LV_OPA_COVER, 0);

    // 电量壳:本轮只画静态满格(不接 AXP192 真读数,SPEC §5.3 TODO),画完不再碰
    lv_obj_t *batt_body = make_plain(scr);
    lv_obj_set_pos(batt_body, BATT_X0, BATT_Y0);
    lv_obj_set_size(batt_body, BATT_W, BATT_H);
    lv_obj_set_style_radius(batt_body, 2, 0);
    lv_obj_set_style_bg_opa(batt_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(batt_body, 2, 0);
    lv_obj_set_style_border_color(batt_body, lv_color_hex(C_MUTED), 0);

    lv_obj_t *batt_nub = make_plain(scr);
    lv_obj_set_pos(batt_nub, BATT_X0 + BATT_W, BATT_Y0 + BATT_H / 2 - 3);
    lv_obj_set_size(batt_nub, 3, 6);
    lv_obj_set_style_bg_opa(batt_nub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(batt_nub, lv_color_hex(C_MUTED), 0);

    lv_obj_t *batt_fill = make_plain(scr);
    lv_obj_set_pos(batt_fill, BATT_X0 + 3, BATT_Y0 + 3);
    lv_obj_set_size(batt_fill, BATT_W - 6, BATT_H - 6);
    lv_obj_set_style_bg_opa(batt_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(batt_fill, lv_color_hex(C_MUTED), 0);

    // 长按进度条(SPEC §5.3.5):按住状态条切模式期间从 X0 长到 X1,松手不足则归零隐藏
    s_hold_bar = lv_bar_create(scr);
    lv_obj_set_pos(s_hold_bar, HOLD_BAR_X0, HOLD_BAR_Y0);
    lv_obj_set_size(s_hold_bar, HOLD_BAR_X1 - HOLD_BAR_X0, HOLD_BAR_H);
    lv_bar_set_range(s_hold_bar, 0, 1000);
    lv_bar_set_value(s_hold_bar, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(s_hold_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_hold_bar, HOLD_BAR_H / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_hold_bar, LV_OPA_TRANSP, LV_PART_MAIN);   // 不画轨道底色
    lv_obj_set_style_radius(s_hold_bar, HOLD_BAR_H / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_hold_bar, lv_color_hex(C_QUIZ), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_hold_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_add_flag(s_hold_bar, LV_OBJ_FLAG_HIDDEN);   // 常态隐藏,按住才显示

    // 长按热区:唯一吃触摸事件的对象(其余装饰对象都已关 CLICKABLE)。
    // 🔴 **热区刻意比状态条卡片大一圈**(见 MODE_HOTSPOT_* 常量),不与视觉边界对齐:
    //    手指按住不动 1.5s,一旦滑出对象 LVGL 就发 PRESS_LOST、进度清零重来;卡片只有
    //    34px 高(≈4mm),指腹的自然位移就足以出界。触摸靶 ≠ 视觉靶,上下各留一截余量。
    //    通用做法见根 CLAUDE.md §8。
    s_mode_hotspot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_mode_hotspot);
    lv_obj_set_pos(s_mode_hotspot, MODE_HOTSPOT_X0, MODE_HOTSPOT_Y0);
    lv_obj_set_size(s_mode_hotspot, MODE_HOTSPOT_X1 - MODE_HOTSPOT_X0,
                    MODE_HOTSPOT_Y1 - MODE_HOTSPOT_Y0);
    lv_obj_set_style_bg_opa(s_mode_hotspot, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_mode_hotspot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_mode_hotspot, mode_hotspot_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_mode_hotspot, mode_hotspot_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_mode_hotspot, mode_hotspot_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_mode_hotspot, mode_hotspot_event_cb, LV_EVENT_PRESS_LOST, NULL);
}

// ── 信息区:② 数字钟读数(占位点 / 数字,SPEC §5.3.2.1/§5.4)────────────────
static void create_panel(lv_obj_t *scr)
{
    for (int k = 0; k < 3; k++) {
        lv_obj_t *dot = make_plain(scr);
        lv_obj_set_size(dot, 8, 8);
        int x = PANEL_CX + (k - 1) * 17 - 4;
        int y = PANEL_CY - 4;
        lv_obj_set_pos(dot, x, y);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x5C6478), 0);
        s_panel_dots[k] = dot;
    }

    lv_obj_t *anchor = make_plain(scr);
    lv_obj_set_size(anchor, PANEL_HW * 2, 40);
    lv_obj_set_pos(anchor, PANEL_CX - PANEL_HW, PANEL_CY - 20);

    s_panel_label = lv_label_create(anchor);
    // 🔴 montserrat_34 才能撑到 READOUT_H=25 的实际像素高(≈0.72×字号);别退回 18 号那档
    // (那是给 12 刻度数字用的,字高只有 13px)。CONFIG_LV_FONT_MONTSERRAT_34 在
    // sdkconfig.defaults 里开(改后须 rm sdkconfig 再 fullclean,CLAUDE.md §11)。
    lv_obj_set_style_text_font(s_panel_label, &lv_font_montserrat_34, 0);
    lv_obj_set_style_text_color(s_panel_label, lv_color_hex(C_DIGIT), 0);
    lv_label_set_text(s_panel_label, "");
    lv_obj_center(s_panel_label);
    lv_obj_add_flag(s_panel_label, LV_OBJ_FLAG_HIDDEN);

    // 🔴 一次性运行时量宽检查(CLAUDE.md §11 平台坑教训:上一批因默认字体矮了 23% 返工过):
    // 最宽读数「12:55」实测宽度须 < 子区②内腔(2*PANEL_HW - 2*PANEL_BORDER)。只在这里 log 一次,
    // 不做运行时自动降级(降级需要同时编两档字体,超出本轮范围,见交付报告"未做"清单)。
    lv_point_t sz;
    lv_text_get_size(&sz, "12:55", &lv_font_montserrat_34, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int inner = 2 * PANEL_HW - 2 * PANEL_BORDER;
    if (sz.x >= inner) {
        ESP_LOGW(TAG, "数字钟读数「12:55」实测宽 %dpx >= 子区②内腔 %dpx —— 会撑破底卡,"
                      "需要把 READOUT_H/PANEL_HW 重算或换 montserrat_32", (int)sz.x, inner);
    } else {
        ESP_LOGI(TAG, "数字钟读数「12:55」实测宽 %dpx < 子区②内腔 %dpx,合规", (int)sz.x, inner);
    }
}

// ── 信息区:③ 反馈脸(idle/yay/huh,SPEC §5.3.4)────────────────────────────
static void create_face_base(lv_obj_t *scr)
{
    int cy = (Z_FACE_Y0 + Z_FACE_Y1) / 2;

    lv_obj_t *base = make_plain(scr);
    lv_obj_set_size(base, FACE_R * 2, FACE_R * 2);
    lv_obj_set_pos(base, PANEL_CX - FACE_R, cy - FACE_R);
    lv_obj_set_style_radius(base, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(base, lv_color_hex(C_FACE), 0);   // 脸用钟面同色

    s_face_dyn = make_plain(scr);
    lv_obj_set_size(s_face_dyn, FACE_R * 2, FACE_R * 2);
    lv_obj_set_pos(s_face_dyn, PANEL_CX - FACE_R, cy - FACE_R);
    lv_obj_set_style_bg_opa(s_face_dyn, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_face_dyn, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   // 眉毛/歪头偶尔探出容器,兜底
}

// 局部坐标系:s_face_dyn 内,中心 (FACE_R, FACE_R)
static void face_draw_eye(int ex, int ey_extra)
{
    lv_obj_t *eye = make_plain(s_face_dyn);
    lv_obj_set_size(eye, 9, 9);
    lv_obj_set_pos(eye, FACE_R + ex - 4, FACE_R - 9 + ey_extra - 4);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(eye, lv_color_hex(C_HAND), 0);
}

// 一段"以 (cx,cy) 为中心、size 见方"的弧(PIL 同约定:0°=3点钟,顺时针)
static void face_draw_arc(int cx, int cy, int size, int start_deg, int end_deg, int width)
{
    lv_obj_t *a = make_arc_widget(s_face_dyn, size, C_HAND, width);
    lv_obj_set_pos(a, FACE_R + cx - size / 2, FACE_R + cy - size / 2);
    lv_arc_set_angles(a, start_deg, end_deg);
}

static void draw_face_idle(void)
{
    face_draw_eye(-13, 0);
    face_draw_eye(13, 0);
    face_draw_arc(0, 7, 28, 15, 165, 3);            // 下半弧 = 微笑
}

static void draw_face_yay(void)
{
    // 大笑:眼睛弯成弧(闭眼上扬),嘴用一颗圆角"胶囊"近似张大的笑口(LVGL 无原生弦月填充,
    // 68px 脸盘下这个简化仍读得出"大笑",与 preview.py 的 chord 填充效果等价传达)。
    face_draw_arc(-13, -9, 12, 200, 340, 3);
    face_draw_arc(13, -9, 12, 200, 340, 3);

    lv_obj_t *mouth = make_plain(s_face_dyn);
    lv_obj_set_size(mouth, 22, 10);
    lv_obj_set_pos(mouth, FACE_R - 11, FACE_R + 4);
    lv_obj_set_style_radius(mouth, 5, 0);
    lv_obj_set_style_bg_opa(mouth, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mouth, lv_color_hex(C_HAND), 0);
}

static void draw_face_huh(void)
{
    face_draw_eye(-13, 2);          // 左眼略低 = 歪头
    face_draw_eye(13, 0);
    face_draw_arc(9, -18, 22, 180, 340, 3);          // 挑起的眉

    // ⚠️ 疑问的嘴必须是波浪线,不能用小圆 —— 小圆跟"张嘴说话"太像,语音已砍,孩子会
    // 以为"它在说话"而干等一个永远不会来的声音(SPEC §5.3.4 定案,别改回小圆嘴)。
    static const int sx[3]     = { -11, -1, 9 };
    static const int y_off[3]  = { 3, 7, 3 };
    static const int start_a[3] = { 0, 180, 0 };
    static const int end_a[3]   = { 180, 360, 180 };
    for (int k = 0; k < 3; k++) {
        face_draw_arc(sx[k] + 6, y_off[k] + 4, 12, start_a[k], end_a[k], 3);
    }
}

// ── 提示弧(SPEC §5.6/§6.1):走时针角、半径 HINT_R,创建于钟面上层、初始隐藏 ─────
static void create_hint_arc(lv_obj_t *scr)
{
    s_hint_arc = make_arc_widget(scr, HINT_R * 2, C_HINT, HINT_ARC_W1);
    lv_obj_set_pos(s_hint_arc, CLOCK_CX - HINT_R, CLOCK_CY - HINT_R);
    lv_arc_set_angles(s_hint_arc, 0, 0);
    lv_obj_add_flag(s_hint_arc, LV_OBJ_FLAG_HIDDEN);
}

void clock_ui_create(void)
{
    bsp_display_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // z-order = 创建顺序(后创建者盖在先创建者之上):
    // 泛光环(庆祝用,常态透明)→ 钟面静态层 → 提示弧 → 动态两针容器 → 信息区(卡片→
    // 状态条→面板→反馈脸)。泛光环半径 > CLOCK_R,创建在钟面之前让面盘边框天然盖住
    // 泛光环落在盘内的部分,只剩外圈可见(§6.3 泛光只该在盘外)。
    create_glow_ring(scr);
    create_static_face(scr);

    create_hint_arc(scr);

    // 动态层的固定容器:覆盖整个钟面圆盘,开 OVERFLOW_VISIBLE 兜底(CLAUDE.md §11 坑:
    // LVGL 默认把子对象裁到父的 coords;两针端点半径 <= HAND_MIN_LEN(72) < CLOCK_R(95),
    // 本不会出屏,这里额外开启只为吃掉线宽/描边在极端角度的几像素溢出,零成本)。
    s_clock_group = lv_obj_create(scr);
    lv_obj_remove_style_all(s_clock_group);
    lv_obj_remove_flag(s_clock_group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_clock_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_clock_group, LV_OPA_TRANSP, 0);
    lv_obj_set_size(s_clock_group, CLOCK_R * 2, CLOCK_R * 2);
    lv_obj_set_pos(s_clock_group, CLOCK_CX - CLOCK_R, CLOCK_CY - CLOCK_R);
    lv_obj_add_flag(s_clock_group, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    create_dynamic_hands();

    create_info_cards(scr);
    create_status_bar(scr);
    create_panel(scr);
    create_face_base(scr);

    bsp_display_unlock();

    // 首次进场:默认 MODE_FREE、占位点、脸 idle、连接点绿(main.c 会在真正探测到单元后
    // 再调 clock_ui_set_linked 更新一次)。放在 unlock 之后调用,各函数自己再取锁。
    clock_ui_set_mode(false);
    clock_ui_set_linked(true);
    clock_ui_set_panel(false, 0, false, false);
    clock_ui_set_face(CLOCK_UI_FACE_IDLE);
}

void clock_ui_set_time(int t)
{
    if (t == s_last_t) {
        return;   // 没变,零 LVGL 调用(§6.2 帧预算:静止时刻不该有任何重绘)
    }
    s_last_t = t;

    float hour_deg = clock_model_hour_angle(t);
    float min_deg  = clock_model_minute_angle(t);

    // 本地坐标系(clock_group 内):钟心恒为 (CLOCK_R, CLOCK_R)
    const float cx = CLOCK_R, cy = CLOCK_R;
    float hx, hy, mx, my;
    polar(cx, cy, HAND_HOUR_LEN, hour_deg, &hx, &hy);
    polar(cx, cy, HAND_MIN_LEN,  min_deg,  &mx, &my);

    bsp_display_lock(0);
    place_segment(s_hour.edge, s_hour.edge_pts, cx, cy, hx, hy);
    place_segment(s_hour.line, s_hour.pts,      cx, cy, hx, hy);
    place_segment(s_min.edge,  s_min.edge_pts,  cx, cy, mx, my);
    place_segment(s_min.line,  s_min.pts,       cx, cy, mx, my);
    bsp_display_unlock();
}

void clock_ui_set_mode_toggle_cb(clock_ui_mode_toggle_cb_t cb, void *user)
{
    s_mode_cb = cb;
    s_mode_cb_user = user;
}

void clock_ui_set_mode(bool quiz)
{
    static bool s_have_drawn = false;   // 首次强制跑一次(哪怕默认值恰好等于 quiz 参数)
    if (s_have_drawn && quiz == s_mode_quiz) {
        return;
    }
    s_have_drawn = true;
    s_mode_quiz = quiz;

    bsp_display_lock(0);
    uint32_t a_color = quiz ? C_MUTED : C_QUIZ;
    uint32_t b_color = quiz ? C_QUIZ : C_MUTED;
    lv_obj_set_style_bg_color(s_mode_a_head, lv_color_hex(a_color), 0);
    lv_obj_set_style_bg_color(s_mode_a_body, lv_color_hex(a_color), 0);
    lv_obj_set_style_border_color(s_mode_b_border, lv_color_hex(b_color), 0);
    lv_obj_set_style_bg_color(s_mode_b_l1, lv_color_hex(b_color), 0);
    lv_obj_set_style_bg_color(s_mode_b_l2, lv_color_hex(b_color), 0);
    if (quiz) {
        lv_obj_add_flag(s_mode_a_hi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_mode_b_hi, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_mode_a_hi, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_mode_b_hi, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

void clock_ui_set_linked(bool linked)
{
    static bool s_have_drawn = false;
    if (s_have_drawn && linked == s_linked) {
        return;
    }
    s_have_drawn = true;
    s_linked = linked;

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(s_link_dot, lv_color_hex(linked ? C_LINK_OK : C_LINK_BAD), 0);
    bsp_display_unlock();
}

void clock_ui_set_panel(bool show, int t, bool quiz_style, bool correct)
{
    if (show == s_panel_show_last && t == s_panel_t_last &&
        quiz_style == s_panel_quiz_last && correct == s_panel_correct_last) {
        return;   // no-op:三个来源(FREE 揭晓计时/QUIZ 出题/win)都可能重复调同一态
    }
    s_panel_show_last = show;
    s_panel_t_last = t;
    s_panel_quiz_last = quiz_style;
    s_panel_correct_last = correct;

    bsp_display_lock(0);
    // 底卡样式:MODE_QUIZ = 橙描边 + 暖橙暗底;否则普通底卡(SPEC §5.3.2.1)
    if (quiz_style) {
        lv_obj_set_style_bg_color(s_panel_card, lv_color_hex(C_CARD_Q), 0);
        lv_obj_set_style_border_width(s_panel_card, PANEL_BORDER, 0);
        lv_obj_set_style_border_color(s_panel_card, lv_color_hex(C_QUIZ), 0);
    } else {
        lv_obj_set_style_bg_color(s_panel_card, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_width(s_panel_card, 0, 0);
    }

    if (show) {
        int hh = (t / 60) % 12;
        if (hh == 0) {
            hh = 12;
        }
        lv_label_set_text_fmt(s_panel_label, "%d:%02d", hh, t % 60);
        uint32_t color = correct ? C_GREEN : (quiz_style ? C_QUIZ : C_DIGIT);
        lv_obj_set_style_text_color(s_panel_label, lv_color_hex(color), 0);
        lv_obj_remove_flag(s_panel_label, LV_OBJ_FLAG_HIDDEN);
        for (int k = 0; k < 3; k++) {
            lv_obj_add_flag(s_panel_dots[k], LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(s_panel_label, LV_OBJ_FLAG_HIDDEN);
        for (int k = 0; k < 3; k++) {
            lv_obj_remove_flag(s_panel_dots[k], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // 数字答对变绿只在庆祝期间;下一题开始(correct=false)时顺带把 12 刻度数字收回原色,
    // 不需要额外的"收尾计时器"(SPEC clock_ui_play_win 文档注释里说明的机制)。
    if (!correct && s_numbers_lit) {
        s_numbers_lit = false;
        for (int i = 0; i < 12; i++) {
            lv_obj_set_style_text_color(s_num_labels[i], lv_color_hex(C_NUM), 0);
        }
    }
    bsp_display_unlock();
}

void clock_ui_set_face(clock_ui_face_t mood)
{
    if (mood == s_face_last) {
        return;
    }
    s_face_last = mood;

    bsp_display_lock(0);
    lv_obj_clean(s_face_dyn);   // 丢掉上一个表情的眼/嘴/眉子对象,重新画(事件驱动,低频)
    switch (mood) {
    case CLOCK_UI_FACE_YAY: draw_face_yay(); break;
    case CLOCK_UI_FACE_HUH: draw_face_huh(); break;
    case CLOCK_UI_FACE_IDLE:
    default:                draw_face_idle(); break;
    }
    bsp_display_unlock();
}

void clock_ui_set_hint(int cur_t, int target_t, int width)
{
    if (width == s_hint_width_last && width == 0) {
        return;   // 一直隐藏,no-op
    }

    bsp_display_lock(0);
    if (width <= 0) {
        lv_obj_add_flag(s_hint_arc, LV_OBJ_FLAG_HIDDEN);
        s_hint_width_last = 0;
        bsp_display_unlock();
        return;
    }
    s_hint_width_last = width;

    // 🔴 走时针角,不是分针角(SPEC §5.6/§6.1):分针角 60min 一周期会把跨小时的差值
    // 算错;时针角 = t*0.5° 在 720min 内单调,唯一编码 Δt。走短边方向(<=180°)。
    float hour_now = clock_model_hour_angle(cur_t);
    float hour_target = clock_model_hour_angle(target_t);
    float cw = fmodf(hour_target - hour_now, 360.0f);
    if (cw < 0) {
        cw += 360.0f;
    }
    float a0, sweep;
    if (cw <= 180.0f) {
        a0 = hour_now;
        sweep = cw;
    } else {
        a0 = hour_target;
        sweep = 360.0f - cw;
    }

    // clock_model 的角度约定是"0°=12点钟方向,顺时针为正";lv_arc 的角度约定是
    // "0°=3点钟方向,顺时针为正"(与 PIL 一致,见 make_arc_widget 注释)——两者相差 90°,
    // 换算时减去 90 并归一化进 [0,360)(与 tools/preview.py::draw_hint_arc 同式)。
    float start = fmodf(a0 - 90.0f, 360.0f);
    if (start < 0) {
        start += 360.0f;
    }
    float end = start + sweep;

    uint32_t color = (width >= HINT_ARC_W2) ? C_HINT2 : C_HINT;
    lv_obj_set_style_arc_color(s_hint_arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_hint_arc, width, LV_PART_INDICATOR);
    lv_arc_set_angles(s_hint_arc, (int32_t)lroundf(start), (int32_t)lroundf(end));
    lv_obj_remove_flag(s_hint_arc, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}

void clock_ui_play_win(void)
{
    bsp_display_lock(0);

    // 12 刻度数字点亮(橙色),收回时机见 clock_ui_set_panel() 的说明
    s_numbers_lit = true;
    for (int i = 0; i < 12; i++) {
        lv_obj_set_style_text_color(s_num_labels[i], lv_color_hex(C_QUIZ), 0);
    }

    // 单次柔和泛光:opa 255→0 淡出,不重复(根 CLAUDE.md §8 光敏安全:非连闪)
    lv_obj_set_style_border_opa(s_glow, LV_OPA_COVER, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_glow);
    lv_anim_set_exec_cb(&a, glow_set_opa);
    lv_anim_set_values(&a, 255, 0);
    lv_anim_set_duration(&a, 700);
    lv_anim_start(&a);

    bsp_display_unlock();
}
