// Unit Chain Joystick(U205)最小驱动 —— 霍尔电磁摇杆 X/Y + Z 按键 + 1 RGB
//
// 硬件事实见 docs/units/Chain_Joystick.md(节点 MCU = STM32G031,走 Chain UART 级联,
// **无 I2C 地址、不直接输出模拟电压**;X/Y 由节点 ADC 数字化后经 UART 上报)。
// 传输层 = chain_bus(Core2 PORT.C/UART2)。
//
// 用法:
//   chain_bus_init_port_c();
//   unit_chain_joystick_probe(1);
//   uint16_t x, y; unit_chain_joystick_read_adc(1, &x, &y);   // 原始 ADC(约 12 位)
//   bool pressed; unit_chain_joystick_read_button(1, &pressed);
// 板载 RGB / 亮度用 chain_bus_set_rgb(id,0,...) / chain_bus_set_rgb_brightness(id,...)。
//
// ⚠️ 个体有零偏(硬件文档 §6):应用上电时采一次居中值做软件归中。
// ⚠️ 供电靠 PORT.C 5V(EXTEN),深度省电切 5V 后节点复位;唤醒后重新 probe。坑同 chain_bus。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_CHAIN_JOYSTICK_ID_DEFAULT  1   // 单节点直连时链上位置 = 1

/** @brief 探测:读设备类型并确认 = Chain Joystick。
 *  @return ESP_OK 在位且类型对;ESP_ERR_NOT_FOUND 类型不符;其余 = 通信失败。 */
esp_err_t unit_chain_joystick_probe(uint8_t id);

/** @brief 读原始 16 位 ADC(实际约 12 位,静止居中 ~2048)。归中/归一化由应用做。 */
esp_err_t unit_chain_joystick_read_adc(uint8_t id, uint16_t *x, uint16_t *y);

/** @brief 读 Z 按键(下压摇杆):true = 按下。 */
esp_err_t unit_chain_joystick_read_button(uint8_t id, bool *pressed);

#ifdef __cplusplus
}
#endif
