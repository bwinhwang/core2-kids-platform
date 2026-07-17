// power_lab_ui —— LVGL 视图层实现
//
// 屏幕结构(320×240):顶 24px `ui_status_bar` 常驻;中间 192px 是 page1_root(遥测+负载
// 矩阵)/ page2_root(休眠演练+续航+录制)两个互斥显示的容器(HIDDEN flag 切换,不整屏
// 重绘——CLAUDE.md §6);底 24px 是常驻的翻页按钮条。两页内容都在 power_lab_ui_start()
// 建一次(静态层),之后只有 power_lab_ui_tick() 里改动态部分(数值/chart/行文字)。
//
// 加锁纪律:仿 unit_bench_ui.c 的既有约定——读传感器(power_monitor 走 AXP192 I2C)在
// bsp_display_lock 之外做,只在真正改 LVGL 对象的那几行短暂加锁;LVGL 事件回调本身跑在
// LVGL 任务上下文,直接调 LVGL API 不需要再包一层锁。
//
// 休眠演练纪律:power_lab_ui_tick 一进来就检查 ctl->drill_stage,非 IDLE 时直接返回,
// 不碰任何 LVGL 对象——DEEP 演练期间背光/5V 已断,NAP 演练期间背光已降,都没必要也不该
// 刷屏(任务说明原话)。演练开始的提示文字改在“请求”那一刻(见 on_nap_click/on_deep_click)
// 写一次,靠 power_lab_ctl 的两段式请求(drill_pending → 下一轮 tick 才真正 force_stage)
// 把这次改动让给 LVGL 至少一次调度机会去刷到屏上,再断电。

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#include "audio_fx.h"
#include "power_monitor.h"
#include "ui_kit.h"

#include "power_lab_ui.h"

static const char *TAG = "pl_ui";

typedef enum { PL_PAGE_1 = 0, PL_PAGE_2 } pl_page_t;

// ── 屏幕结构 ────────────────────────────────────────────────────────────────
static pl_ctl_t        *s_ctl;
static ui_status_bar_t *s_status_bar;
static lv_obj_t *s_page1_root, *s_page2_root;
static lv_obj_t *s_nav_lbl;
static pl_page_t s_page = PL_PAGE_1;

static int64_t s_last_status_ms;

// ── page1:遥测 + 负载矩阵 ───────────────────────────────────────────────────
static ui_list_menu_t  *s_list_menu;
static lv_obj_t        *s_chg_status_lbl;
static ui_value_card_t *s_card_battv, *s_card_batti;
static lv_obj_t        *s_vbus_val_lbl;
static lv_obj_t        *s_chart_src_lbl;
static ui_chart_t      *s_chart;

#define ROW_BACKLIGHT 0
#define ROW_LED       1
#define ROW_EXTEN     2
#define ROW_AUDIO     3
#define ROW_HAPTIC    4
#define ROW_CPU       5

// ── page2:休眠演练 + 续航 + 录制 ────────────────────────────────────────────
static lv_obj_t        *s_rec_btn_lbl;
static lv_obj_t        *s_drill_status_lbl;
static ui_value_card_t *s_card_avg, *s_card_dur, *s_card_end;
static lv_obj_t        *s_rec_status_lbl;

// ── 小工具(仿 unit_bench_ui.c 同名函数)─────────────────────────────────────

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

// 手写一张"文字值"卡片(同 unit_bench_ui.c 的 make_text_card):标签+大字面板,但显示任意
// 文字而非经 ui_value_card_set_value 格式化的数字——VBUS 卡要同时展示 mV 和 mA 两个数,
// 拼成一行文字比拆两张卡省屏幕(page1 密度已经比较满)。
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
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);   // 组合文字较长,16px 而非 24px
    lv_obj_set_style_text_color(val, lv_color_hex(UI_KIT_COLOR_VALUE), 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 6, -4);

    return val;
}

// ── page1:负载矩阵行文字(与 pl_ctl_t 状态保持一致,build 时和每次点击后都调)──────

static void refresh_row_backlight(void)
{
    char buf[24];
    int pct = pl_ctl_backlight_pct(s_ctl);
    snprintf(buf, sizeof(buf), "背光:%d%%%s", pct, pct == 0 ? "(暗)" : "");
    ui_list_menu_set_row_text(s_list_menu, ROW_BACKLIGHT, buf);
}

static void refresh_row_led(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "灯带:%d", pl_ctl_led_level(s_ctl));
    ui_list_menu_set_row_text(s_list_menu, ROW_LED, buf);
}

static void refresh_row_exten(void)
{
    ui_list_menu_set_row_text(s_list_menu, ROW_EXTEN, s_ctl->exten_on ? "5V EXTEN:ON" : "5V EXTEN:OFF");
}

