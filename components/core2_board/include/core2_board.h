// core2_board —— Core2 v1.0 + M5GO Bottom2 一键 bring-up(平台外设层的总入口)
//
// 新应用只需:
//     core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;
//     ESP_ERROR_CHECK(core2_board_init(&cfg));
// 即完成:内部 I2C → 屏+LVGL+背光 → I2C 自检扫描 → AXP192 直控绑定 →
//        (按需)灯带供电+驱动 / 喇叭 / 震动 / IMU,全部按实机验证过的顺序。
//
// ⚠️ 初始化顺序是本组件固化的核心知识,别绕过它手工散装初始化:
//   1. bsp_i2c_init 必须最先(AXP192/触摸/IMU 都挂内部 I2C);
//   2. bsp_display_start 会做 AXP192 基础配置(屏电/背光),并把 REG 0x12 重写——
//      所以 core2_power_init / EXTEN(灯带 5V)必须在它**之后**;
//   3. 灯带先开 M-Bus 5V(EXTEN)再 init 驱动,否则数据在跑灯不亮(经典坑);
//   4. IMU 复用内部 I2C 总线句柄,不自己再 init 一条(会和 AXP192/触摸抢)。
//
// 各外设也可单独用(core2_power / ledstrip_fx / audio_fx / haptics / imu_mpu6886),
// 本组件只是把它们按正确顺序串起来。板级事实见 docs/platform/,坑位见各组件头注释。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int     brightness_pct;      // 初始背光 0~100(幼儿应用建议 ≤60,护眼也省电)
    bool    enable_leds;         // Bottom2 灯带(自动开 M-Bus 5V/EXTEN)
    uint8_t led_max_brightness;  // 灯带全局亮度上限 0~255(幼儿应用建议 ≤48)
    bool    enable_audio;        // NS4168 喇叭(audio_fx,整局保持 open)
    bool    enable_haptics;      // 震动马达(AXP192 LDO3,经 BSP feature)
    bool    enable_imu;          // MPU6886(来自 Bottom2;没接底座会失败)
} core2_board_cfg_t;

// 幼儿应用默认:全开、低亮
#define CORE2_BOARD_CFG_KIDS_DEFAULT (core2_board_cfg_t){ \
    .brightness_pct = 60, .enable_leds = true, .led_max_brightness = 48, \
    .enable_audio = true, .enable_haptics = true, .enable_imu = true }

/**
 * @brief 按正确顺序初始化平台外设。cfg 传 NULL 等价于 KIDS_DEFAULT。
 * @return ESP_OK 全部就绪;ESP_ERR_NOT_FOUND IMU 没找到(多半是没接 Bottom2 底座);
 *         其余错误码来自对应外设 init。失败的外设有显式 ESP_LOGE,不静默。
 */
esp_err_t core2_board_init(const core2_board_cfg_t *cfg);

/** @brief LVGL 显示句柄(core2_board_init 之后有效)。 */
lv_display_t *core2_board_display(void);

/** @brief 内部 I2C 总线句柄(挂 AXP192/触摸/RTC/IMU;外接 UNIT 别用这条)。 */
i2c_master_bus_handle_t core2_board_i2c(void);

/** @brief 扫内部 I2C 并打印在位器件(M0 自检用;init 内已自动跑一次)。
 *  @return true = 扫到 0x68(IMU/底座在位)。 */
bool core2_board_i2c_scan(void);

#ifdef __cplusplus
}
#endif
