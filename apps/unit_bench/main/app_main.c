// unit_bench —— 外设/单元评估台(应用入口)
//
// 职责边界:本文件只做"平台 bring-up + 主循环编排",与其它 app 的 app_main.c 同一职责
// 边界(参照 apps/chain_lab/main/app_main.c)。真正的扫描/挂载状态机在
// unit_bench_scan.c,LVGL 视图与轮询逻辑在 unit_bench_ui.c——三层拆分见
// apps/unit_bench/README.md「代码结构」。
//
// 硬件:Core2 + Bottom2,评估对象来自 PORT.A(I2C:DLight/Ultrasonic/Gesture/8Encoder)+
//      PORT.C(Chain UART:Encoder/Joystick)。两个口的 5V 都是 M-Bus(EXTEN),
//      core2_board_init(enable_leds=true) 已代开。

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_slot.h"
#include "audio_fx.h"
#include "chain_bus.h"
#include "core2_board.h"
#include "core2_sleep.h"
#include "haptics.h"
#include "imu_mpu6886.h"
#include "kv_store.h"

#include "unit_bench_ui.h"

static const char *TAG = "unit_bench";

// 主循环节拍:评估台不追游戏时期 60Hz 手感线(CLAUDE.md §4),10Hz 足够描出数值/趋势,
// 也给 I2C(PORT.A)/UART(Chain)事务留够时间片。core2_sleep 的 frame_ms 必须和这个节拍
// 对齐,否则 nap_after_ms/deep_after_ms 换算出的"帧数阈值"就不代表真实秒数了。
#define UB_LOOP_MS 100

void app_main(void)
{
    ESP_LOGI(TAG, "=== unit_bench 外设/单元评估台 启动 ===");

    // ⓪ 第一行:设回 factory,此后任何复位/崩溃都回 launcher
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V → PORT.A/PORT.C 才有电)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_DEFAULT;
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 标定持久化(超声波零点偏移等,namespace="unit_bench")
    kv_store_init("unit_bench");

    // ③ Chain 传输层(PORT.C/UART2)。失败只影响 Chain 探测,不拦其余单元的评估功能。
    esp_err_t chain_err = chain_bus_init_port_c();
    if (chain_err != ESP_OK) {
        ESP_LOGW(TAG, "chain_bus_init_port_c 失败(%s):Chain 节点评估不可用,PORT.A 单元不受影响",
                 esp_err_to_name(chain_err));
    }

    // ④ UI(状态栏 + 列表页)+ 首次扫描 + 开机问候
    unit_bench_ui_start();
    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);

    // ⑤ 省电编排:评估台语境下深度省电切 M-Bus 5V 会杀掉被测单元的供电(CLAUDE.md §10 桌面
    //   评估省电坑),是错误行为——manage_bus_5v=false 且事实上禁用 DEEP,只留 NAP(降亮)一
    //   级。core2_sleep 目前没有现成的"禁用 DEEP"布尔开关(见 components/core2_sleep/
    //   core2_sleep.c),这里选的办法是把 deep_after_ms 设成 INT32_MAX:
    //     core2_sleep_feed() 判进入 DEEP 的条件是 `frames > deep_after_ms / frame_ms`
    //     (frames 是 int,每个 NAP 帧 +1)。deep_after_ms=INT32_MAX、frame_ms=100 时,
    //     阈值 = 2147483647/100 ≈ 21,474,836 帧,按 100ms/帧折算约 24.8 天连续静止才会
    //     碰到——不会溢出(frames 远达不到 int32 上限 2.1e9),事实上等价于"永不进 DEEP"。
    core2_sleep_cfg_t sc = CORE2_SLEEP_CFG_DEFAULT;
    sc.manage_bus_5v = false;
    sc.frame_ms      = UB_LOOP_MS;
    sc.deep_after_ms = INT32_MAX;
    core2_sleep_t sleep;
    core2_sleep_init(&sleep, &sc);

    // ⑥ 主循环(10Hz):UI 轮询/热插拔重试/CSV 导出全在 unit_bench_ui_tick 里,这里只管
    //   读 IMU 喂省电状态机(桌面评估场景的防误打盹另由 UI 层按需调 core2_sleep_kick)。
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        int64_t now_ms = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        unit_bench_ui_tick(now_ms, &sleep);

        imu_accel_t a;
        bool have = imu_mpu6886_read_accel(&a) == ESP_OK;
        int delay_ms = core2_sleep_feed(&sleep, have ? (float[]){ a.x, a.y, a.z } : NULL, true);

        vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));
    }
}
