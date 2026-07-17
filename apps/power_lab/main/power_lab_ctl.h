// power_lab_ctl —— model 层:负载开关矩阵 / 遥测快照 / 休眠演练 / 续航估算 / 离线录制
//
// 职责边界(仿 apps/unit_bench 的 scan/ui 三层拆分,见其 README「代码结构」):本文件
// 不碰 LVGL,只做"读传感器 → 维护状态 → 应用负载开关动作"这些纯逻辑;真正的画面在
// power_lab_ui.c 里,每帧读本文件暴露的只读状态渲染,点击回调再反过来调本文件的
// pl_ctl_* 动作函数。
//
// 关键设计取舍(详见 apps/power_lab/README.md「关键设计取舍」,这里只记结论):
//   1. power_lab 不跑 core2_sleep 的自动闲置省电(CLAUDE.md §7 明确 power_lab 是省电
//      纪律的例外——它本身就是评估功耗的工具),只用 core2_sleep_force_stage() 做
//      "手动演练"入口,主循环不调 core2_sleep_feed()。
//   2. CPU 锁频用 esp_pm_configure() 做"240MHz 恒定" vs "80~240MHz 自动调频"两档,
//      **不开 light_sleep_enable**(避开 FreeRTOS tickless idle 与 PSRAM light-sleep
//      漏电流规避选项的一整套额外配置面,查证结论见 README)。
//   3. SPIFFS 挂载/格式化是阻塞调用,不能在 LVGL 回调里直接跑——本文件的 rec 相关函数
//      用"请求位 + tick 里执行"两段式解耦,UI 层只管设请求位和读状态文字。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core2_sleep.h"
#include "power_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── 休眠演练 ────────────────────────────────────────────────────────────────
typedef enum {
    PL_DRILL_IDLE = 0,   // 未在演练,UI 正常刷新
    PL_DRILL_NAP,        // 演练 NAP 中:仍在采样,UI 不刷新(§7 三条固化顺序里背光会降,没必要画)
    PL_DRILL_DEEP,       // 演练 DEEP 中:屏/灯带/5V 已断,UI 绝不能碰(bsp_display 状态未定义)
} pl_drill_stage_t;

// CPU 锁频两档(仅在 pl_ctl_init 探测到 esp_pm_configure 可用时生效,见 cpu_pm_available)
typedef enum {
    PL_CPU_FIXED_240 = 0,   // {max=240,min=240,light_sleep=false}:恒定最高频
    PL_CPU_AUTO_DFS,        // {max=240,min=80, light_sleep=false}:按负载自动降频,不开自动轻睡眠
} pl_cpu_mode_t;

typedef struct {
    // ── 遥测快照(1Hz 刷新,pl_ctl_tick 内部维护;telem_valid=false 表示还没成功读到过)──
    power_telemetry_t telem;
    bool              telem_valid;
    float             chart_ma_smoothed;   // 供 chart 用的滑动平均电流(mA,含正负号)
    bool              chart_from_vbus;     // true=当前 smoothed 值取自 VBUS 电流,false=取自电池净电流

    // ── 负载开关矩阵状态(仅记录"当前挡位",UI 据此渲染行文字)────────────────────
    int  backlight_idx;   // 0..3 → {0,10,60,100} %
    int  led_idx;         // 0..2 → {0,48,255}
    bool exten_on;
    pl_cpu_mode_t cpu_mode;
    bool cpu_pm_available;   // false = esp_pm_configure 探测失败,CPU 锁频行在 UI 上显式禁用

    // ── 休眠演练 ────────────────────────────────────────────────────────────
    pl_drill_stage_t drill_pending;    // UI 点击按钮时先设这个(见 pl_ctl_request_drill),
                                       // 真正的 force_stage 推到下一轮 pl_ctl_tick 才做——
                                       // 让 UI 先画出的"NAP/DEEP 演练中…"提示有机会被 LVGL
                                       // 渲染任务实际刷到屏上(否则本回调线程内紧接着调
                                       // force_stage 断背光,提示文字很可能来不及显示)。
    pl_drill_stage_t drill_stage;
    int64_t          drill_start_ms;   // 本次演练开始时间(算实际时长用)
    int64_t          drill_end_ms;     // 演练自动结束时间(force_stage 定时唤醒用)
    bool              drill_have_result;
    pl_drill_stage_t  drill_last_stage;
    float             drill_avg_ma;
    int               drill_duration_ms;
    int               drill_sample_n;

    // ── 续航估算 ────────────────────────────────────────────────────────────
    // 用最近一次 telem.batt_discharge_ma 做线性外推,见 pl_ctl_endurance_hours()。

    // ── SPIFFS 离线录制(两段式:UI 设请求位,pl_ctl_tick 里执行阻塞操作)───────────
    bool rec_active;
    bool rec_pending_start;
    bool rec_pending_dump;
    char rec_status[56];   // UI 状态文字,如 "未录制" / "初始化存储中…" / "录制中" / "导出完成"

    core2_sleep_t *sleep;   // core2_sleep_force_stage 演练用的状态结构(外部拥有,本结构只持指针)
    int64_t last_telem_ms;
    int64_t last_drill_sample_ms;
} pl_ctl_t;

