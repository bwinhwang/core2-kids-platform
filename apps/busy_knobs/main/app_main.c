// 旋钮忙碌台(busy_knobs)—— 应用入口
//
// 玩法:8Encoder 的 8 个旋钮对应屏上 8 根彩虹音柱——转=柱子升降+五声音阶叮咚+
//      旋钮就地灯变亮;按=柱顶小脸唱歌弹跳;8 根全拉满=全屏庆祝后缓缓落回;
//      拨动开关=白天/黑夜换景。无目标、无失败,忙碌板(busy board)范式。
// 硬件:Core2 + Bottom2 + Unit 8Encoder(PORT.A @0x41,吃 M-Bus 5V/EXTEN)。

#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "knobs_game.h"

static const char *TAG = "busy_knobs";

void app_main(void)
{
    ESP_LOGI(TAG, "=== 旋钮忙碌台 启动 ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃/电源键退出都回 launcher
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V → 8Encoder 才有电)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    app_slot_enable_button_exit();   // 电源键短按 = 回 launcher

    // ② 游戏本体(8Encoder 没插也能进:屏上有提示卡,插上即生效)
    knobs_game_start();
    ESP_LOGI(TAG, "游戏任务已启动:转动旋钮开始玩");
}
