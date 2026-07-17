// 小鸡回窝(chick_pour)—— 应用入口(CLAUDE.md §3, §7, §15;仿 tilt_maze app_main.c)
//
// P2 阶段(SPEC §12):归家闭环——P1 群体手感(已实机验证)之上,加两家门判定 +
// 捕获/弹出 + 探头小脸计数 + 五声音阶进度 + 派对 + 重散布点。
// ATTRACT 睡醒/摇一摇彩蛋/launcher 专属图标为 P3,本轮不做。
//
// 平台外设 bring-up 全部收在 core2_board_init()(顺序知识固化在组件里,勿散装重写);
// 本文件只负责:① 一键平台初始化 ② 起应用层(静态场景/反馈/状态机)。
#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "scene.h"
#include "feedback.h"
#include "game_state.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_LOGI(TAG, "=== 小鸡回窝(chick_pour)P2 启动 ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃都回 launcher
    //    (crash-safe,机制见 components/app_slot/README.md)
    app_slot_return_to_factory();

    // ① 平台外设一键 bring-up:I2C → 屏/LVGL/背光 → I2C 自检 → AXP192 直控 →
    //    灯带(含 M-Bus 5V)→ 喇叭 → 震动 → IMU,顺序已实机验证。
    //    本卡带零外设(纯 IMU,SPEC 前言),用不到 PORT.A/C,但组件默认全开无害。
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 应用层:静态后院一次画完 → 反馈编排器 → 状态机(建动物+起 60Hz game_task)
    scene_init();
    ESP_ERROR_CHECK(feedback_init());
    game_state_start();
    ESP_LOGI(TAG, "P2 就绪:小鸡进窝、小鸭进塘,全归家开派对(SPEC §12 点检清单)");
}
