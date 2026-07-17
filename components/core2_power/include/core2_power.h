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
#include <stddef.h>
#include <stdint.h>
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

/**
 * @brief 连续读 len 个寄存器(从 reg 起始,单笔 I2C 事务)。
 *
 * 供 `power_monitor` 等需要直读 AXP192 ADC/库仑计寄存器的组件用,复用本组件已绑定的
 * AXP192 设备句柄,避免各自重复 `i2c_master_bus_add_device`。AXP192 是普通寄存器寻址
 * 芯片(不是 8Encoder 那类 MCU 从机),`i2c_master_transmit_receive` 组合读安全,
 * 与本文件 `core2_power_bus_5v`/`core2_power_backlight` 内部的 RMW 读法一致。
 *
 * @param reg 起始寄存器地址。
 * @param buf 输出缓冲,长度 len。
 * @param len 要读的字节数。
 * @return ESP_OK 成功;ESP_ERR_INVALID_STATE core2_power_init 尚未成功过;
 *         ESP_ERR_INVALID_ARG buf 为 NULL 或 len 为 0。
 */
esp_err_t core2_power_read_regs(uint8_t reg, uint8_t *buf, size_t len);

/**
 * @brief 整字节写一个寄存器(不做读-改-写)。
 *
 * 供 `power_monitor` 写 ADC 使能(reg 0x82)/ 库仑计控制(reg 0xB8)这类"整个寄存器
 * 只有一个功能、别的组件不会碰"的场景。⚠️ 与 `axp_rmw`(内部用于 REG 0x12)不同,
 * 本函数**不做 RMW**——若目标寄存器同时管着别的功能位(如 REG 0x12 那种挤在一起的
 * 电源使能位),必须走 `core2_power_bus_5v`/`core2_power_backlight` 那类专用 RMW 接口,
 * 不要用本函数整字节覆盖。
 *
 * @return ESP_OK 成功;ESP_ERR_INVALID_STATE core2_power_init 尚未成功过。
 */
esp_err_t core2_power_write_reg(uint8_t reg, uint8_t val);

#ifdef __cplusplus
}
#endif
