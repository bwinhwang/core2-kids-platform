# chain_bus —— M5Stack Chain 系列 UART 级联总线传输层

Chain 系列节点(Chain Encoder / Joystick / Key / Angle / ToF …)**不是 Grove I2C 单元**:
同为 HY2.0-4P,但走 **UART 115200 8N1 菊花链**。本组件让 **Core2 直接作 Chain host**,
把帧协议(与官方 `m5stack/M5Chain` 库逐字节对齐)封成"请求/应答"一次事务,并自动跳过
节点主动上报的心跳/枚举包。是 `unit_chain_encoder` / `unit_chain_joystick` 的共同底座。

硬件事实唯一出处:`docs/units/Chain_Encoder.md`、`docs/units/Chain_Joystick.md`、
`docs/platform/M5GO_Bottom2.md` §3(PORT.C)。

## 接线(Core2 PORT.C = UART2)

| Grove 线 | Core2 GPIO | 作用 |
|---|---|---|
| Yellow | **G13** | Core2 RXD2(收节点回帧) |
| White | **G14** | Core2 TXD2(发请求给节点) |

- 节点 **IN 口朝主控**(外壳三角箭头从主控指向外侧),经随附 **Chain Bridge** 桥接;接反整链不通。
- 单节点直连 → 链上位置 `id=1`;两节点级联 → 按物理顺序 `id=1,2`。

## 用法

```c
#include "chain_bus.h"

chain_bus_init_port_c();                       // 装 UART2 @115200 8N1(G14 TX / G13 RX)

chain_dev_type_t t;
if (chain_bus_get_device_type(1, &t, 60) == ESP_OK) { /* 链位 1 在位,t=类型码 */ }

// 底层通用事务:发 (id,cmd,data),等匹配 (id,cmd) 应答,拷回载荷(id/cmd 之后、crc 之前)
uint8_t p[4], n;
chain_bus_request(1, 0x30 /*JOY_GET_16ADC*/, NULL, 0, p, sizeof(p), &n, 40);

chain_bus_set_rgb_brightness(1, 40, 40);       // 节点板载 RGB 亮度档 0~100
chain_bus_set_rgb(1, 0, 255, 128, 0, 40);      // 第 0 颗灯设色
```

## 帧格式(实现依据,`chain_bus.c` 与 M5Chain ChainCommon 对齐)

```
主控发:  AA 55 | lenLo lenHi | id | cmd | data... | crc | 55 AA
节点回:  AA 55 | lenLo lenHi | id | cmd | payload... | crc | 55 AA
```
- 帧头 `AA 55`,帧尾 `55 AA`。
- `len`(小端)= `id + cmd + data + crc` 的字节数 = `3 + dataLen`;整帧 = `dataLen + 9`。
- `crc` = `(id + cmd + data 全部字节)` 求和取低 8 位。
- 应答的 **载荷** = `id/cmd` 之后、`crc` 之前的字节(即库里 `returnPacket[6..]`);
  多值为**小端**(如 encoder 计数 int16、joystick ADC uint16)。
- 节点还会主动发心跳(约 1/s,cmd `0xFD`)、枚举请求(`0xFC`)、按键上报(`0xE0`)——
  `chain_bus_request` 发前 `uart_flush_input` 清积压,收时按 (id,cmd) 逐帧匹配、其余丢弃。

## 坑 / 注意

- 🔴 **供电 = PORT.C 5V = M-Bus 5V(EXTEN)**,与 PORT.A 单元同源:电池供电时 EXTEN 没开
  节点没电(插 USB 时 VBUS 直通会掩盖)。`core2_board_init(enable_leds=true)` 已代开 EXTEN;
  不用灯带的应用需 `core2_power_bus_5v(true)`。**深度省电切 5V → 节点断电复位**,唤醒后重新 probe。
- 🔴 **插蓝口 PORT.C**(不是红 PORT.A/黑 PORT.B)。Chain 不能当 I2C 扫,也不直连主控 ADC。
- ⚠️ **Core2 直连 Chain host 未经官方背书**(硬件文档 §2 标注未验证):官方范式是 Chain
  节点挂在独立 Chain 主控(如 Chain DualKey)后面。本组件按 M5Chain 库实现,理应可直连;
  上板不通时用 `chain_bus_sniff(ms)` 抓 PORT.C 原始字节——**有心跳 = 链路通只是没认到;
  一个字节都收不到 = 没供电/接反/直连不成立**,据此定位。
- UART 用 **UART_NUM_2**(经典 ESP32 三个 UART:0=日志,1 空闲,2 给 PORT.C),与 BSP 不冲突。
- 每次事务 flush 输入 → 会丢掉两次事务间到达的按键上报包(`0xE0`);本平台用轮询
  `getButtonStatus`(`0xE1`)读按键,不依赖上报,无影响。
