// busy_bus —— 应用入口
//
// 用途:单屏俯视小镇,摇杆开小巴士接动物乘客、按房形+颜色双编码送到对应的家。
// 硬件:Core2 + Bottom2 + Chain Joystick 接 **PORT.C**(蓝口,UART2 G13/G14),经 Chain
//      Bridge;摇杆吃 PORT.C 5V = M-Bus 5V/EXTEN(core2_board_init 已代开)。
#include "esp_log.h"

#include "app_slot.h"
#include "core2_board.h"
#include "bus_link.h"
#include "feedback.h"

static const char *TAG = "busy_bus";

void app_main(void)
{
    ESP_LOGI(TAG, "=== 小小巴士 启动 ===");

    // ⓪ 第一行先把启动分区设回 factory:此后任何复位/崩溃都回 launcher
    app_slot_return_to_factory();

    // ① 平台一键 bring-up(enable_leds=true 顺带开 M-Bus 5V → PORT.C 摇杆才有电)
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;   // 全开、低亮(60%/灯带≤48)
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "平台初始化失败(%s)%s", esp_err_to_name(err),
                 err == ESP_ERR_NOT_FOUND ? ":确认 Bottom2 底座在位(IMU/电池都来自底座)" : "");
        return;
    }

    // ② 反馈编排器(音频/触觉/灯带/摇杆节点四通道)+ 小镇本体(没插摇杆也能进:
    //    屏上有提示卡,插上即接管)
    ESP_ERROR_CHECK(feedback_init());
    bus_link_start();
    ESP_LOGI(TAG, "小小巴士已启动:Chain 摇杆插 PORT.C(蓝口,经 Chain Bridge)");
}
