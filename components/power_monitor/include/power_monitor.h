// power_monitor —— AXP192 遥测(电池/VBUS 电压电流、充电状态、库仑计)
//
// 复用 core2_power 已绑定的 AXP192 设备句柄(core2_power_read_regs 原语),不二次
// i2c_master_bus_add_device。power_lab / unit_bench 等评估 app 用它读"这台机器自己
// 的功耗状态",本组件只做**一次性读全量**,不做后台采样任务/历史环形缓冲(那是 app 层
// 的事,见 CLAUDE.md §4)。
//
// 寄存器地址/换算公式来源(AGENTS.md §1 铁律:本会话 Espressif MCP 不可用,退路是
// WebFetch 核对 M5Stack 官方库源码,拿不到就砍、不编造)——
//
//   Confirmed via m5stack/M5Core2 (github.com/m5stack/M5Core2,
//   raw.githubusercontent.com/m5stack/M5Core2/master/src/AXP192.cpp,2026-07-17 抓取):
//     GetBatVoltage()    reg 0x78(12-bit),ADCLSB = 1.1 mV/count
//     GetBatCurrent()    充电 reg 0x7A(13-bit)/ 放电 reg 0x7C(13-bit),
//                        ADCLSB = 0.5 mA/count,净值 = 充电 - 放电
//     GetVBusVoltage()   reg 0x5A(12-bit),ADCLSB = 1.7 mV/count
//     GetVBusCurrent()   reg 0x5C(12-bit),ADCLSB = 0.375 mA/count
//     电源状态           reg 0x00(8-bit):bit5(0x20)=VBUS 在位,bit2(0x04)=正在充电
//     库仑计             GetCoulombchargeData()=Read32bit(0xB0)、
//                        GetCoulombdischargeData()=Read32bit(0xB4)、
//                        控制 reg 0xB8(EnableCoulombcounter 写 0x80、
//                        ClearCoulombcounter 写 0xA0);
//                        mAh = 65536 * 0.5 * (charge-discharge) / 3600.0 / 25.0
//     ADC 使能           SetAdcState(true) 写 reg 0x82 = 0xFF(开全部 ADC 通道,
//                        含 VBUS 电压/电流——不确定 BSP 是否已默认开启,init 时主动写一次)
//
//   Confirmed via m5stack/M5StickC (github.com/m5stack/M5StickC,
//   raw.githubusercontent.com/m5stack/M5StickC/master/src/AXP192.cpp,2026-07-17 抓取,
//   交叉核对 12/13-bit ADC 寄存器字节序):充电电流读法 `icharge = (buf[0]<<5) + buf[1]`
//   ——高字节在基址寄存器、低字节在 base+1,13-bit = (hi<<5)|lo。12-bit 同理按 M5Unified
//   (github.com/m5stack/M5Unified,src/utility/AXP192_Class.hpp)确认的电池电压读法
//   "高字节 0x78、低 4 位 0x79&0x0F、raw=(hi<<4)|lo" 类推,X-Powers AXP192 全系列 ADC
//   寄存器统一采用此"高字节 + 低字节低若干位"布局,两份官方驱动源码交叉印证。
//
//   ⚠️ **未逐字节核实到 Read32bit() 的字节序实现**(M5Core2 源码只给出调用点
//   `Read32bit(0xB0)`,未取到该函数体);本组件按 AXP192 其余多字节寄存器一致的大端序
//   假设实现,若实机库仑计读数明显不合理,优先怀疑这里的字节序,详见 README「待查证」。
//
//   ⚠️ **内部温度 ADC(reg 0x5E)寄存器地址已核实但本组件不暴露**——评估台优先级更高的是
//   电压/电流,温度字段留待后续按同一套路补(不是查不到,是范围裁剪)。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   batt_mv;           // 电池电压 mV
    int   batt_charge_ma;    // 电池充电电流 mA(未充电时读数应≈0,ADC 噪声下可能有个位数残留)
    int   batt_discharge_ma; // 电池放电电流 mA(充电中读数应≈0)
    bool  vbus_present;      // VBUS(USB)是否在位
    int   vbus_mv;           // VBUS 电压 mV(vbus_present=false 时无意义)
    int   vbus_ma;           // VBUS 电流 mA(vbus_present=false 时无意义)
    bool  charging;          // 是否正在充电(reg 0x00 bit2)
    float coulomb_mah;       // 库仑计估算 mAh(自上次 reset 起累计净变化;字节序待查证,见头注释)
} power_telemetry_t;

/**
 * @brief 初始化:开全部 AXP192 ADC 通道 + 使能库仑计。
 *        复用 core2_power 已绑定的 AXP192 句柄,须在 core2_power_init() 成功之后调用。
 * @return ESP_OK 成功;ESP_ERR_INVALID_STATE core2_power 尚未初始化;
 *         其余 = I2C 写失败。
 */
esp_err_t power_monitor_init(void);

/** @brief 一次读全量遥测。任一寄存器读失败即返回错误,out 内容不完整勿用。 */
esp_err_t power_monitor_read(power_telemetry_t *out);

/** @brief 库仑计清零(ClearCoulombcounter,reg 0xB8 写 0xA0),重新开始累计。 */
esp_err_t power_monitor_coulomb_reset(void);

#ifdef __cplusplus
}
#endif
