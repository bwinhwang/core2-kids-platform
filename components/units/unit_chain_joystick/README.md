# unit_chain_joystick —— Chain Joystick(U205)最小驱动

M5Stack **Chain 系列**霍尔电磁摇杆节点:X/Y 模拟量 + Z 按键 + 1 颗 RGB,节点 MCU
**STM32G031**,走 **Chain UART 级联**(**无 I2C 地址、不直接输出模拟电压**;X/Y 由节点
ADC 数字化后经 UART 上报)。硬件事实唯一出处:`docs/units/Chain_Joystick.md`。传输层 =
[`chain_bus`](../chain_bus/README.md)(Core2 PORT.C/UART2)。

## 用法

```c
#include "chain_bus.h"
#include "unit_chain_joystick.h"

chain_bus_init_port_c();
if (unit_chain_joystick_probe(1) == ESP_OK) {              // 读设备类型确认是 joystick
    uint16_t x, y; unit_chain_joystick_read_adc(1, &x, &y);// 原始 ADC(约 12 位,居中 ~2048)
    bool pressed; unit_chain_joystick_read_button(1, &pressed);
}
// 板载 RGB / 亮度用 chain_bus_set_rgb(1,0,...) / chain_bus_set_rgb_brightness(1,...)
```

## 命令码(M5Chain ChainJoystick,应答载荷小端)

| 命令 | 值 | 应答载荷 |
|---|---|---|
| GET_16ADC | `0x30` | x(uint16)+ y(uint16) |
| GET_8ADC | `0x31` | x(uint8)+ y(uint8) |
| BUTTON_GET_STATUS | `0xE1` | `[0]` = 1 按下 / 0 松开 |

## 注意

- 🔴 **个体有零偏**(硬件文档 §6):上电时采一次居中 ADC 做**软件归中**,再归一化到 [-1,1]
  (chain_lab 即这么做)。别假设居中就是 2048。
- 本驱动只暴露**原始 ADC**(最稳、免依赖节点内映射表);M5Chain 另有 MAPPED_INT16/INT8
  命令返回节点内映射后的居中值,但依赖先配 mapped range,验证台不用。
- 供电/接线/省电坑见 `chain_bus` README(PORT.C 5V=EXTEN;深度省电切 5V → 节点复位 → 重新 probe)。