static void refresh_row_cpu(void)
{
    if (!s_ctl->cpu_pm_available) {
        ui_list_menu_set_row_text(s_list_menu, ROW_CPU, "CPU锁频:不可用");
        return;
    }
    ui_list_menu_set_row_text(s_list_menu, ROW_CPU,
        s_ctl->cpu_mode == PL_CPU_FIXED_240 ? "CPU:240定" : "CPU:自动80-240");
}

// ── page1:点击回调 ──────────────────────────────────────────────────────────

static void on_row_click(int row_idx, void *user_data)
{
    (void)user_data;
    switch (row_idx) {
        case ROW_BACKLIGHT:
            pl_ctl_cycle_backlight(s_ctl);
            refresh_row_backlight();
            break;
        case ROW_LED:
            pl_ctl_cycle_led(s_ctl);
            refresh_row_led();
            break;
        case ROW_EXTEN: {
            bool on = ui_list_menu_get_switch(s_list_menu, ROW_EXTEN);
            pl_ctl_set_exten(s_ctl, on);
            refresh_row_exten();
            break;
        }
        case ROW_AUDIO:
            pl_ctl_test_audio();
            break;
        case ROW_HAPTIC:
            pl_ctl_test_haptic();
            break;
        case ROW_CPU:
            pl_ctl_cycle_cpu(s_ctl);
            refresh_row_cpu();
            break;
        default:
            break;
    }
}

// ── page1 建页 ──────────────────────────────────────────────────────────────

