// bsp_port.h —— 三个 Grove 扩展口的【固定】引脚(UNIT 外设的接入点)
//
// 叠上 Bottom2 后,Core2 系统共有 3 个 Grove 口(Core2 §4 + Bottom2 §3):
//   PORT.A = 外部 I2C(独立于内部总线,不撞 0x34/0x38/0x51/0x68)
//   PORT.B = ADC(只读) + DAC(真模拟输出)
//   PORT.C = UART2
// 新增一个 UNIT 外设:看它用哪个口,复制 components/units/unit_template。
#pragma once

#include "driver/i2c_master.h"

// ───────────────────────── PORT.A —— 外部 I2C(G32/G33)──────────────────────
// 独立 I2C 控制器,与内部总线物理隔离;外接 M5 I2C UNIT(超声波/手势/光线…)走这里
#define BSP_PORT_A_I2C_PORT     I2C_NUM_1
#define BSP_PIN_PORTA_SDA       32
#define BSP_PIN_PORTA_SCL       33
#define BSP_PORT_A_I2C_HZ       100000   // Grove I2C UNIT 多为 100k;个别可上 400k

// ───────────────────────── PORT.B —— ADC/DAC(Bottom2 §3)────────────────────
#define BSP_PIN_PORTB_DAC       26       // 真模拟输出(G26)
#define BSP_PIN_PORTB_ADC       36       // 输入只读脚(G36)

// ───────────────────────── PORT.C —— UART2(Bottom2 §3)──────────────────────
#define BSP_PORT_C_UART_NUM     2
#define BSP_PIN_PORTC_RX        13       // RXD2(G13)
#define BSP_PIN_PORTC_TX        14       // TXD2(G14)

// 逻辑端口标识(给 UNIT 驱动/文档用)
typedef enum {
    BSP_PORT_A = 0,   // I2C
    BSP_PORT_B,       // ADC/DAC
    BSP_PORT_C,       // UART
} bsp_port_t;
