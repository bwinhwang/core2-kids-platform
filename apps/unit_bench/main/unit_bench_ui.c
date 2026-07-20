// unit_bench_ui —— LVGL 视图层实现
//
// 屏幕结构(320×240):顶 24px `ui_status_bar` 常驻;下方 216px 是 s_list_root(列表页)/
// s_detail_root(详情页)两个互斥显示的容器(同一时刻只显示一个,HIDDEN flag 切换,不整屏
// 重绘——CLAUDE.md §6)。详情页内部再分 content(0,0,320,184,数值卡/chart)+ bottom
// (0,184,320,32,按钮条)。
//
// 静态层/动态层纪律:列表页的行、详情页的卡片/chart/按钮都在 switch_view() 进页时创建
// 一次(静态层);之后每帧只调 ui_value_card_set_value / ui_chart_push / ui_list_menu_
// set_row_text 这些"只改动态部分"的 API。
//
// 加锁纪律:LVGL 事件回调(按钮点击/列表点击)本身跑在 LVGL 任务上下文里,直接调 LVGL API
// 不需要再包 bsp_display_lock(仿 apps/tilt_maze/main/parent_menu.c 的既有约定)。本文件
// 里唯一会从"另一个任务"(app_main 的主循环)touch LVGL 的路径是 unit_bench_ui_tick()——
// 读传感器(I2C/UART,可能阻塞几到几百 ms)一律放在 bsp_display_lock 之外,只在真正要改
// LVGL 对象的那几行才短暂加锁,避免长时间攥住 LVGL 的锁把渲染卡住。
//
// 热插拔:是否"挂载"由 unit_bench_scan 的后台 2s 周期扫描 + 各详情页读失败连续
// UB_FAIL_STREAK_LIMIT(20)帧判拔线共同维护(CLAUDE.md §10);详情页只负责"挂了就读、
// 没挂就显式红字",不自己管理挂载状态机。

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "chain_bus.h"
#include "data_log.h"
#include "haptics.h"
#include "kv_store.h"
#include "power_monitor.h"
#include "ui_kit.h"

#include "unit_8encoder.h"
#include "unit_chain_encoder.h"
#include "unit_chain_joystick.h"
#include "unit_dlight.h"
#include "unit_gesture.h"
#include "unit_scd41.h"
#include "unit_ultrasonic.h"

#include "unit_bench_scan.h"
#include "unit_bench_ui.h"

static const char *TAG = "ub_ui";

#define UB_FAIL_STREAK_LIMIT 20   // 连续读失败 ~20 帧(10Hz 主循环下约 2s)判拔线

typedef enum {
    UB_VIEW_LIST = 0,
    UB_VIEW_DLIGHT,
    UB_VIEW_ULTRASONIC,
    UB_VIEW_GESTURE,
    UB_VIEW_8ENCODER,
    UB_VIEW_SCD41,
    UB_VIEW_CHAIN_ENCODER,
    UB_VIEW_CHAIN_JOYSTICK,
} ub_view_t;

// ── 屏幕结构 ────────────────────────────────────────────────────────────────
static ui_status_bar_t *s_status_bar;
static lv_obj_t *s_list_root;
static lv_obj_t *s_detail_root;
static ui_list_menu_t *s_list_menu;
static lv_obj_t *s_bus_err_label;

static ub_view_t s_view = UB_VIEW_LIST;
static ub_kind_t s_row_kind[UB_SCAN_MAX_ROWS];
static bool             s_chain_row_present;
static chain_dev_type_t s_chain_row_type;

static int64_t s_last_scan_ms;
static int64_t s_last_status_ms;
static bool    s_power_ready;
static core2_sleep_t *s_sleep;   // unit_bench_ui_tick 每帧更新,poll_* 据此调 core2_sleep_kick

// ── 详情页共享控件(同一时刻只有一种视图在用,复用同一批指针)────────────────────
static ui_value_card_t *s_card0, *s_card1, *s_card2, *s_card3;
static ui_chart_t      *s_chart;
static lv_obj_t        *s_text_val0;   // Gesture "Last" 手写卡片的值 label
static lv_obj_t        *s_log_lbl;
static lv_obj_t        *s_ultra_offset_lbl;

static int  s_fail_count;
static bool s_logging;
static const char *s_log_name;
static const char *s_log_cols;

// DLight
static float s_dlight_prev = -1.0f;

// Ultrasonic
static int     s_ultra_phase;        // 0=待触发 1=已触发待读(两笔独立事务,见 unit_ultrasonic.h)
static float   s_ultra_prev = -1.0f;
static int32_t s_ultra_offset_mm;    // kv_store "ultra_offset",mm,标定零点偏移

// Gesture
static int s_gesture_count;

// 8Encoder(增量读后硬件自动清零,应用层自己累加成"绝对角度/计数")
static int32_t s_enc_total[UNIT_8ENCODER_NUM_ENC];
static bool    s_enc_prev_btn[UNIT_8ENCODER_NUM_ENC];
static bool    s_enc_prev_sw;

// SCD41(周期模式每 ~5s 一次新数;-1 = 尚无读数)
static int s_scd_prev_co2 = -1;

// Chain Encoder
static int16_t s_ce_prev_value;
static bool    s_ce_prev_btn;
static bool    s_ce_have_prev;

