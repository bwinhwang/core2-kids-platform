// platform_pins.h —— Core2 v1.0 + M5GO Bottom2 的【固定】引脚/地址常量
//
// 这些是平台硬件事实,跨应用不变,取自 docs/platform/Core2_v1_0.md 与
// M5GO_Bottom2.md(已与原理图核对)。**不要在这里放应用可调参数**(那些在
// app_core/app_tuning.h);**不要凭印象改引脚**(改板级事实先回平台文档核对)。
#pragma once

#include "driver/i2c_master.h"   // i2c_port_t / I2C_NUM_0
// 注:BSP_LCD_SPI_HOST 用 SPI2_HOST,但不在此 include spi 头(避免把 SPI 依赖强加给
// 所有 include bsp.h 的地方)。该宏只在 bsp_display.c 展开,那里已 include spi_master.h。

// ───────────────────────── 内部 I2C 总线(G21/G22)──────────────────────────
// 挂 AXP192 0x34 / MPU6886 0x68 / FT6336U 0x38 / BM8563 0x51(Core2 §4)
#define BSP_I2C_INTERNAL_PORT   I2C_NUM_0
#define BSP_PIN_I2C_INT_SDA     21
#define BSP_PIN_I2C_INT_SCL     22
#define BSP_I2C_INTERNAL_HZ     400000

// I2C 从机地址(内部总线)
#define BSP_ADDR_AXP192         0x34
#define BSP_ADDR_FT6336U        0x38
#define BSP_ADDR_BM8563         0x51
#define BSP_ADDR_MPU6886        0x68

// ───────────────────────── NS4168 I2S 功放(Core2 §3)────────────────────────
// SPK_EN 走 AXP192 IO2(不是 GPIO),由 bsp_power 控制
#define BSP_PIN_I2S_BCLK        12     // strapping(MTDI)
#define BSP_PIN_I2S_WS          0      // LRCK,strapping
#define BSP_PIN_I2S_DOUT        2      // strapping

// ───────────────────────── SK6812 ×10 灯条(Bottom2 §2,单数据线 G25)────────
#define BSP_PIN_LED_DATA        25
#define BSP_LED_COUNT           10
// 逻辑 index → 物理灯珠顺序:默认恒等,**上板逐颗点亮实测后修改**(Bottom2 §2)
#define BSP_LED_LAYOUT          {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}

// ───────────────────────── ILI9342C 屏(SPI,Core2 §3)───────────────────────
// RST/背光/逻辑电走 AXP192(IO4/DC3/LDO2),不是 GPIO
#define BSP_LCD_SPI_HOST        SPI2_HOST
#define BSP_PIN_LCD_MOSI        23     // 与 SD 共用
#define BSP_PIN_LCD_MISO        38     // 输入只读脚
#define BSP_PIN_LCD_SCLK        18
#define BSP_PIN_LCD_CS          5      // strapping,板载已占
#define BSP_PIN_LCD_DC          15     // strapping,板载已占
#define BSP_PIN_SD_CS           4      // SD 独立片选(与屏共 SPI 总线)
#define BSP_LCD_PCLK_HZ         (40 * 1000 * 1000)
#define BSP_LCD_W               320    // ILI9341 竖屏 240×320 → 旋成横屏 320×240
#define BSP_LCD_H               240

// ───────────────────────── FT6336U 触摸(内部 I2C 0x38,Core2 §3)────────────
#define BSP_PIN_TOUCH_INT       39     // 输入只读脚;RST 走 AXP192 IO4
