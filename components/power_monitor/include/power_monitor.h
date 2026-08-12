// power_monitor —— AXP192 电池 / USB 读数(ADC 直读)
//
// 平台原来完全看不到电量:既不能在低电时提示,也没法回答
// "DEEP 里到底还在耗多少电"。本组件把 AXP192 的电池 ADC 读出来,给三个用户:
//   ① core2_sleep:低电自动关机 + DEEP 期间定期打电量日志(§5.1「先量,别猜」);
//   ② launcher / clock_turn:屏上电池指示(家长看的,不是给幼儿的信息);
//   ③ 任何想在自己 UI 上显示电量的卡带。
//
// 🔴 **插着 USB 时电池电流读数没有参考价值**:VBUS 在位时系统吃 USB 的电、电池在充电,
//    放电电流恒 ~0。要量真实待机功耗必须拔 USB 跑(而拔了就没串口)——实用办法是
//    看屏上电压:静置前后各读一次 bat_mv,除以时长得 mV/h。
//
// 寄存器与换算 **Confirmed via github.com/m5stack/M5Core2 src/AXP192.cpp + src/AXP.cpp**:
//   0x00 bit5=VBUS 在位 / bit2=正在充电;0x82=ADC 使能(写 0xFF 全开)
//   0x78 12bit 电池电压 ×1.1mV;0x7A 13bit 充电流、0x7C 13bit 放电流,均 ×0.5mA
//   0x5A 12bit VBUS 电压 ×1.7mV
//   12bit = (hi<<4)|lo(lo 仅低 4 位有效);13bit = (hi<<5)|lo(lo 仅低 5 位有效)
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  bat_mv;    // 电池电压 mV(单节锂电,满 ~4.2V)
    int  bat_ma;    // 电池电流 mA:>0 = 正在充电,<0 = 正在放电
    int  vbus_mv;   // VBUS(USB)电压 mV
    int  pct;       // 粗略电量 0~100(按开路电压曲线估,带载时偏低)
    bool usb;       // VBUS 在位(插着 USB / 底座供电)
    bool charging;  // AXP192 报告正在充电
} power_status_t;

/** @brief 使能 AXP192 电池/VBUS ADC(REG 0x82)。须在 core2_power_init 之后调用
 *  (复用其 AXP192 句柄);core2_board_init 已代调,应用一般不必自己调。 */
esp_err_t power_monitor_init(void);

/** @brief 读一次电量状态(3 笔 I2C,~1ms)。
 *  @return ESP_OK;失败时 *out 全 0,调用方应把 bat_mv==0 当作"读不到,别据此决策"。 */
esp_err_t power_monitor_read(power_status_t *out);

/** @brief 电压 → 粗略电量 %(分段线性,4.15V=100% / 3.30V=0%)。
 *  只是给人看的量级,不是库仑计;带载时电压被拉低,读数会偏保守。 */
int power_monitor_pct(int bat_mv);

#ifdef __cplusplus
}
#endif