// Chain Joystick(个体有零偏,进页时采一次居中值做软件归中)
static uint16_t s_cj_center_x = 2048, s_cj_center_y = 2048;
static int      s_cj_prev_x, s_cj_prev_y;
static bool     s_cj_prev_btn;

// ── 前置声明 ────────────────────────────────────────────────────────────────
static void switch_view(ub_view_t v);
static void make_bottom_bar(lv_obj_t *bottom, bool with_cal);
static void update_ultra_offset_label(void);
static const char *gesture_name(gesture_event_t g);
static const char *kind_name(ub_kind_t k);

static void build_dlight(lv_obj_t *content, lv_obj_t *bottom);
static void build_ultrasonic(lv_obj_t *content, lv_obj_t *bottom);
static void build_gesture(lv_obj_t *content, lv_obj_t *bottom);
static void build_8encoder(lv_obj_t *content, lv_obj_t *bottom);
static void build_scd41(lv_obj_t *content, lv_obj_t *bottom);
static void build_chain_encoder(lv_obj_t *content, lv_obj_t *bottom);
static void build_chain_joystick(lv_obj_t *content, lv_obj_t *bottom);

static void poll_dlight(void);
static void poll_ultrasonic(void);
static void poll_gesture(void);
static void poll_8encoder(void);
static void poll_scd41(void);
static void poll_chain_encoder(void);
static void poll_chain_joystick(void);

// ── 小工具 ──────────────────────────────────────────────────────────────────

// 粗略电量映射(与 launcher/main/app_main.c 同一近似公式,LiPo 3.3V=0%~4.2V=100% 线性)
static int batt_mv_to_pct(int mv)
{
    if (mv <= 3300) return 0;
    if (mv >= 4200) return 100;
    return (int)((mv - 3300) * 100 / (4200 - 3300));
}

static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *text, lv_event_cb_t cb, lv_obj_t **out_lbl)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_KIT_COLOR_PANEL), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    if (out_lbl) *out_lbl = lbl;
    return btn;
}

// 手写一张"文字值"卡片(和 ui_value_card 同一视觉风格,但显示任意文字而非数字——
// 用于 Gesture 详情页的"最近手势"展示,ui_value_card 只支持数字/告警文字两态,
// 不适合展示正常态的手势名字符串)。返回值 label,调用方直接 lv_label_set_text* 更新。
static lv_obj_t *make_text_card(lv_obj_t *parent, int x, int y, int w, int h, const char *title)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_KIT_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 6, 4);

    lv_obj_t *val = lv_label_create(panel);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(UI_KIT_COLOR_VALUE), 0);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 4);

    return val;
}

static const char *kind_name(ub_kind_t k)
{
    switch (k) {
        case UB_KIND_DLIGHT:     return "DLight";
        case UB_KIND_ULTRASONIC: return "Ultrasonic";
        case UB_KIND_GESTURE:    return "Gesture";
        case UB_KIND_8ENCODER:   return "8Encoder";
        case UB_KIND_SCD41:      return "CO2L";
        default: return "?";
    }
}

static const char *gesture_name(gesture_event_t g)
{
    switch (g) {
        case GESTURE_UP:                return "UP";
        case GESTURE_DOWN:              return "DOWN";
        case GESTURE_LEFT:              return "LEFT";
        case GESTURE_RIGHT:             return "RIGHT";
        case GESTURE_FORWARD:           return "FORWARD";
        case GESTURE_BACKWARD:          return "BACKWARD";
        case GESTURE_CLOCKWISE:         return "CW";
        case GESTURE_COUNTER_CLOCKWISE: return "CCW";
        case GESTURE_WAVE:              return "WAVE";
        default:                        return "(无)";
    }
}

// ── 列表页 ──────────────────────────────────────────────────────────────────

static void on_list_row_click(int row_idx, void *user_data)
{
    (void)user_data;
    if (row_idx == UB_SCAN_MAX_ROWS) {   // 固定最后一行 = Chain
        if (!s_chain_row_present) { audio_fx_play(SND_BUMP_LIGHT); return; }
        if (s_chain_row_type == CHAIN_DEV_ENCODER)       switch_view(UB_VIEW_CHAIN_ENCODER);
        else if (s_chain_row_type == CHAIN_DEV_JOYSTICK) switch_view(UB_VIEW_CHAIN_JOYSTICK);
        else audio_fx_play(SND_BUMP_LIGHT);   // 在位但类型不认识,不跳转
        return;
    }
    if (row_idx < 0 || row_idx >= UB_SCAN_MAX_ROWS) return;
    switch (s_row_kind[row_idx]) {
        case UB_KIND_DLIGHT:     switch_view(UB_VIEW_DLIGHT);     break;
        case UB_KIND_ULTRASONIC: switch_view(UB_VIEW_ULTRASONIC); break;
        case UB_KIND_GESTURE:    switch_view(UB_VIEW_GESTURE);    break;
        case UB_KIND_8ENCODER:   switch_view(UB_VIEW_8ENCODER);   break;
        case UB_KIND_SCD41:      switch_view(UB_VIEW_SCD41);      break;
        default: audio_fx_play(SND_BUMP_LIGHT); break;   // 空行/未知地址:轻反馈,不跳转
    }
}