/** @brief 初始化:power_monitor_init(须在 core2_power_init 之后,由 app_main 保证顺序)+
 *         探测 esp_pm_configure 是否可用(CONFIG_PM_ENABLE 未开时返回 ESP_ERR_NOT_SUPPORTED,
 *         对应降级 cpu_pm_available=false)+ 记录负载矩阵的初始基线状态。
 *  @param sleep 外部已 core2_sleep_init 过的状态结构指针,仅供 force_stage 演练用。 */
void pl_ctl_init(pl_ctl_t *c, core2_sleep_t *sleep);

/** @brief 主循环每帧调用一次:1Hz 遥测轮询 + 休眠演练采样/收尾 + 录制请求位执行。
 *  不碰 LVGL,可以在任意锁状态下调用。 */
void pl_ctl_tick(pl_ctl_t *c, int64_t now_ms);

// ── 负载开关矩阵动作(UI 点击回调里调用)────────────────────────────────────────
void pl_ctl_cycle_backlight(pl_ctl_t *c);
void pl_ctl_cycle_led(pl_ctl_t *c);
void pl_ctl_set_exten(pl_ctl_t *c, bool on);
void pl_ctl_cycle_cpu(pl_ctl_t *c);
void pl_ctl_test_audio(void);
void pl_ctl_test_haptic(void);

// 挡位数值只读访问(UI 渲染行文字用;挡位表本身是 power_lab_ctl.c 的私有数组,单一真源)
int pl_ctl_backlight_pct(const pl_ctl_t *c);
int pl_ctl_led_level(const pl_ctl_t *c);

// ── 休眠演练 ────────────────────────────────────────────────────────────────
/** @brief UI 点击"演练 NAP/DEEP"按钮时调(stage 只接受 PL_DRILL_NAP/PL_DRILL_DEEP)。
 *  非阻塞、不碰硬件——只记一个"待处理"标志,真正调用 core2_sleep_force_stage 的动作
 *  推迟到下一轮 pl_ctl_tick(见该函数与头部注释)。演练中或已有一个待处理请求时,
 *  重复调用会被忽略。 */
void pl_ctl_request_drill(pl_ctl_t *c, pl_drill_stage_t stage);

// ── 续航估算 ────────────────────────────────────────────────────────────────
/** @brief mAh(500,平台常量,Bottom2 电池容量)/ 放电电流 mA = 剩余小时数。
 *  @return >=0 正常估算值;<0 表示当前没有有效放电电流可供估算(充电中/未知/接近 0)。 */
float pl_ctl_endurance_hours(const pl_ctl_t *c);

// ── SPIFFS 离线录制(两段式请求,详见 power_lab_ctl.c 与 components/data_log/README.md)──
void pl_ctl_request_rec_start(pl_ctl_t *c);
void pl_ctl_request_rec_stop(pl_ctl_t *c);
void pl_ctl_request_rec_dump(pl_ctl_t *c);

#ifdef __cplusplus
}
#endif
