// fish_pond —— 应用入口
//
// 大鱼池塘:摇杆开船、曲柄放/收线,把浆果饵放到鱼跟前钓鱼,集满 3 条放生开派对。
// 外设 = Chain Encoder(曲柄)+ Chain Joystick(船),PORT.C(蓝口,UART2)经 Chain Bridge,
// 与 chain_lab 同套硬件、传输层零改动(SPEC.md §1)。

#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "chain_link.h"

static const char *TAG = "fish_pond";

void app_main(void)
{
    ESP_LOGI(TAG, "=== fish_pond 大鱼池塘 启动 ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃都回 launcher
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V → PORT.C Chain 节点才有电)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 池塘本体(没插 Chain 节点也能进:屏上有提示卡,插上即接管)
    chain_link_start();
    ESP_LOGI(TAG, "池塘已启动:Chain 节点插 PORT.C(蓝口,经 Chain Bridge)");
}