static void rebuild_list_rows(const ub_scan_result_t *r)
{
    if (r->bus_stuck) {
        lv_obj_remove_flag(s_bus_err_label, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < UB_SCAN_MAX_ROWS; i++) {
            s_row_kind[i] = UB_KIND_EMPTY;
            ui_list_menu_set_row_text(s_list_menu, i, "");
        }
    } else {
        lv_obj_add_flag(s_bus_err_label, LV_OBJ_FLAG_HIDDEN);
        char buf[40];
        for (int i = 0; i < UB_SCAN_MAX_ROWS; i++) {
            if (i < r->row_count) {
                s_row_kind[i] = r->rows[i].kind;
                if (r->rows[i].kind == UB_KIND_UNKNOWN)
                    snprintf(buf, sizeof(buf), "0x%02X (未知单元)", r->rows[i].addr);
                else
                    snprintf(buf, sizeof(buf), "0x%02X %s", r->rows[i].addr, kind_name(r->rows[i].kind));
                ui_list_menu_set_row_text(s_list_menu, i, buf);
            } else {
                s_row_kind[i] = UB_KIND_EMPTY;
                ui_list_menu_set_row_text(s_list_menu, i, "");
            }
        }
    }

    const char *chain_txt;
    if (!r->chain_present)                       chain_txt = "Chain: (未接)";
    else if (r->chain_type == CHAIN_DEV_ENCODER)  chain_txt = "Chain: Encoder";
    else if (r->chain_type == CHAIN_DEV_JOYSTICK) chain_txt = "Chain: Joystick";
    else                                          chain_txt = "Chain: 未知类型节点";
    ui_list_menu_set_row_text(s_list_menu, UB_SCAN_MAX_ROWS, chain_txt);
    s_chain_row_present = r->chain_present;
    s_chain_row_type    = r->chain_type;
}

static void on_rescan_click(lv_event_t *e)
{
    (void)e;
    audio_fx_play(SND_BUMP_LIGHT);
    // 手动即时刷新:阻塞至多约 300ms(Chain 探测超时)。已在 LVGL 事件回调上下文里,
    // 这一下停顿是用户主动点按触发的一次性代价,可接受(不在每帧主循环里发生)。
    ub_scan_result_t r;
    ub_scan_run(&r, 300);
    rebuild_list_rows(&r);   // 已在 LVGL 上下文,不再包 bsp_display_lock
    s_last_scan_ms = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;   // 顺带重置周期计时
}

static void build_list_ui(lv_obj_t *parent)
{
    s_bus_err_label = lv_label_create(parent);
    lv_label_set_text(s_bus_err_label, "PORT.A 总线异常,检查供电/线缆");
    lv_obj_set_style_text_font(s_bus_err_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_bus_err_label, lv_color_hex(UI_KIT_COLOR_WARN), 0);
    lv_obj_set_pos(s_bus_err_label, 4, 2);
    lv_obj_add_flag(s_bus_err_label, LV_OBJ_FLAG_HIDDEN);

    s_list_menu = ui_list_menu_create(parent, 0, 20, 320, 156);
    for (int i = 0; i < UB_SCAN_MAX_ROWS; i++) ui_list_menu_add_row(s_list_menu, "", false);
    ui_list_menu_add_row(s_list_menu, "Chain: (未接)", false);   // 固定最后一行
    ui_list_menu_on_click(s_list_menu, on_list_row_click, NULL);

    make_btn(parent, 4, 182, 150, 28, "Rescan", on_rescan_click, NULL);
}

// ── 详情页公共:底部按钮条 / Back / Log ─────────────────────────────────────

static void on_back_click(lv_event_t *e) { (void)e; switch_view(UB_VIEW_LIST); }

static void on_log_click(lv_event_t *e)
{
    (void)e;
    s_logging = !s_logging;
    if (s_logging) data_log_begin(s_log_name, s_log_cols);
    else            data_log_end();
    if (s_log_lbl) lv_label_set_text(s_log_lbl, s_logging ? "Log:ON" : "Log:OFF");
}

static void on_cal_minus(lv_event_t *e)
{
    (void)e;
    s_ultra_offset_mm--;
    kv_store_set_i32("ultra_offset", s_ultra_offset_mm);
    update_ultra_offset_label();
}

static void on_cal_plus(lv_event_t *e)
{
    (void)e;
    s_ultra_offset_mm++;
    kv_store_set_i32("ultra_offset", s_ultra_offset_mm);
    update_ultra_offset_label();
}

static void update_ultra_offset_label(void)
{
    if (s_ultra_offset_lbl) lv_label_set_text_fmt(s_ultra_offset_lbl, "offset %+ldmm", (long)s_ultra_offset_mm);
}

static void make_bottom_bar(lv_obj_t *bottom, bool with_cal)
{
    s_logging = false;
    s_log_lbl = NULL;
    if (with_cal) {
        make_btn(bottom, 2,   4, 68, 24, "Back",    on_back_click,  NULL);
        make_btn(bottom, 74,  4, 90, 24, "Log:OFF", on_log_click,   &s_log_lbl);
        make_btn(bottom, 168, 4, 68, 24, "Cal-",    on_cal_minus,   NULL);
        make_btn(bottom, 240, 4, 68, 24, "Cal+",    on_cal_plus,    NULL);
    } else {
        make_btn(bottom, 2,   4, 154, 24, "Back",    on_back_click, NULL);
        make_btn(bottom, 164, 4, 154, 24, "Log:OFF", on_log_click,  &s_log_lbl);
    }
}

