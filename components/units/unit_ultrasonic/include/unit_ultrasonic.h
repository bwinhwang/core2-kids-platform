// Unit Ultrasonic-I2C(U098-B1)最小驱动 —— I2C 超声波测距,芯片 RCWL-9620,地址 0x57
//
// 硬件事实见 docs/units/Unit_Ultrasonic_I2C.md(量程 2cm~450cm,测量周期 ~50ms)。
// 接入:unit_ultrasonic_init(core2_board_port_a(), 0)  —— PORT.A 外接总线(G32/G33)。
//
// ⚠️ 供电坑(同 8Encoder):单元吃 PORT.A 的 5V = M-Bus 5V(EXTEN)。电池供电时 EXTEN
//   没开单元就没电(插 USB 时 VBUS 直通会掩盖);深度省电切 5V 后单元断电复位,恢复
//   供电后重调 init 即可。详见 core2_board_port_a() 注释。
//
// 读写协议(按 M5Unit-Sonic 成熟约定,注册表无 I2C 版现成组件故自写):
//   触发 = 写单字节 0x01(一笔 transmit);等 ≥ 一个测量周期(~50ms,官方库用 120ms 保守);
//   读 = 收 3 字节(一笔 receive),24-bit **大端**、单位 **µm**,mm = raw/1000。
//   🔴 触发与读是两笔独立事务,**不用 i2c_master_transmit_receive/repeated-start**
//   (与 8Encoder 那类 MCU 从机的组合读坑无关,但分开做最稳、与官方库一致)。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_ULTRASONIC_ADDR_DEFAULT  0x57
#define UNIT_ULTRASONIC_MIN_MM        20.0f     // 2cm 近距盲区,更近读数不可信
#define UNIT_ULTRASONIC_MAX_MM        4500.0f   // 450cm 量程上限

/**
 * @brief 挂载并探测超声波单元(发一次触发看是否 ACK)。
 *
 * 可重复调用:单元没插/没电时返回错误,插上/复电后再调即可接管(设备句柄只挂一次)。
 *
 * @param bus  外接 I2C 总线(core2_board_port_a())。
 * @param addr 7 位地址;传 0 = 默认 0x57。
 * @return ESP_OK 就绪;ESP_ERR_INVALID_ARG 总线为 NULL;其余 = I2C 通信失败(没插/没电)。
 */
esp_err_t unit_ultrasonic_init(i2c_master_bus_handle_t bus, uint8_t addr);

/** @brief 发一次测量触发(写 0x01)。之后须等 ≥ 一个测量周期再 read_mm。 */
esp_err_t unit_ultrasonic_trigger(void);

/**
 * @brief 读回上一次触发的测距结果(mm)。
 *
 * @param mm 输出距离(毫米,float)。
 * @return ESP_OK 且 mm 在 [MIN_MM,MAX_MM] 内;
 *         ESP_ERR_NOT_FOUND = 收到数据但越界(无回波/太远太近 = "没有目标",单元仍在);
 *         其余(TIMEOUT/INVALID_RESPONSE 等)= I2C 读失败(拔线/断电),调用方据此判喪失。
 */
esp_err_t unit_ultrasonic_read_mm(float *mm);

#ifdef __cplusplus
}
#endif
