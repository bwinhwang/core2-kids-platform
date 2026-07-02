// core2_power —— Core2 v1.0 AXP192 直控(官方 BSP 不管的两路电源)
//
// ⚠️ 本组件固化两个实机踩坑(勿删,新应用直接复用):
//
// 1) M-Bus 5V / EXTEN:M5GO Bottom2 灯带(及一切吃 M-Bus 5V 的外设)由 Core2 的
//    SY7088 升压供电,使能挂 AXP192 EXTEN(REG 0x12 bit6)。Espressif BSP 只配
//    屏/触摸/喇叭/震动那几路 rail,**从不开 EXTEN** → 裸 BSP 下灯带没供电全黑
//    (数据线 G25 照常翻转、led_strip refresh 仍返回 ESP_OK,极易误判"灯带坏了")。
//
// 2) 背光 DCDC3:BSP 的 bsp_display_brightness_set(0) 只把 DCDC3 电压降到最低档
//    (~2.95V)并不断电,屏仍有微光;深度省电要真黑屏必须清 DCDC3 使能位
//    (REG 0x12 bit1)。
//
// 详见 docs/platform/Core2_v1_0.md §2、CLAUDE.md §17/§20.6。
// 初始化顺序:必须在 bsp_display_start() **之后**调用 core2_power_init()
// (BSP 的 AXP192 初始化会把 REG 0x12 重写,先调会被清掉)。
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 绑定 AXP192(内部 I2C 0x34)并做一次性准备(GPIO0 配成 LDOio 3.3V 输出)。
 *        不改变任何电源开关状态;须在 bsp_display_start() 之后调用。
 * @param bus 来自 bsp_i2c_get_handle() 的内部 I2C 总线句柄。
 */
esp_err_t core2_power_init(i2c_master_bus_handle_t bus);

/** @brief 开/关 M-Bus 5V(AXP192 EXTEN → SY7088 升压)。
 *  用 Bottom2 灯带前必须开;深度省电时关掉可断灯带供电 + SY7088 静态电流。 */
esp_err_t core2_power_bus_5v(bool on);

/** @brief 真正开/关背光(AXP192 DCDC3 使能位)。
 *  brightness 0% 不熄屏(见文件头坑 2);关背光前建议先把亮度调 0,
 *  开背光后再恢复亮度,避免瞬间闪亮。 */
esp_err_t core2_power_backlight(bool on);

#ifdef __cplusplus
}
#endif