// ── 视图切换(LVGL 事件回调上下文,不包锁;进页画一次静态层,不算每帧重绘)──────

static void switch_view(ub_view_t v)
{
    if (s_logging) { data_log_end(); s_logging = false; }   // 离开详情页自动收尾未关的录制
    s_view = v;

    if (v == UB_VIEW_LIST) {
        lv_obj_add_flag(s_detail_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_list_root, LV_OBJ_FLAG_HIDDEN);
        audio_fx_play(SND_COLLECT);
        return;
    }

    lv_obj_add_flag(s_list_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(s_detail_root);
    s_card0 = s_card1 = s_card2 = s_card3 = NULL;
    s_chart = NULL;
    s_text_val0 = NULL;
    s_ultra_offset_lbl = NULL;
    s_fail_count = 0;

    lv_obj_t *content = lv_obj_create(s_detail_root);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 320, 184);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bottom = lv_obj_create(s_detail_root);
    lv_obj_remove_style_all(bottom);
    lv_obj_set_size(bottom, 320, 32);
    lv_obj_set_pos(bottom, 0, 184);
    lv_obj_remove_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);

    switch (v) {
        case UB_VIEW_DLIGHT:         build_dlight(content, bottom);         break;
        case UB_VIEW_ULTRASONIC:     build_ultrasonic(content, bottom);     break;
        case UB_VIEW_GESTURE:        build_gesture(content, bottom);        break;
        case UB_VIEW_8ENCODER:       build_8encoder(content, bottom);       break;
        case UB_VIEW_SCD41:          build_scd41(content, bottom);          break;
        case UB_VIEW_CHAIN_ENCODER:  build_chain_encoder(content, bottom);  break;
        case UB_VIEW_CHAIN_JOYSTICK: build_chain_joystick(content, bottom); break;
        default: break;
    }

    lv_obj_remove_flag(s_detail_root, LV_OBJ_FLAG_HIDDEN);
    audio_fx_play(SND_COLLECT);
}

// ── DLight ──────────────────────────────────────────────────────────────────

static void build_dlight(lv_obj_t *content, lv_obj_t *bottom)
{
    s_card0 = ui_value_card_create(content, 8, 8, 140, 80, "Lux", "lx");
    // chart 值域 0~50000(=lux*10,即 0~5000lx):覆盖室内到多云室外;强光直射(>5000lx)
    // 会顶到量程,数值卡本身仍精确显示实际读数,已知限制见 README。
    s_chart = ui_chart_create(content, 156, 8, 156, 150, 80, 0, 50000);
    s_dlight_prev = -1.0f;
    s_log_name = "dlight";
    s_log_cols = "lux";
    make_bottom_bar(bottom, false);
}

static void poll_dlight(void)
{
    if (!ub_scan_attached(UB_KIND_DLIGHT)) {
        bsp_display_lock(0);
        ui_value_card_set_error(s_card0, "未接");
        bsp_display_unlock();
        return;
    }

    float lux;
    esp_err_t err = unit_dlight_read_lux(&lux);
    if (err != ESP_OK) {
        s_fail_count++;
        if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
            ub_scan_mark_lost(UB_KIND_DLIGHT);
            bsp_display_lock(0);
            ui_value_card_set_error(s_card0, "断线");
            bsp_display_unlock();
            haptics_play(HAPTIC_BUMP_HARD);
        }
        return;
    }
    s_fail_count = 0;

    int32_t scaled = (int32_t)(lux * 10.0f);
    if (scaled > 50000) scaled = 50000;
    if (scaled < 0) scaled = 0;

    bsp_display_lock(0);
    ui_value_card_set_value(s_card0, lux, NULL);
    ui_chart_push(s_chart, scaled);
    bsp_display_unlock();

    if (s_logging) data_log_row("%.1f", lux);
    if (s_sleep && (s_dlight_prev < 0.0f || fabsf(lux - s_dlight_prev) > 5.0f)) core2_sleep_kick(s_sleep);
    s_dlight_prev = lux;
}

// ── Ultrasonic ──────────────────────────────────────────────────────────────

