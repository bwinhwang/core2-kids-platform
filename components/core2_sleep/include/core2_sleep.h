// core2_sleep —— 两级省电编排器(打盹 → 深度省电 → 去抖唤醒)
//
// 电池只有底座 500mAh,幼儿应用**必做** idle 省电。本组件把实机调通的整套编排固化:
//
//   AWAKE ──(可打盹状态下机身静止 nap_after_ms)──► NAP 打盹(降亮 + 灯带慢呼吸)
//         ──(再静止 deep_after_ms)──► DEEP 深度省电:
//              亮度0 → 断 DCDC3(真黑屏,brightness 0% 不熄屏!)→ 灯带熄 → 切 M-Bus 5V
//              → 轮询降频(feed 返回值变成 deep_poll_ms,少唤醒 CPU)
//         ──(连续多帧明显动作,去抖)──► 唤醒:
//              恢复 5V → 重启 DCDC3 → 恢复亮度 → 灯带回常态 →(可选)轻震
//         ──(DEEP 里再耗 deep_shutdown_ms)──► **自动关机**(AXP192 断电)
//
// 🔴 DEEP **不是终点站**(2026-08-12 补):DEEP 省的全是外设电(屏/灯带/5V),主控仍在
// 160MHz 全速跑,没有 light sleep、没有动态调频 —— 光靠 DEEP,设备会一路耗到电池放空
// (500mAh 电芯还每次都是深放电)。所以久置最终必须**真关机**,这是本组件唯一能把待机
// 功耗归零的手段。插着 USB 时不关机(开发/充电场景)。
//
// 上面**每一步的顺序都是实机踩坑的结论**(详见 core2_power / motion_detect 的 README
// 与 CLAUDE.md §20.6),别在应用里手工重排。判"没人玩"只看机身动作量(motion_detect),
// 绝不要用应用量(球速/游标)——会永不打盹。
//
// 用法(应用主循环,60Hz):
//   static core2_sleep_t sl;
//   core2_sleep_init(&sl, NULL);                        // NULL = 实机定案默认值
//   循环每帧:
//     int delay_ms = core2_sleep_feed(&sl,
//                        have ? (float[]){ax,ay,az} : NULL,
//                        in_gameplay && have);           // 只有"正玩着"的状态可进打盹
//     if (core2_sleep_stage(&sl) == CORE2_SLEEP_AWAKE) { ...跑应用逻辑... }
//     vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));   // DEEP 时自动降频
#pragma once

#include <stdbool.h>
#include "motion_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CORE2_SLEEP_AWAKE = 0,   // 清醒:应用正常跑
    CORE2_SLEEP_NAP,         // 打盹:降亮省电,应用应暂停画面更新
    CORE2_SLEEP_DEEP,        // 深度省电:屏/灯带全灭、5V 已切、轮询降频
} core2_sleep_stage_t;

typedef struct {
    int   nap_after_ms;      // 静止多久进打盹(默认 12000)
    int   deep_after_ms;     // 打盹后再静止多久进深度省电(默认 60000)
    int   awake_brightness;  // 清醒背光 %(默认 60;可运行时改,见 set_awake_brightness)
    int   nap_brightness;    // 打盹背光 %(默认 10)
    int   frame_ms;          // 清醒/打盹的帧周期(默认 16 ≈ 60Hz,用于 ms→帧换算)
    int   deep_poll_ms;      // 深度省电轮询周期(默认 120)
    float wake_thresh;       // 明显动作阈值 g,0=实测默认 0.12
    int   wake_frames;       // 唤醒去抖帧数,0=实测默认 3
    // ⚠️ 下面两个"关机"字段:0 = 用默认值,**负数 = 关闭该策略**(与上面数值字段的
    //    "0=默认"约定一致,但多了负数这条退出口——因为 0 在这里是个危险的合法值)。
    int   deep_shutdown_ms;  // DEEP 持续多久自动关机(默认 600000=10min;<0 = 永不关机)
    int   crit_bat_mv;       // 电池低于此值自动关机,防深放电(默认 3300;<0 = 不检查)
    bool  manage_leds;       // 自动切灯带 IDLE/OFF/AMBIENT(默认 true;不用灯带的应用设 false)
    bool  manage_bus_5v;     // 深度省电切 M-Bus 5V(默认 true;若 5V 还带着别的外设设 false)
    bool  wake_haptic;       // 唤醒时一下轻震(默认 true)
    // 阶段切换回调(可 NULL):应用借此换画面(角色睡着/伸懒腰)等,回调在 feed 的调用上下文执行
    void (*on_stage_change)(core2_sleep_stage_t from, core2_sleep_stage_t to);
} core2_sleep_cfg_t;

