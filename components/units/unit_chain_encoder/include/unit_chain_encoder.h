// Unit Chain Encoder(U207)最小驱动 —— AB 旋转编码器 + 中心按键 + 1 RGB
//
// 硬件事实见 docs/units/Chain_Encoder.md(节点 MCU = STM32G031,走 Chain UART 级联,
// **无 I2C 地址**)。传输层 = chain_bus(Core2 PORT.C/UART2)。
//
// 用法:
//   chain_bus_init_port_c();
//   unit_chain_encoder_probe(1);                     // 探在位 + 确认是 encoder
//   int16_t v; unit_chain_encoder_read_value(1, &v); // 绝对计数(应用自算帧间 delta)
//   bool pressed; unit_chain_encoder_read_button(1, &pressed);
// 板载 RGB / 亮度用 chain_bus_set_rgb(id,0,...) / chain_bus_set_rgb_brightness(id,...)。
//
// ⚠️ 供电靠 PORT.C 5V(EXTEN),深度省电切 5V 后节点复位;唤醒后重新 probe。坑同 chain_bus。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_CHAIN_ENCODER_ID_DEFAULT  1   // 单节点直连时链上位置 = 1

/** @brief 探测:读设备类型并确认 = Chain Encoder。
 *  @return ESP_OK 在位且类型对;ESP_ERR_NOT_FOUND 类型不符;其余 = 通信失败(没插/没电/接反)。 */
esp_err_t unit_chain_encoder_probe(uint8_t id);

/** @brief 读绝对计数(有符号 16 位,会回绕)。顺时针增(默认 AB 相位)。 */
esp_err_t unit_chain_encoder_read_value(uint8_t id, int16_t *value);

/** @brief 读中心按键:true = 按下。 */
esp_err_t unit_chain_encoder_read_button(uint8_t id, bool *pressed);

/** @brief 把绝对计数清零。 */
esp_err_t unit_chain_encoder_reset_value(uint8_t id);

#ifdef __cplusplus
}
#endif