static void build_ultrasonic(lv_obj_t *content, lv_obj_t *bottom)
{
    s_card0 = ui_value_card_create(content, 8, 8, 140, 80, "Dist", "mm");
    s_chart = ui_chart_create(content, 156, 8, 156, 150, 80, 0, (int32_t)UNIT_ULTRASONIC_MAX_MM);

    s_ultra_offset_lbl = lv_label_create(content);
    lv_obj_set_style_text_font(s_ultra_offset_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ultra_offset_lbl, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_set_pos(s_ultra_offset_lbl, 8, 92);
    update_ultra_offset_label();

    s_ultra_phase = 0;
    s_ultra_prev = -1.0f;
    s_log_name = "ultrasonic";
    s_log_cols = "mm";
    make_bottom_bar(bottom, true);
}

static void poll_ultrasonic(void)
{
    if (!ub_scan_attached(UB_KIND_ULTRASONIC)) {
        bsp_display_lock(0);
        ui_value_card_set_error(s_card0, "未接");
        bsp_display_unlock();
        s_ultra_phase = 0;
        return;
    }

    if (s_ultra_phase == 0) {
        // 触发与读是两笔独立事务(unit_ultrasonic.h),本帧只发触发,下一帧(~100ms 后,
        // 已超过器件 ~50ms 测量周期)再读结果——两帧一个测量周期≈5Hz,落在 CLAUDE.md §6.6
        // 建议的 2~10Hz 区间内。
        esp_err_t err = unit_ultrasonic_trigger();
        if (err == ESP_OK) {
            s_ultra_phase = 1;
            s_fail_count = 0;
        } else {
            s_fail_count++;
            if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
                ub_scan_mark_lost(UB_KIND_ULTRASONIC);
                bsp_display_lock(0);
                ui_value_card_set_error(s_card0, "断线");
                bsp_display_unlock();
                haptics_play(HAPTIC_BUMP_HARD);
            }
        }
        return;
    }

    float mm;
    esp_err_t err = unit_ultrasonic_read_mm(&mm);
    s_ultra_phase = 0;   // 无论成败都回到"待触发",下一轮重新起一个测量周期

    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        s_fail_count++;
        if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
            ub_scan_mark_lost(UB_KIND_ULTRASONIC);
            bsp_display_lock(0);
            ui_value_card_set_error(s_card0, "断线");
            bsp_display_unlock();
            haptics_play(HAPTIC_BUMP_HARD);
        }
        return;
    }
    s_fail_count = 0;

    // ESP_ERR_NOT_FOUND = 越界/无回波(unit_ultrasonic.h),不算错误——单元仍在,只是暂无
    // 目标,展示为量程上限而不是红字断线。
    float shown = (err == ESP_OK) ? (mm + (float)s_ultra_offset_mm) : UNIT_ULTRASONIC_MAX_MM;
    int32_t scaled = (int32_t)shown;
    if (scaled > (int32_t)UNIT_ULTRASONIC_MAX_MM) scaled = (int32_t)UNIT_ULTRASONIC_MAX_MM;
    if (scaled < 0) scaled = 0;

    bsp_display_lock(0);
    ui_value_card_set_value(s_card0, shown, NULL);
    ui_chart_push(s_chart, scaled);
    bsp_display_unlock();

    if (s_logging) data_log_row("%.1f", shown);
    if (s_sleep && (s_ultra_prev < 0.0f || fabsf(shown - s_ultra_prev) > 3.0f)) core2_sleep_kick(s_sleep);
    s_ultra_prev = shown;
}

// ── Gesture ─────────────────────────────────────────────────────────────────

static void build_gesture(lv_obj_t *content, lv_obj_t *bottom)
{
    s_text_val0 = make_text_card(content, 8, 8, 140, 80, "Last");
    lv_label_set_text(s_text_val0, "(无)");
    s_card1 = ui_value_card_create(content, 156, 8, 140, 80, "Count", "");
    ui_value_card_set_value(s_card1, 0, NULL);
    s_gesture_count = 0;
    s_log_name = "gesture";
    s_log_cols = "gesture_id,gesture_name,count";
    make_bottom_bar(bottom, false);
}

static void poll_gesture(void)
{
    if (!ub_scan_attached(UB_KIND_GESTURE)) {
        bsp_display_lock(0);
        lv_label_set_text(s_text_val0, "未接");
        bsp_display_unlock();
        return;
    }

    gesture_event_t g;
    esp_err_t err = unit_gesture_read(&g);
    if (err != ESP_OK) {
        s_fail_count++;
        if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
            ub_scan_mark_lost(UB_KIND_GESTURE);
            bsp_display_lock(0);
            lv_label_set_text(s_text_val0, "断线");
            bsp_display_unlock();
            haptics_play(HAPTIC_BUMP_HARD);
        }
        return;
    }
    s_fail_count = 0;
    if (g == GESTURE_NONE) return;   // 本帧无新手势,不是错误,不更新画面

    s_gesture_count++;
    bsp_display_lock(0);
    lv_label_set_text(s_text_val0, gesture_name(g));
    ui_value_card_set_value(s_card1, (float)s_gesture_count, NULL);
    bsp_display_unlock();

    if (s_logging) data_log_row("%d,%s,%d", (int)g, gesture_name(g), s_gesture_count);
    if (s_sleep) core2_sleep_kick(s_sleep);   // 挥出一个手势 = 明确的评估动作
}

// ── 8Encoder ────────────────────────────────────────────────────────────────

static void build_8encoder(lv_obj_t *content, lv_obj_t *bottom)
{
    s_card0 = ui_value_card_create(content, 8,   8,  148, 80, "Enc0",   "cnt");
    s_card1 = ui_value_card_create(content, 164, 8,  148, 80, "Enc1",   "cnt");
    s_card2 = ui_value_card_create(content, 8,   96, 148, 80, "BtnCnt", "");
    s_card3 = ui_value_card_create(content, 164, 96, 148, 80, "SW",     "");
    for (int i = 0; i < UNIT_8ENCODER_NUM_ENC; i++) {
        s_enc_total[i] = 0;
        s_enc_prev_btn[i] = false;
    }
    s_enc_prev_sw = false;
    ui_value_card_set_value(s_card0, 0, NULL);
    ui_value_card_set_value(s_card1, 0, NULL);
    ui_value_card_set_value(s_card2, 0, NULL);
    ui_value_card_set_value(s_card3, 0, NULL);
    s_log_name = "8encoder";
    // CSV 导出记录全部 8 路原始增量累计 + 按下数 + 拨动开关,比屏上精选展示的 4 张卡更完整。
    s_log_cols = "enc0,enc1,enc2,enc3,enc4,enc5,enc6,enc7,btncnt,sw";
    make_bottom_bar(bottom, false);
}