typedef struct {
    core2_sleep_cfg_t   cfg;
    core2_sleep_stage_t stage;
    motion_detect_t     md;
    int                 frames;      // 当前阶段帧计数(change_stage 清零)
    int                 probe_ms;    // 距上次读电量的累计 ms(跨阶段,不清零)
    int                 low_hits;    // 连续读到低电的次数(单次坏读不许触发关机)
} core2_sleep_t;

// 实机定案默认(倾斜迷宫 2026-07-01 调通值);自定义时从这份改起,别漏掉 bool 字段
#define CORE2_SLEEP_CFG_DEFAULT (core2_sleep_cfg_t){ \
    .nap_after_ms = 12000, .deep_after_ms = 60000, \
    .awake_brightness = 60, .nap_brightness = 10, \
    .frame_ms = 16, .deep_poll_ms = 120, \
    .deep_shutdown_ms = 600000, .crit_bat_mv = 3300, \
    .manage_leds = true, .manage_bus_5v = true, .wake_haptic = true }

/** @brief 初始化。cfg 传 NULL = CORE2_SLEEP_CFG_DEFAULT;数值字段为 0 处用默认值补齐。
 *  依赖 core2_power_init 已完成(core2_board_init 已代管)。 */
void core2_sleep_init(core2_sleep_t *s, const core2_sleep_cfg_t *cfg);

/**
 * @brief 每帧喂一次三轴加速度并推进省电状态机。
 * @param accel_g      三轴加速度(g);本帧没读到传 NULL。
 * @param nap_eligible 当前应用状态是否允许进打盹(如:只有游玩态 true;
 *                     菜单/庆祝/校准等给 false,静止计时会被清零)。
 * @return 建议的本帧循环延时 ms(清醒/打盹 = frame_ms,深度省电 = deep_poll_ms)。
 */
int core2_sleep_feed(core2_sleep_t *s, const float accel_g[3], bool nap_eligible);

/** @brief 当前阶段。非 AWAKE 时应用应跳过逻辑/渲染。 */
core2_sleep_stage_t core2_sleep_stage(const core2_sleep_t *s);

/** @brief 最近一帧机身动作量 |Δax|+|Δay|+|Δaz|(g),随 feed 更新。
 *  应用可复用它做自己的稳定判据(如:IMU 校准前等机身静止)。 */
float core2_sleep_motion(const core2_sleep_t *s);

/** @brief 立即唤醒(触摸/按键等非 IMU 活动时调;清醒态调用无副作用)。 */
void core2_sleep_wake(core2_sleep_t *s);

/** @brief 记一次"外部活动":清零静止计时(切关/交互事件时调,防误打盹)。 */
void core2_sleep_kick(core2_sleep_t *s);

/** @brief 运行时改清醒亮度;清醒态立即生效,休眠态记住待唤醒用。 */
void core2_sleep_set_awake_brightness(core2_sleep_t *s, int pct);

/**
 * @brief 手动跳到目标阶段(2026-07-17 新增,power_lab 休眠演练用)。
 *
 * 复用组件内固化的亮度→DCDC3→灯带→5V 顺序(内部直接调用文件内已有的
 * enter_nap/enter_deep/do_wake,与自动状态机走同一套路径),**不许 app 散装重拼这套
 * 顺序**。跳到当前已在的阶段是空操作。stage 传 AWAKE 等价于唤醒(即 core2_sleep_wake)。
 *
 * @param stage 目标阶段。
 */
void core2_sleep_force_stage(core2_sleep_t *s, core2_sleep_stage_t stage);

#ifdef __cplusplus
}
#endif