static void build_page1(lv_obj_t *root)
{
    s_list_menu = ui_list_menu_create(root, 0, 0, 132, 192);
    ui_list_menu_add_row(s_list_menu, "背光:60%", false);   // ROW_BACKLIGHT
    ui_list_menu_add_row(s_list_menu, "灯带:0", false);     // ROW_LED
    ui_list_menu_add_row(s_list_menu, "5V EXTEN:OFF", true); // ROW_EXTEN(带 switch,默认不勾选)
    ui_list_menu_add_row(s_list_menu, "喇叭测试音", false);  // ROW_AUDIO(瞬时触发)
    ui_list_menu_add_row(s_list_menu, "震动测试", false);    // ROW_HAPTIC(瞬时触发)
    ui_list_menu_add_row(s_list_menu, "CPU:240定", false);   // ROW_CPU
    ui_list_menu_on_click(s_list_menu, on_row_click, NULL);

    s_chg_status_lbl = lv_label_create(root);
    lv_label_set_text(s_chg_status_lbl, "状态:--");
    lv_obj_set_style_text_font(s_chg_status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_chg_status_lbl, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_set_pos(s_chg_status_lbl, 136, 0);

    s_card_battv = ui_value_card_create(root, 136, 16, 88, 60, "BattV", "mV");
    s_card_batti = ui_value_card_create(root, 228, 16, 88, 60, "BattI", "mA");

    s_vbus_val_lbl = make_text_card(root, 136, 80, 180, 40, "VBUS");

    s_chart_src_lbl = lv_label_create(root);
    lv_label_set_text(s_chart_src_lbl, "trend");
    lv_obj_set_style_text_font(s_chart_src_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_chart_src_lbl, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_set_pos(s_chart_src_lbl, 136, 122);

    // chart 值域:兼顾电池净电流(充-放,大致 -500..+300mA)与 VBUS 电流(充电+跑负载,
    // 可能到 1~2A)两种来源,取一个都能容纳的粗略范围——量级对不对需要实机核实(README
    // 已记入待点检清单),不是精确刻度。
    s_chart = ui_chart_create(root, 136, 136, 180, 56, 60, -500, 2000);

    refresh_row_backlight();
    refresh_row_led();
    refresh_row_exten();
    refresh_row_cpu();
}

// ── page2:点击回调 ──────────────────────────────────────────────────────────

// 点击后立即(还在 LVGL 任务上下文里)把提示文字画出来,再设请求位——真正的
// core2_sleep_force_stage 推到下一轮 pl_ctl_tick(app_task 主循环上下文)才执行,让这行
// 文字有机会被 LVGL 自己的渲染任务实际刷到屏上(见文件头注释「休眠演练纪律」)。
static void on_nap_click(lv_event_t *e)
{
    (void)e;
    if (s_ctl->drill_stage != PL_DRILL_IDLE || s_ctl->drill_pending != PL_DRILL_IDLE) return;
    lv_label_set_text(s_drill_status_lbl, "演练:NAP 中…");
    pl_ctl_request_drill(s_ctl, PL_DRILL_NAP);
    audio_fx_play(SND_BUMP_LIGHT);
}

static void on_deep_click(lv_event_t *e)
{
    (void)e;
    if (s_ctl->drill_stage != PL_DRILL_IDLE || s_ctl->drill_pending != PL_DRILL_IDLE) return;
    lv_label_set_text(s_drill_status_lbl, "演练:DEEP 中(屏将熄灭)…");
    pl_ctl_request_drill(s_ctl, PL_DRILL_DEEP);
    audio_fx_play(SND_BUMP_LIGHT);
}

static void on_coulomb_reset_click(lv_event_t *e)
{
    (void)e;
    power_monitor_coulomb_reset();
    audio_fx_play(SND_COLLECT);
}

static void on_rec_toggle_click(lv_event_t *e)
{
    (void)e;
    if (s_ctl->rec_active) pl_ctl_request_rec_stop(s_ctl);
    else                    pl_ctl_request_rec_start(s_ctl);
}

static void on_dump_click(lv_event_t *e)
{
    (void)e;
    pl_ctl_request_rec_dump(s_ctl);
}

// ── page2 建页 ──────────────────────────────────────────────────────────────

static void build_page2(lv_obj_t *root)
{
    make_btn(root, 0, 0,   132, 30, "演练 NAP",   on_nap_click,           NULL);
    make_btn(root, 0, 32,  132, 30, "演练 DEEP",  on_deep_click,          NULL);
    make_btn(root, 0, 64,  132, 30, "库仑计复位",  on_coulomb_reset_click, NULL);
    make_btn(root, 0, 96,  132, 30, "录制:开始",  on_rec_toggle_click,    &s_rec_btn_lbl);
    make_btn(root, 0, 128, 132, 30, "Dump 导出",  on_dump_click,          NULL);

    s_drill_status_lbl = lv_label_create(root);
    lv_label_set_text(s_drill_status_lbl, "演练:空闲");
    lv_obj_set_style_text_font(s_drill_status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_drill_status_lbl, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_set_pos(s_drill_status_lbl, 0, 164);
    lv_obj_set_width(s_drill_status_lbl, 132);

    s_card_avg = ui_value_card_create(root, 136, 0,   184, 54, "回放均值电流", "mA");
    s_card_dur = ui_value_card_create(root, 136, 58,  184, 54, "回放时长",     "s");
    s_card_end = ui_value_card_create(root, 136, 116, 184, 54, "续航估算",     "h");

    s_rec_status_lbl = lv_label_create(root);
    lv_label_set_text(s_rec_status_lbl, "未录制");
    lv_obj_set_style_text_font(s_rec_status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_rec_status_lbl, lv_color_hex(UI_KIT_COLOR_LABEL), 0);
    lv_obj_set_pos(s_rec_status_lbl, 136, 174);
}

// ── 翻页 ────────────────────────────────────────────────────────────────────

static void on_nav_click(lv_event_t *e)
{
    (void)e;
    s_page = (s_page == PL_PAGE_1) ? PL_PAGE_2 : PL_PAGE_1;
    if (s_page == PL_PAGE_1) {
        lv_obj_remove_flag(s_page1_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page2_root, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_nav_lbl, "Page 2 >>");
    } else {
        lv_obj_add_flag(s_page1_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_page2_root, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_nav_lbl, "<< Page 1");
    }
    audio_fx_play(SND_COLLECT);
}

// ── 公共入口 ────────────────────────────────────────────────────────────────

void power_lab_ui_start(pl_ctl_t *ctl)
{
    s_ctl = ctl;

    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1E2126), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_status_bar = ui_status_bar_create(scr, "power_lab");

    s_page1_root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_page1_root);
    lv_obj_set_size(s_page1_root, 320, 192);
    lv_obj_set_pos(s_page1_root, 0, 24);
    lv_obj_remove_flag(s_page1_root, LV_OBJ_FLAG_SCROLLABLE);
    build_page1(s_page1_root);

    s_page2_root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_page2_root);
    lv_obj_set_size(s_page2_root, 320, 192);
    lv_obj_set_pos(s_page2_root, 0, 24);
    lv_obj_remove_flag(s_page2_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page2_root, LV_OBJ_FLAG_HIDDEN);
    build_page2(s_page2_root);

    lv_obj_t *nav = lv_obj_create(scr);
    lv_obj_remove_style_all(nav);
    lv_obj_set_size(nav, 320, 24);
    lv_obj_set_pos(nav, 0, 216);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    make_btn(nav, 84, 0, 152, 22, "Page 2 >>", on_nav_click, &s_nav_lbl);

    bsp_display_unlock();

    int64_t now_ms = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_last_status_ms = now_ms;
    ESP_LOGI(TAG, "UI 就绪");
}

