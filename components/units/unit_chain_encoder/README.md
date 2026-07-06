# unit_chain_encoder —— Chain Encoder(U207)最小驱动

M5Stack **Chain 系列**旋转编码器节点:AB 编码器(方向+计数)+ 中心按键 + 1 颗 RGB,
节点 MCU **STM32G031**,走 **Chain UART 级联**(**无 I2C 地址**)。硬件事实唯一出处:
`docs/units/Chain_Encoder.md`。传输层 = [`chain_bus`](../chain_bus/README.md)(Core2 PORT.C/UART2)。

## 用法

```c
#include "chain_bus.h"
#include "unit_chain_encoder.h"

chain_bus_init_port_c();
if (unit_chain_encoder_probe(1) == ESP_OK) {          // 读设备类型确认是 encoder
    int16_t v; unit_chain_encoder_read_value(1, &v);  // 绝对计数(应用自算帧间 delta)
    bool pressed; unit_chain_encoder_read_button(1, &pressed);
}
// 板载 RGB / 亮度用 chain_bus_set_rgb(1,0,...) / chain_bus_set_rgb_brightness(1,...)
```

## 命令码(M5Chain ChainEncoder,应答载荷小端)

| 命令 | 值 | 应答载荷 |
|---|---|---|
| GET_VALUE | `0x10` | int16 绝对计数 |
| GET_INC | `0x11` | int16 增量 |
| RESET_VALUE | `0x13` | 操作状态 |
| BUTTON_GET_STATUS | `0xE1` | `[0]` = 1 按下 / 0 松开 |

## 注意

- 计数是**相对量**(无绝对角度),会回绕(int16)。应用取"绝对计数 + 自算 delta"最稳,
  不依赖 `GET_INC` 的读后是否自清(库里另有 RESET_INC,故 GET_INC 未必自清,少用)。
- 顺时针增 = 默认 AB 相位(可用 M5Chain 的 SET_AB_STATUS 反相,本驱动未暴露,应用侧取反即可)。
- 供电/接线/省电坑见 `chain_bus` README(PORT.C 5V=EXTEN;深度省电切 5V → 节点复位 → 重新 probe)。
