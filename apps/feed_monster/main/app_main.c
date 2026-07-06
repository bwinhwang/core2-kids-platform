// 喂怪兽(feed_monster)—— 应用入口
//
// 玩法:超声波测手离探头远近——近=怪兽张大嘴+音高升+灯带亮暖,远=闭嘴静默(连续层=空气琴);
//      怪兽饿了→饼干出现→手靠近(进吃区)就 CHOMP 吃掉,喂满 5 个→全屏庆祝→重开。
//      无目标失败、无计时,零失败。
// 硬件:Core2 + Bottom2 + Unit Ultrasonic-I2C(PORT.A @0x57,吃 M-Bus 5V/EXTEN)。

#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "monster_game.h"

static const char *TAG = "feed_monster";

void app_main(void)
{
    ESP_LOGI(TAG, "=== 喂怪兽 启动 ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃/电源键退出都回 launcher
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V → PORT.A 超声波才有电)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    app_slot_enable_button_exit();   // 电源键短按 = 回 launcher

    // ② 游戏本体(超声波没插也能进:屏上有提示卡,插上即生效)
    monster_game_start();
    ESP_LOGI(TAG, "游戏任务已启动:把手靠近超声波喂怪兽");
}