void power_lab_ui_tick(pl_ctl_t *ctl, int64_t now_ms)
{
    // 休眠演练进行中(NAP 已降背光 / DEEP 已断背光+5V):绝不碰 LVGL,详见文件头注释。
    if (ctl->drill_stage != PL_DRILL_IDLE) return;

    if (now_ms - s_last_status_ms >= 1000) {
        s_last_status_ms = now_ms;

        int  batt_pct = -1;
        bool vbus = false;
        if (ctl->telem_valid) {
            batt_pct = batt_mv_to_pct(ctl->telem.batt_mv);
            vbus     = ctl->telem.vbus_present;
        }

        char chg_buf[24];
        if (!ctl->telem_valid)          snprintf(chg_buf, sizeof(chg_buf), "状态:遥测未就绪");
        else if (ctl->telem.charging)   snprintf(chg_buf, sizeof(chg_buf), "状态:充电中");
        else if (ctl->telem.vbus_present) snprintf(chg_buf, sizeof(chg_buf), "状态:USB供电");
        else                             snprintf(chg_buf, sizeof(chg_buf), "状态:放电中");

        char vbus_buf[40];
        if (!ctl->telem_valid) {
            snprintf(vbus_buf, sizeof(vbus_buf), "--");
        } else if (!ctl->telem.vbus_present) {
            snprintf(vbus_buf, sizeof(vbus_buf), "未接");
        } else {
            snprintf(vbus_buf, sizeof(vbus_buf), "%dmV %dmA", ctl->telem.vbus_mv, ctl->telem.vbus_ma);
        }

        int32_t chart_val = (int32_t)ctl->chart_ma_smoothed;
        if (chart_val < -500) chart_val = -500;
        if (chart_val > 2000) chart_val = 2000;

        bsp_display_lock(0);
        ui_status_bar_update(s_status_bar, (uint32_t)(now_ms / 1000), batt_pct, vbus);
        lv_label_set_text(s_chg_status_lbl, chg_buf);
        if (ctl->telem_valid) {
            ui_value_card_set_value(s_card_battv, (float)ctl->telem.batt_mv, NULL);
            ui_value_card_set_value(s_card_batti,
                (float)(ctl->telem.batt_charge_ma - ctl->telem.batt_discharge_ma), NULL);
        } else {
            ui_value_card_set_error(s_card_battv, "未就绪");
            ui_value_card_set_error(s_card_batti, "未就绪");
        }
        lv_label_set_text(s_vbus_val_lbl, vbus_buf);
        lv_label_set_text(s_chart_src_lbl, ctl->chart_from_vbus ? "trend(vbus mA)" : "trend(batt mA)");
        ui_chart_push(s_chart, chart_val);

        // page2 的续航/录制状态即使当前不在 page2 也顺带刷新,保证翻页回来时是最新的
        // (与 unit_bench_ui.c 列表页周期扫描的既有惯例一致)。
        float hrs = pl_ctl_endurance_hours(ctl);
        if (hrs >= 0.0f) ui_value_card_set_value(s_card_end, hrs, NULL);
        else             ui_value_card_set_error(s_card_end, ctl->telem_valid && ctl->telem.charging ? "充电中" : "N/A");

        if (ctl->drill_have_result) {
            ui_value_card_set_value(s_card_avg, ctl->drill_avg_ma, NULL);
            ui_value_card_set_value(s_card_dur, (float)ctl->drill_duration_ms / 1000.0f, NULL);
            char drill_buf[32];
            snprintf(drill_buf, sizeof(drill_buf), "演练:上次%s完成",
                     ctl->drill_last_stage == PL_DRILL_NAP ? "NAP" : "DEEP");
            lv_label_set_text(s_drill_status_lbl, drill_buf);
        } else if (ctl->drill_stage == PL_DRILL_IDLE && ctl->drill_pending == PL_DRILL_IDLE) {
            // 这个分支只在"从未演练过"时命中一次(有结果后走上面 if);演练刚请求还没被
            // ctl_tick 应用的那一瞬间不会落到这里,见 app_main.c 里 ctl_tick 先于本函数调用
            // 的顺序保证(文件头注释「休眠演练纪律」)。
            lv_label_set_text(s_drill_status_lbl, "演练:空闲");
        }

        lv_label_set_text(s_rec_status_lbl, ctl->rec_status);
        if (s_rec_btn_lbl) lv_label_set_text(s_rec_btn_lbl, ctl->rec_active ? "录制:停止" : "录制:开始");
        bsp_display_unlock();
    }
}
