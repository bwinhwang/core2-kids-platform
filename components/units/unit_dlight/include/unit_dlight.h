// Unit DLight(U136)最小驱动 —— I2C 环境光/照度传感器,芯片 BH1750FVI,地址 0x23
//
// 硬件事实见 docs/units/Unit_DLight.md(量程 1~65535 lx,高分辨率单次测量约 120ms)。
// 接入:unit_dlight_init(core2_board_port_a(), 0)  —— PORT.A 外接总线(G32/G33)。
//
// ⚠️ 供电坑(同 8Encoder/超声波):单元吃 PORT.A 的 5V = M-Bus 5V(EXTEN)。电池供电
//   时 EXTEN 没开单元就没电(插 USB 时 VBUS 直通会掩盖);深度省电切 5V 后单元断电
//   复位(模式丢失),恢复供电后重调 init 即可(init 会重发上电+选模式)。详见
//   core2_board_port_a() 注释。
//
// 读写协议(BH1750 连续高分辨率模式,注册表 espressif/bh1750 与 datasheet 双核实):
//   init = 写 0x01(Power On)+ 写 0x10(连续高分辨率,1lx/~120ms),各一笔 transmit;
//   之后传感器持续测量,读 = 收 2 字节(一笔 receive),16-bit **大端**原始值,
//   照度 lx = raw / 1.2(高分辨率模式换算系数)。
//   🔴 连续模式下每次读**不需要**再发命令 → 读就是纯 receive,与写命令是分开的独立
//   事务,**不用 i2c_master_transmit_receive/repeated-start**(与 8Encoder 那类 MCU
//   从机的组合读坑无关,但分开做最稳、与官方库一致)。
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_DLIGHT_ADDR_DEFAULT  0x23

/**
 * @brief 挂载并初始化 DLight 单元(发上电 + 选连续高分辨率模式,并探在位)。
 *
 * 可重复调用:设备句柄只挂一次,但每次都会重发上电+选模式命令 —— 因此深度省电
 * 切 5V 令单元断电复位后,直接再调本函数即可重新接管(模式会被重新配好)。
 *
 * @param bus  外接 I2C 总线(core2_board_port_a())。
 * @param addr 7 位地址;传 0 = 默认 0x23。
 * @return ESP_OK 就绪;ESP_ERR_INVALID_ARG 总线为 NULL;
 *         其余 = I2C 通信失败(没插/没电/插错口):
 *         INVALID_RESPONSE=NACK(0x23 无应答,多半插错口:红色 PORT.A 在机身侧面);
 *         TIMEOUT=总线拉死(线缆/供电/上拉)。用 core2_board_port_a_scan() 一扫便知。
 */
esp_err_t unit_dlight_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 读回当前照度(lx)。连续模式下随时可读,拿到的是最近一次测量值。
 *
 * @param lux 输出照度(勒克斯,float;强光/量程顶会饱和在 ~54612 附近)。
 * @return ESP_OK 成功;ESP_ERR_INVALID_STATE 未 init;ESP_ERR_INVALID_ARG lux 为 NULL;
 *         其余(TIMEOUT/INVALID_RESPONSE)= I2C 读失败(拔线/断电),调用方据此判丢失。
 */
esp_err_t unit_dlight_read_lux(float *lux);

#ifdef __cplusplus
}
#endif
