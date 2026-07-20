// Unit CO2L(U104)最小驱动 —— I2C CO₂/温/湿传感器,芯片 Sensirion SCD41,地址 0x62
//
// 硬件事实见 docs/units/Unit_SCD41.md(CO₂ 400~5000ppm ±(40ppm+5%读数),温 -10~60°C,
// 湿 0~95%RH,内置 5V→3.3V LDO)。接入:unit_scd41_init(core2_board_port_a(), 0)
// —— PORT.A 外接总线(G32/G33)。
//
// ⚠️ 供电坑(同 DLight/8Encoder/超声波):单元吃 PORT.A 的 5V = M-Bus 5V(EXTEN)。电池
//   供电时 EXTEN 没开单元就没电;深度省电切 5V 后单元断电复位(退出周期测量),恢复供电
//   后重调 init 即可(init 会 stop+start 把它重新拉回周期测量态)。详见 core2_board_port_a()。
//
// ⚠️ 测量节奏:SCD41 **周期模式每 5 秒才产出一次新数据**(不像 DLight/超声波即时可读)。
//   正确用法 = init 里 start 一次,之后每次读先 unit_scd41_data_ready() 探"有没有新数",
//   ready 才 unit_scd41_read();两次新数之间沿用上一次读数,别把"数据未就绪"当成读失败。
//
// 读写协议(SCD4x,Confirmed via esp-idf-lib/scd4x.c 源码 + Sensirion SCD4x datasheet):
//   - 命令均为 16-bit,大端两字节发送;命令与后续读之间是**两笔独立事务**(发命令+STOP,
//     等执行时间,再单独 receive),不是 repeated-start。
//   - start_periodic=0x21B1(执行~1ms)/ read_measurement=0xEC05(~1ms,读回 9 字节)/
//     get_data_ready_status=0xE4B8(~1ms,读回 3 字节)/ stop_periodic=0x3F86(**执行 500ms**)。
//   - read 回 9 字节 = 三个 16-bit 字,每字后跟 1 字节 CRC:[CO2 hi,lo,CRC][T hi,lo,CRC][RH hi,lo,CRC]。
//   - CRC-8:多项式 0x31、初值 0xFF、不反射、无最终异或(逐字节 XOR 后 8 次移位)。
//   - data_ready:状态字 & 0x07FF != 0 即有新数据。
//   - 换算:CO2[ppm]=raw 直接;T[°C]=-45 + 175*raw/65536;RH[%]=100*raw/65536。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_SCD41_ADDR_DEFAULT  0x62

/**
 * @brief 挂载并初始化 SCD41 单元(stop→等 500ms→start,把它拉回一个确定的周期测量态)。
 *
 * 可重复调用:设备句柄只挂一次。每次都先 stop_periodic(容忍失败——单元本就 idle 时
 * stop 也被接受)再 start_periodic,因此无论单元此刻是刚上电(idle)还是上一会话残留的
 * 测量态,init 后都统一处于"周期测量已启动"。**init 因 stop 的 500ms 执行时间会阻塞约
 * 0.5s**;它由后台扫描的 try_attach 调用(不持 LVGL 锁),只延后一次 app_task tick,不冻结
 * 渲染。首个数据约 5s 后就绪,调用方轮询 unit_scd41_data_ready()。
 *
 * @param bus  外接 I2C 总线(core2_board_port_a())。
 * @param addr 7 位地址;传 0 = 默认 0x62。
 * @return ESP_OK 就绪;ESP_ERR_INVALID_ARG 总线为 NULL;
 *         其余 = start 命令 I2C 失败(没插/没电/插错口):
 *         INVALID_RESPONSE=NACK(0x62 无应答);TIMEOUT=总线拉死。
 */
esp_err_t unit_scd41_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 探"是否有新一批测量数据就绪"(周期模式下约每 5s 才为 true)。
 *
 * @param ready 输出:true=有新数据可 read;false=本周期尚未产出(不算失败,沿用上次读数)。
 * @return ESP_OK 通信成功(ready 有效);ESP_ERR_INVALID_STATE 未 init;
 *         ESP_ERR_INVALID_ARG ready 为 NULL;ESP_ERR_INVALID_CRC 校验失败;
 *         其余(TIMEOUT/INVALID_RESPONSE)= I2C 失败(拔线/断电),调用方据此判丢失。
 */
esp_err_t unit_scd41_data_ready(bool *ready);

/**
 * @brief 读回最近一批 CO₂/温/湿(应先 data_ready 为 true 再调,否则拿到的是旧值)。
 *
 * @param co2_ppm 输出 CO₂ 浓度(ppm,uint16;可传 NULL 跳过)。
 * @param temp_c  输出温度(°C,float;可传 NULL)。
 * @param rh_pct  输出相对湿度(%,float;可传 NULL)。
 * @return ESP_OK;ESP_ERR_INVALID_STATE 未 init;ESP_ERR_INVALID_CRC 校验失败;
 *         其余 = I2C 读失败(拔线/断电),调用方据此判丢失。
 */
esp_err_t unit_scd41_read(uint16_t *co2_ppm, float *temp_c, float *rh_pct);

#ifdef __cplusplus
}
#endif