static void poll_8encoder(void)
{
    if (!ub_scan_attached(UB_KIND_8ENCODER)) {
        bsp_display_lock(0);
        ui_value_card_set_error(s_card0, "未接");
        ui_value_card_set_error(s_card1, "未接");
        ui_value_card_set_error(s_card2, "未接");
        ui_value_card_set_error(s_card3, "未接");
        bsp_display_unlock();
        return;
    }

    int32_t inc[UNIT_8ENCODER_NUM_ENC];
    bool    btn[UNIT_8ENCODER_NUM_ENC];
    bool    sw = false;
    esp_err_t e1 = unit_8encoder_read_increments(inc);
    esp_err_t e2 = unit_8encoder_read_buttons(btn);
    esp_err_t e3 = unit_8encoder_read_switch(&sw);
    if (e1 != ESP_OK || e2 != ESP_OK || e3 != ESP_OK) {
        s_fail_count++;
        if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
            ub_scan_mark_lost(UB_KIND_8ENCODER);
            bsp_display_lock(0);
            ui_value_card_set_error(s_card0, "断线");
            ui_value_card_set_error(s_card1, "断线");
            ui_value_card_set_error(s_card2, "断线");
            ui_value_card_set_error(s_card3, "断线");
            bsp_display_unlock();
            haptics_play(HAPTIC_BUMP_HARD);
        }
        return;
    }
    s_fail_count = 0;

    bool active = (sw != s_enc_prev_sw);
    int btn_cnt = 0;
    for (int i = 0; i < UNIT_8ENCODER_NUM_ENC; i++) {
        s_enc_total[i] += inc[i];
        if (inc[i] != 0) active = true;
        if (btn[i]) btn_cnt++;
        if (btn[i] != s_enc_prev_btn[i]) active = true;
        s_enc_prev_btn[i] = btn[i];
    }
    s_enc_prev_sw = sw;

    bsp_display_lock(0);
    ui_value_card_set_value(s_card0, (float)s_enc_total[0], NULL);
    ui_value_card_set_value(s_card1, (float)s_enc_total[1], NULL);
    ui_value_card_set_value(s_card2, (float)btn_cnt, NULL);
    ui_value_card_set_value(s_card3, sw ? 1.0f : 0.0f, NULL);
    bsp_display_unlock();

    if (s_logging) {
        data_log_row("%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%d,%d",
                     (long)s_enc_total[0], (long)s_enc_total[1], (long)s_enc_total[2], (long)s_enc_total[3],
                     (long)s_enc_total[4], (long)s_enc_total[5], (long)s_enc_total[6], (long)s_enc_total[7],
                     btn_cnt, sw ? 1 : 0);
    }
    if (s_sleep && active) core2_sleep_kick(s_sleep);
}

// ── SCD41(Unit CO2L:CO₂/温/湿)────────────────────────────────────────────

static void build_scd41(lv_obj_t *content, lv_obj_t *bottom)
{
    // 上排:CO₂ 大数值卡 + CO₂ 趋势 chart;下排:温度卡 + 湿度卡(3 卡 + 1 chart,合 §8 密度)
    s_card0 = ui_value_card_create(content, 8, 8, 140, 80, "CO2", "ppm");
    // chart 值域 0~2000ppm:覆盖室内常见带(新风 ~400、密闭升到 1000+);>2000 会顶到量程,
    // 数值卡本身仍精确显示实际读数(SCD41 量程到 5000),已知限制见 README。
    s_chart = ui_chart_create(content, 156, 8, 156, 80, 80, 0, 2000);
    s_card1 = ui_value_card_create(content, 8,   96, 150, 80, "Temp",  "C");   // °非 ASCII,用 C
    s_card2 = ui_value_card_create(content, 164, 96, 148, 80, "Humid", "%");
    s_scd_prev_co2 = -1;
    s_log_name = "scd41";
    s_log_cols = "co2_ppm,temp_c,rh_pct";
    make_bottom_bar(bottom, false);
}

