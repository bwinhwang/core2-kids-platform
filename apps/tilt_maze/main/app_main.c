// 倾斜迷宫 —— 应用入口 (CLAUDE.md §3, §7, §15)
//
// 平台外设 bring-up 全部收在 core2_board_init()(顺序知识固化在组件里,勿散装重写);
// 本文件只负责:① 一键平台初始化 ② 起应用层(渲染/反馈/家长菜单/游戏状态机)。
// 游戏逻辑全在 game_state.c / physics.c / maze.c / render.c / feedback.c。
//
// 【新应用参考】复制本工程做新 APP 时,保留第 ① 步,替换第 ② 步为你的应用逻辑;
// 平台复用指南见 docs/platform/BSP_GUIDE.md。

#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "render.h"
#include "feedback.h"
#include "game_state.h"
#include "parent_menu.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "=== 倾斜迷宫 启动 ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃/电源键退出都回 launcher
    //    (crash-safe,机制见 components/app_slot/README.md)
    app_slot_return_to_factory();

    // ① 平台外设一键 bring-up:I2C → 屏/LVGL/背光 → I2C 自检 → AXP192 直控 →
    //    灯带(含 M-Bus 5V)→ 喇叭 → 震动 → IMU,顺序已实机验证。
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 应用层
    render_init();
    ESP_ERROR_CHECK(feedback_init());
    feedback_emit_hello();                 // 开机问候:音 + 轻震 + 灯带暖色
    parent_menu_init();                    // 家长隐藏菜单(底部长按 3s)
    app_slot_enable_button_exit();         // 电源键短按 = 回 launcher
    game_state_start();                    // 状态机:ATTRACT→校准→PLAY→WIN→下一关
    ESP_LOGI(TAG, "状态机已启动:倾斜机身开始玩");
}
