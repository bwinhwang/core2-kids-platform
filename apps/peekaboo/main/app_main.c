// 躲猫猫昼夜屋(peekaboo)—— 应用入口
//
// 玩法:DLight 测环境光。小手把传感器一"捂住"(挡光)→ 屏幕入夜(星星/月亮/萤火虫出来、
//      圆圆打哈欠睡着、灯带转暗)；手一"松开"(光回来)→ 天亮(太阳出来、圆圆伸懒腰
//      醒来 = "躲猫猫!"的揭晓),攒够几次天亮 → 全屏小庆祝。
//      "捂住/松开"是幼儿最擅长的大动作,因果强、可无限重复;零失败、无计时。
// 硬件:Core2 + Bottom2 + Unit DLight(PORT.A @0x23,吃 M-Bus 5V/EXTEN)。

#include "esp_log.h"
#include "nvs_flash.h"

#include "app_slot.h"
#include "core2_board.h"
#include "peekaboo_game.h"

static const char *TAG = "peekaboo";

void app_main(void)
{
    ESP_LOGI(TAG, "=== 躲猫猫昼夜屋 启动(v2 夜里来客) ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃/电源键退出都回 launcher
    app_slot_return_to_factory();

    // NVS:相册/游行进度持久化(P4)。分区表已有 nvs(0x9000/16KB),不改表。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V → PORT.A 的 DLight 才有电)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    app_slot_enable_button_exit();   // 电源键短按 = 回 launcher

    // ② 游戏本体(DLight 没插也能进:屏上有提示卡,插上即生效)
    peekaboo_game_start();
    ESP_LOGI(TAG, "游戏任务已启动:捂住 DLight 天就黑,松开手天就亮");
}