static void poll_scd41(void)
{
    if (!ub_scan_attached(UB_KIND_SCD41)) {
        bsp_display_lock(0);
        ui_value_card_set_error(s_card0, "未接");
        ui_value_card_set_error(s_card1, "未接");
        ui_value_card_set_error(s_card2, "未接");
        bsp_display_unlock();
        return;
    }

    // 周期模式每 ~5s 才有新数据:先探 data_ready,ready 才 read;两者任一 I2C 失败才计入
    // 拔线判定,"未就绪"(ready=false)不算失败(否则会把在线单元误判成掉线)。
    bool ready = false;
    esp_err_t err = unit_scd41_data_ready(&ready);
    if (err == ESP_OK && ready) {
        uint16_t co2; float tc, rh;
        err = unit_scd41_read(&co2, &tc, &rh);
        if (err == ESP_OK) {
            s_fail_count = 0;

            int32_t co2_clamped = co2;
            if (co2_clamped > 2000) co2_clamped = 2000;

            bsp_display_lock(0);
            ui_value_card_set_value(s_card0, (float)co2, NULL);
            ui_value_card_set_value(s_card1, tc, NULL);
            ui_value_card_set_value(s_card2, rh, NULL);
            ui_chart_push(s_chart, co2_clamped);
            bsp_display_unlock();

            if (s_logging) data_log_row("%u,%.1f,%.1f", (unsigned)co2, tc, rh);
            // CO₂ 变化(如对着单元呼气)= 有人在评估 → 唤醒;首个读数也 kick 一次
            if (s_sleep && (s_scd_prev_co2 < 0 || abs((int)co2 - s_scd_prev_co2) > 30))
                core2_sleep_kick(s_sleep);
            s_scd_prev_co2 = co2;
            return;
        }
    }

    if (err != ESP_OK) {              // data_ready 或 read 的 I2C 失败 → 累计,连续 20 帧判拔线
        s_fail_count++;
        if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
            ub_scan_mark_lost(UB_KIND_SCD41);
            bsp_display_lock(0);
            ui_value_card_set_error(s_card0, "断线");
            ui_value_card_set_error(s_card1, "断线");
            ui_value_card_set_error(s_card2, "断线");
            bsp_display_unlock();
            haptics_play(HAPTIC_BUMP_HARD);
        }
    } else {                          // 通信正常、只是本周期还没新数据:健康,清失败计数
        s_fail_count = 0;
    }
}

// ── Chain Encoder ───────────────────────────────────────────────────────────

static void build_chain_encoder(lv_obj_t *content, lv_obj_t *bottom)
{
    s_card0 = ui_value_card_create(content, 8, 8,  140, 80, "Count",  "cnt");
    s_card1 = ui_value_card_create(content, 8, 96, 140, 80, "Button", "");
    s_chart = ui_chart_create(content, 156, 8, 156, 150, 80, -32768, 32767);
    s_ce_have_prev = false;
    s_log_name = "chain_encoder";
    s_log_cols = "count,button";
    make_bottom_bar(bottom, false);
}

static void poll_chain_encoder(void)
{
    if (!ub_scan_chain_present() || ub_scan_chain_type() != CHAIN_DEV_ENCODER) {
        bsp_display_lock(0);
        ui_value_card_set_error(s_card0, "未接");
        ui_value_card_set_error(s_card1, "未接");
        bsp_display_unlock();
        return;
    }

    int16_t val;
    bool    btn;
    esp_err_t e1 = unit_chain_encoder_read_value(1, &val);
    esp_err_t e2 = unit_chain_encoder_read_button(1, &btn);
    if (e1 != ESP_OK || e2 != ESP_OK) {
        s_fail_count++;
        if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
            ub_scan_mark_chain_lost();
            bsp_display_lock(0);
            ui_value_card_set_error(s_card0, "断线");
            ui_value_card_set_error(s_card1, "断线");
            bsp_display_unlock();
            haptics_play(HAPTIC_BUMP_HARD);
        }
        return;
    }
    s_fail_count = 0;

    bsp_display_lock(0);
    ui_value_card_set_value(s_card0, (float)val, NULL);
    ui_value_card_set_value(s_card1, btn ? 1.0f : 0.0f, NULL);
    ui_chart_push(s_chart, (int32_t)val);
    bsp_display_unlock();

    if (s_logging) data_log_row("%d,%d", (int)val, btn ? 1 : 0);

    bool active = (!s_ce_have_prev) || (val != s_ce_prev_value) || (btn != s_ce_prev_btn);
    s_ce_prev_value = val;
    s_ce_prev_btn   = btn;
    s_ce_have_prev  = true;
    if (s_sleep && active) core2_sleep_kick(s_sleep);
}

// ── Chain Joystick ──────────────────────────────────────────────────────────

static int norm_adc(uint16_t raw, uint16_t center)
{
    int d = (int)raw - (int)center;
    int v = d * 100 / 2048;   // 12-bit ADC 半量程≈2048,归一化到 -100..100
    if (v > 100) v = 100;
    if (v < -100) v = -100;
    return v;
}

static void build_chain_joystick(lv_obj_t *content, lv_obj_t *bottom)
{
    s_card0 = ui_value_card_create(content, 8,   8,  148, 80, "X",     "%");
    s_card1 = ui_value_card_create(content, 164, 8,  148, 80, "Y",     "%");
    s_card2 = ui_value_card_create(content, 8,   96, 148, 80, "Z Btn", "");

    // 个体有零偏(硬件文档 §6):进页时采一次居中值做软件归中,读失败就退回几何中值 2048。
    uint16_t cx, cy;
    if (unit_chain_joystick_read_adc(1, &cx, &cy) == ESP_OK) {
        s_cj_center_x = cx;
        s_cj_center_y = cy;
    } else {
        s_cj_center_x = 2048;
        s_cj_center_y = 2048;
    }
    s_cj_prev_x = s_cj_prev_y = 0;
    s_cj_prev_btn = false;
    s_log_name = "chain_joystick";
    s_log_cols = "x_raw,y_raw,x_norm,y_norm,button";
    make_bottom_bar(bottom, false);
}

