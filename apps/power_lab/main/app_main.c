// power_lab —— 功耗/系统评估台(应用入口)
//
// 职责边界:本文件只做"平台 bring-up + 主循环编排",与 apps/unit_bench/main/app_main.c
// 同一职责边界——真正的负载矩阵/遥测/演练/续航/录制状态机在 power_lab_ctl.c(model 层,
// 不碰 LVGL),LVGL 视图与轮询逻辑在 power_lab_ui.c(view 层)。三层拆分详见
// apps/power_lab/README.md「代码结构」。
//
// 硬件:Core2 + Bottom2。评估对象是"这台机器自己的功耗"(AXP192 遥测),不需要 PORT.A
// 外接单元,core2_board_init 仍用默认配置(灯带/音频/震动/IMU 全开)——灯带/喇叭/震动本身
// 就是负载开关矩阵的三个可控负载源,IMU 平台硬依赖 Bottom2(见 CLAUDE.md §3.2)。
//
// 省电纪律(与 unit_bench 刻意不同,CLAUDE.md §7 明确点名的例外):power_lab 是"评估功耗
// 的工具本身",主循环**不**调用 core2_sleep_feed() 做自动闲置省电——那套自动降亮/断电
// 逻辑会和本 app 的负载开关矩阵互相打架(用户刚把背光调到 100% 观察电流,自动省电 12s
// 后又把它降下去,矩阵状态和实际状态就对不上了)。core2_sleep_t 仍然初始化,只是只通过
// core2_sleep_force_stage()(经 power_lab_ctl 的两段式请求)在"演练"场景下手动驱动。

#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_slot.h"
#include "audio_fx.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "haptics.h"

#include "power_lab_ctl.h"
#include "power_lab_ui.h"

static const char *TAG = "power_lab";

// 主循环节拍:与 unit_bench 同一取值(评估台不追 60Hz 手感线,10Hz 够描出数值/趋势)。
// 注意:本 app 不调 core2_sleep_feed,所以这里是**固定**周期,不像 unit_bench 那样跟随
// core2_sleep_feed 的返回值在 DEEP 阶段自动降频轮询——休眠演练期间(NAP/DEEP)主循环仍按
// 这个固定节拍跑,只是不碰 LVGL(见 power_lab_ui_tick 的守卫),用于持续做电流采样。
#define PL_LOOP_MS 100

void app_main(void)
{
    ESP_LOGI(TAG, "=== power_lab 功耗/系统评估台 启动 ===");

    // ⓪ 第一行:设回 factory,此后任何复位/崩溃都回 launcher
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(灯带/喇叭/震动全开——三者本身就是负载矩阵的可控负载源)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_DEFAULT;
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 省电状态结构初始化(仅供 force_stage 演练用,主循环不调 core2_sleep_feed,见文件头注释)。
    //    manage_bus_5v/manage_leds 保持默认 true:DEEP 演练的意义就是完整重现真实深度省电的
    //    亮度→DCDC3→灯带→5V 全链路断电,不像 unit_bench 那样需要保护外接单元供电
    //    (power_lab 本身不接 PORT.A 外接单元,断 5V 只影响灯带,可以接受)。
    core2_sleep_t sleep;
    core2_sleep_init(&sleep, NULL);

    // ③ model 层初始化(power_monitor_init + 探测 esp_pm_configure + 负载矩阵基线)
    static pl_ctl_t ctl;   // static:主循环全程存活,体积也不小(power_telemetry_t 等),不占栈
    pl_ctl_init(&ctl, &sleep);

    // ④ UI(两页 LVGL 画面)+ 开机问候
    power_lab_ui_start(&ctl);
    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);

    // ⑤ 主循环(10Hz 固定节拍):model 层先 tick(遥测轮询/演练采样收尾/录制请求执行),
    //    UI 层再 tick(演练进行中会直接跳过,见 power_lab_ui.c 文件头「休眠演练纪律」)。
    //    ctl tick 必须先于 ui tick 调用——这个顺序保证了"点按钮请求演练"与"下一帧才真正
    //    force_stage"之间,ui_tick 不会观察到"pending 已设但 drill_stage 还没变"的中间态
    //    (power_lab_ui.c on_nap_click/on_deep_click 注释与 power_lab_ctl.h 头注释均提到此点)。
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        int64_t now_ms = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        pl_ctl_tick(&ctl, now_ms);
        power_lab_ui_tick(&ctl, now_ms);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(PL_LOOP_MS));
    }
}
