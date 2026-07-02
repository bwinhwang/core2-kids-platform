// 倾斜迷宫 —— 应用入口 (CLAUDE.md §3, §7, §15)
//
// 职责很窄:① 硬件 bring-up(BSP/AXP192 屏电、I2C、IMU、灯带、震动、音频)
//          ② 起反馈编排器  ③ 起游戏状态机(ATTRACT→校准→PLAY→WIN→下一关)
// 游戏逻辑全在 game_state.c / physics.c / maze.c / render.c / feedback.c。

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#include "bsp/m5stack_core_2.h"

#include "imu_mpu6886.h"
#include "ledstrip_fx.h"
#include "haptics.h"
#include "maze_audio.h"
#include "render.h"
#include "feedback.h"
#include "game_state.h"
#include "parent_menu.h"
#include "power.h"

static const char *TAG = "app";

static void i2c_scan(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "扫描内部 I2C 总线 (G21/G22)…");
    bool found_imu = false;
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            const char *name = "?";
            switch (addr) {
                case 0x34: name = "AXP192 (电源)";   break;
                case 0x38: name = "FT6336U (触摸)";  break;
                case 0x51: name = "BM8563 (RTC)";    break;
                case 0x68: name = "MPU6886 (IMU)"; found_imu = true; break;
            }
            ESP_LOGI(TAG, "  发现 0x%02X  %s", addr, name);
        }
    }
    if (!found_imu) {
        ESP_LOGE(TAG, "✗ 没扫到 0x68！底座没接?没有 IMU 游戏跑不了(CLAUDE.md §2.2)");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 倾斜迷宫 启动 ===");

    // 1) I2C(内部总线)
    ESP_ERROR_CHECK(bsp_i2c_init());
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();

    // 2) 显示 + LVGL(同时配好 AXP192 屏供电/背光)
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "bsp_display_start 失败(检查 CONFIG_BSP_PMU_AXP192)");
        abort();
    }
    ESP_ERROR_CHECK(bsp_display_brightness_set(60));   // 压低亮度护眼(§13)
    render_init();

    // 3) 自检:扫 I2C 确认底座 IMU 在位
    i2c_scan(bus);

    // 3.5) 打开 M-Bus 5V(SY7088 升压),否则底座灯带无供电全黑(见 power.h 注释)
    power_init(bus);

    // 4) 反馈通道组件 + 编排器
    ESP_ERROR_CHECK(ledstrip_fx_init());
    ledstrip_fx_set_max_brightness(48);
    ESP_ERROR_CHECK(haptics_init());
    ESP_ERROR_CHECK(maze_audio_init());
    ESP_ERROR_CHECK(feedback_init());
    feedback_emit_hello();                 // 开机问候:音 + 轻震 + 灯带暖色

    // 5) IMU(MPU6886 @ 0x68,复用 BSP I2C 总线)
    esp_err_t imu_err = imu_mpu6886_init(bus);
    if (imu_err != ESP_OK) {
        ESP_LOGE(TAG, "IMU 初始化失败(%s):确认 Bottom2 在位、0x68 可达",
                 esp_err_to_name(imu_err));
        return;
    }

    // 6) 家长隐藏菜单入口(底部长按 3s)
    parent_menu_init();

    // 7) 起状态机
    game_state_start();
    ESP_LOGI(TAG, "状态机已启动:倾斜机身开始玩");
}