static void poll_chain_joystick(void)
{
    if (!ub_scan_chain_present() || ub_scan_chain_type() != CHAIN_DEV_JOYSTICK) {
        bsp_display_lock(0);
        ui_value_card_set_error(s_card0, "未接");
        ui_value_card_set_error(s_card1, "未接");
        ui_value_card_set_error(s_card2, "未接");
        bsp_display_unlock();
        return;
    }

    uint16_t x, y;
    bool     btn;
    esp_err_t e1 = unit_chain_joystick_read_adc(1, &x, &y);
    esp_err_t e2 = unit_chain_joystick_read_button(1, &btn);
    if (e1 != ESP_OK || e2 != ESP_OK) {
        s_fail_count++;
        if (s_fail_count >= UB_FAIL_STREAK_LIMIT) {
            ub_scan_mark_chain_lost();
            bsp_display_lock(0);
            ui_value_card_set_error(s_card0, "断线");
            ui_value_card_set_error(s_card1, "断线");
            ui_value_card_set_error(s_card2, "断线");
            bsp_display_unlock();
            haptics_play(HAPTIC_BUMP_HARD);
        }
        return;
    }
    s_fail_count = 0;

    int nx = norm_adc(x, s_cj_center_x);
    int ny = norm_adc(y, s_cj_center_y);

    bsp_display_lock(0);
    ui_value_card_set_value(s_card0, (float)nx, NULL);
    ui_value_card_set_value(s_card1, (float)ny, NULL);
    ui_value_card_set_value(s_card2, btn ? 1.0f : 0.0f, NULL);
    bsp_display_unlock();

    if (s_logging) data_log_row("%u,%u,%d,%d,%d", x, y, nx, ny, btn ? 1 : 0);

    bool active = (abs(nx - s_cj_prev_x) > 5) || (abs(ny - s_cj_prev_y) > 5) || (btn != s_cj_prev_btn);
    s_cj_prev_x = nx;
    s_cj_prev_y = ny;
    s_cj_prev_btn = btn;
    if (s_sleep && active) core2_sleep_kick(s_sleep);
}

// ── 公共入口 ────────────────────────────────────────────────────────────────

void unit_bench_ui_start(void)
{
    s_power_ready = (power_monitor_init() == ESP_OK);
    if (!s_power_ready) ESP_LOGW(TAG, "power_monitor 初始化失败,状态栏电量不可用");

    kv_store_get_i32("ultra_offset", &s_ultra_offset_mm, 0);

    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1E2126), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_status_bar = ui_status_bar_create(scr, "unit_bench");

    s_list_root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list_root);
    lv_obj_set_size(s_list_root, 320, 216);
    lv_obj_set_pos(s_list_root, 0, 24);
    lv_obj_remove_flag(s_list_root, LV_OBJ_FLAG_SCROLLABLE);
    build_list_ui(s_list_root);

    s_detail_root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_detail_root);
    lv_obj_set_size(s_detail_root, 320, 216);
    lv_obj_set_pos(s_detail_root, 0, 24);
    lv_obj_remove_flag(s_detail_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_detail_root, LV_OBJ_FLAG_HIDDEN);

    bsp_display_unlock();

    // 首次扫描:开机就把列表填上,不必等第一个 2s 后台周期
    ub_scan_result_t r;
    ub_scan_run(&r, 300);
    bsp_display_lock(0);
    rebuild_list_rows(&r);
    bsp_display_unlock();

    int64_t now_ms = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_last_scan_ms   = now_ms;
    s_last_status_ms = now_ms;
    ESP_LOGI(TAG, "UI 就绪,首扫:%d 个 PORT.A 地址,Chain %s", r.row_count,
             r.chain_present ? "在位" : "未接");
}

void unit_bench_ui_tick(int64_t now_ms, core2_sleep_t *sleep)
{
    s_sleep = sleep;

    if (now_ms - s_last_scan_ms >= 2000) {
        s_last_scan_ms = now_ms;
        ub_scan_result_t r;
        ub_scan_run(&r, 300);   // 纯驱动调用,不碰 LVGL,不需要锁
        bsp_display_lock(0);
        rebuild_list_rows(&r);   // 即使当前在详情页(list_root 隐藏)也顺带刷新,保证返回列表时是最新的
        bsp_display_unlock();
    }

    if (now_ms - s_last_status_ms >= 1000) {
        s_last_status_ms = now_ms;
        int  batt_pct = -1;
        bool vbus = false;
        if (s_power_ready) {
            power_telemetry_t t;
            if (power_monitor_read(&t) == ESP_OK) {
                batt_pct = batt_mv_to_pct(t.batt_mv);
                vbus = t.vbus_present;
            }
        }
        bsp_display_lock(0);
        ui_status_bar_update(s_status_bar, (uint32_t)(now_ms / 1000), batt_pct, vbus);
        bsp_display_unlock();
    }

    switch (s_view) {
        case UB_VIEW_LIST: break;   // 列表行文字已在上面的周期扫描里更新,这里无需再做什么
        case UB_VIEW_DLIGHT:         poll_dlight();         break;
        case UB_VIEW_ULTRASONIC:     poll_ultrasonic();     break;
        case UB_VIEW_GESTURE:        poll_gesture();        break;
        case UB_VIEW_8ENCODER:       poll_8encoder();       break;
        case UB_VIEW_SCD41:          poll_scd41();          break;
        case UB_VIEW_CHAIN_ENCODER:  poll_chain_encoder();  break;
        case UB_VIEW_CHAIN_JOYSTICK: poll_chain_joystick(); break;
    }
}
