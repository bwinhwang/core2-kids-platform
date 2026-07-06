// Unit 8Encoder(U153)最小驱动 —— 8 路旋转编码器 + 8 按键 + 1 拨动开关 + 9 RGB
//
// 硬件事实见 docs/units/Unit_8Encoder.md(从机 MCU = STM32F030C8T6,I2C 0x41)。
// 接入:unit_8encoder_init(core2_board_port_a(), 0)  —— PORT.A 外接总线(G32/G33)。
//
// ⚠️ 供电坑:单元吃 PORT.A 的 5V = M-Bus 5V(EXTEN)。电池供电时 EXTEN 没开单元
//   就没电(插 USB 时 VBUS 直通会掩盖问题);深度省电切 5V 后单元复位,LED/计数全清,
//   恢复供电后应用要自己重建 LED 状态。详见 core2_board_port_a() 注释。
//
// 设计:与官方 Arduino 库同粒度——每个值一次 I2C 事务(固件对"跨值连读"未验证,
//      不做批量拼读);Increment 寄存器读后自动清零,天然是"这一帧转了多少格"。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_8ENCODER_ADDR_DEFAULT  0x41
#define UNIT_8ENCODER_NUM_ENC       8
#define UNIT_8ENCODER_NUM_LEDS      9   // LED0~7=旋钮就地灯,LED8=拨动开关灯

// 按键电平换算:官方固件按下读 0(上拉到 GND)。若实机验证相反,只改这一个宏。
#define UNIT_8ENCODER_BTN_ACTIVE_LOW 1

/**
 * @brief 挂载并探测 8Encoder(读固件版本寄存器当在位检查)。
 *
 * 可重复调用:单元没插时返回错误,插上后再调即可接管(设备句柄只挂一次)。
 * 成功时顺带把 8 路 Increment 读一遍清掉存量(上电期间的误转不算数)。
 *
 * @param bus  外接 I2C 总线(core2_board_port_a())。
 * @param addr 7 位地址;传 0 = 默认 0x41。
 * @return ESP_OK 就绪;ESP_ERR_INVALID_ARG 总线为 NULL;其余 = I2C 通信失败(没插/没电)。
 */
esp_err_t unit_8encoder_init(i2c_master_bus_handle_t bus, uint8_t addr);

/** @brief 读第 idx 路(0~7)增量(读后硬件自动清零 = 距上次读转了多少格,带符号)。 */
esp_err_t unit_8encoder_read_increment(int idx, int32_t *inc);

/** @brief 依次读全部 8 路增量(8 次事务;任一失败即返回,inc 内容不完整勿用)。 */
esp_err_t unit_8encoder_read_increments(int32_t inc[UNIT_8ENCODER_NUM_ENC]);

/** @brief 读第 idx 路(0~7)按键,已按 UNIT_8ENCODER_BTN_ACTIVE_LOW 换算:true=按下。 */
esp_err_t unit_8encoder_read_button(int idx, bool *pressed);

/** @brief 依次读全部 8 路按键(8 次事务)。 */
esp_err_t unit_8encoder_read_buttons(bool pressed[UNIT_8ENCODER_NUM_ENC]);

/** @brief 读拨动开关(0x60):true/false 对应两个拨位(哪边是 true 以实机为准)。 */
esp_err_t unit_8encoder_read_switch(bool *on);

/** @brief 写第 idx 颗(0~8)RGB。满亮度刺眼,幼儿应用请在应用层压亮度。 */
esp_err_t unit_8encoder_set_led(int idx, uint8_t r, uint8_t g, uint8_t b);

/** @brief 读第 idx 路累计计数(有符号 32 位,会回绕;一般用 Increment 就够)。 */
esp_err_t unit_8encoder_read_counter(int idx, int32_t *val);

/** @brief 清零第 idx 路累计计数。 */
esp_err_t unit_8encoder_reset_counter(int idx);

/** @brief 读固件版本(寄存器 0xFE)。 */
esp_err_t unit_8encoder_fw_version(uint8_t *ver);

#ifdef __cplusplus
}
#endif
