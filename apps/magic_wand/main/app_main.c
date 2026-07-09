// magic_wand —— 隔空魔法棒(新卡带,ota_5)。规格见 apps/magic_wand/SPEC.md。
#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "magic_wand.h"

static const char *TAG = "magic_wand";

void app_main(void)
{
    ESP_LOGI(TAG, "=== magic_wand 启动 ===");
    app_slot_return_to_factory();   // 第一行:之后任何复位/崩溃都回 launcher

    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "core2_board_init 失败: %s", esp_err_to_name(err));
        return;
    }

    magic_wand_start();
}
