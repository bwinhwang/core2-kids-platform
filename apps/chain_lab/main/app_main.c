// chain_lab —— 应用入口
//
// 用途:验证 Chain Encoder(U207)/ Chain Joystick(U205)两个驱动 + chain_bus 传输层。
//      屏上一个仪表台:转编码器 → 表盘指针转 + 数值变 + 节点灯换色;推摇杆 → 方块里的
//      光点跟着走 + 节点灯换色;按下 → 光点变绿 + 节点灯闪白 + 轻震。同时验证读(RX)与
//      写节点 RGB(TX)两条路。
// 硬件:Core2 + Bottom2 + Chain 节点接 **PORT.C**(蓝口,UART2 G13/G14),经 Chain Bridge;
//      节点吃 PORT.C 5V = M-Bus 5V/EXTEN(core2_board_init 已代开)。

#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "chain_lab.h"

static const char *TAG = "chain_lab";

void app_main(void)
{
    ESP_LOGI(TAG, "=== Chain 验证台 启动 ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃都回 launcher
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V → PORT.C Chain 节点才有电)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 验证台本体(没插节点也能进:屏上有提示卡,插上即接管)
    chain_lab_start();
    ESP_LOGI(TAG, "验证台已启动:Chain 节点插 PORT.C(蓝口,经 Chain Bridge)");
}
