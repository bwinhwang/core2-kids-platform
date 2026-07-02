// M-Bus 5V 电源控制(AXP192 EXTEN → SY7088 升压)。
//
// ⚠️ 关键板级事实:M5GO Bottom2 的 SK6812 灯带吃 M-Bus 5V,而这条 5V 由 Core2 的
// SY7088 升压提供、其使能挂在 **AXP192 EXTEN**(REG 0x12 bit6)。Espressif BSP 只配
// 屏/触摸/喇叭/震动那几路 rail,**从不开 EXTEN**,所以裸 BSP 下底座灯带没供电、全黑
// (数据线 G25 是 3.3V 逻辑照常翻转,RMT refresh 仍报 OK,极易误判成"灯带坏了")。
// 见 Core2_v1_0.md §2(SY7088)、CLAUDE.md §12。
#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"

/** @brief 绑定 AXP192(0x34)并打开 M-Bus 5V(灯带供电)。须在 bsp_display_start 之后调用。 */
void power_init(i2c_master_bus_handle_t bus);

/** @brief 运行时开/关 M-Bus 5V(深度省电时关掉可断灯带 + SY7088 静态电流)。 */
void power_bus_5v(bool on);

/** @brief 真正开/关背光。Core2 背光 = AXP192 DCDC3(REG 0x12 bit1)。
 *  BSP 的 brightness 0% 只把 DCDC3 电压降到最低档(~2.95V)仍亮,并不熄屏;
 *  深度省电要彻底黑屏必须断 DCDC3。关背光前把亮度调 0、开背光后再恢复亮度。 */
void power_backlight(bool on);
