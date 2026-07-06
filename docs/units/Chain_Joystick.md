# CHAIN Joystick —— 硬件 Ground Truth

本文件是 **Chain Joystick** 这颗外设节点的板级硬件事实,可复制到任意使用 Chain 总线的项目。**项目特定逻辑请勿写在这里**,放 `CLAUDE.md`。

来源:
- M5Stack 官方产品页 PDF(`Chain_Joystick.pdf`,SKU **U205**)
- 节点 MCU datasheet:**STM32G031G8U6**、灯珠 **WS2812C**

---

## 1. 一句话定位

M5Stack **Chain 系列**高精度霍尔电磁摇杆输入节点。**X/Y 模拟量 + Z 按键**三轴输入,无接触、耐磨、抗干扰。节点自带 **STM32G031G8U6**,对外走 **UART 串口级联**,不是 I2C/GPIO。

---

## 2. 总线 / 接线(Chain = UART 级联)

- 通信:**UART,115200 bps,8N1**。
- 接口:**2× HY2.0-4P**(IN / OUT),用随附 **Chain Bridge** 桥接。
- **方向有意义**:三角箭头从主控(Chain DualKey 等)指向外侧;`IN` 朝主控、`OUT` 朝下一节点,接反不通信。
- 总线携带 `5V / GND` + 两根串口数据线;主控一组 UART 可挂多个 Chain 节点。
- **接 Core2 PORT.C**(UART2,`BSP_PORT_C_UART_NUM`):Yellow=**G13**(Core2 RXD2)、White=**G14**(Core2 TXD2)。Bottom2 §3 明确 PORT.C 可直挂 Chain 系列(115200 8N1);**别抄 AtomS3 的脚**。
- **驱动映射(本平台)**:组件 `components/units/unit_chain_joystick`;走 PORT.C UART2,用 `BSP_PIN_PORTC_RX(G13)` / `BSP_PIN_PORTC_TX(G14)`;帧格式见 §4(写代码前查 MCP / 官方 Chain 协议文档,AGENTS.md 铁律)。
- ⚠️ Core2 能否**直接**作 Chain host(不经独立 Chain 主控如 DualKey)未上板验证;§6 表述以官方协议为准。
- 工作电流:**25.4mA**。工作温度 0~40℃。

> 物理:23.9 x 23.9 x D30.4 mm,8.6 g。
> 随包装含 **1× Chain Bridge**。

---

## 3. 节点内部引脚(STM32G031 侧,仅供参考)

> ⚠️ 下表是节点内部 STM32 的连线,**不是主控/host 的接线**。主控通过 Chain UART 协议访问。

| 功能 | STM32G031 引脚 |
|---|---|
| RGB(WS2812C) | PA8 |
| 按键 Button(Z 轴) | PB0 |
| 摇杆 XOUT / YOUT | PA7 / PA6 |
| UART1(TXD1 / RXD1) | PB6 / PB7 |
| UART2(TXD2 / RXD2) | PA2 / PA3 |

摇杆为霍尔电磁式,X/Y 是模拟量(节点内 ADC 采集后经 UART 上报);Z 为数字按键。

---

## 4. 协议(帧格式已从官方库核实)

> **Confirmed via github m5stack/M5Chain**(`src/ChainCommon`、`src/ChainJoystick`):
> 官方 PDF 只给链接,帧格式实取自官方 Arduino 库源码。本平台实现见
> `components/units/chain_bus`(传输)+ `components/units/unit_chain_joystick`(设备)。

- **一帧**:`AA 55 | lenLo lenHi | id | cmd | data.. | crc | 55 AA`。`len`(小端)= `3+dataLen`;
  `crc` = `(id+cmd+data)` 求和低 8 位;`id` = 链上位置(1 起,单节点直连=1)。应答同格式,
  载荷 = id/cmd 之后、crc 之前的字节,多值**小端**。
- **可读**:16 位 ADC `GET_16ADC=0x30`(载荷 x·uint16 + y·uint16)/ 8 位 `GET_8ADC=0x31` /
  节点内映射后居中值 `GET_MAPPED_INT16=0x34`(需先配 mapped range)/ Z 按键 `BUTTON_GET_STATUS=0xE1`
  (载荷[0]:1 按下)/ 设备类型 `0xFB`(Joystick=0x0004)/ 固件版本 `0xFA`。
- **可写**:RGB `SET_RGB_VALUE=0x20`(data=[index,num,r,g,b])/ RGB 亮度 `SET_RGB_LIGHT=0x22`(0~100)/
  映射范围 `SET_ADC_XY_MAPPED_RANGE=0x33` / 节点地址等。
- 🔴 **个体零偏**:原始 ADC 静止未必居中,上电采一次居中值做**软件归中**再归一化(见 chain_lab)。
- 节点主动发心跳(`0xFD`,约 1/s)/ 枚举(`0xFC`)/ 按键上报(`0xE0`);主机轮询时按 (id,cmd) 匹配、其余丢弃。

---

## 5. 电源

- 输入 **DC 5V**(经 Chain 总线),节点内部 LDO(ME6206A33XG)降 3V3 供 STM32 + WS2812C。

---

## 6. 关键注意事项 / 易错点

- **Chain ≠ Grove I2C**:同是 HY2.0-4P 但走 UART 级联,**不能**当 I2C/模拟摇杆单元直接接到主控 ADC。
- 接线方向(IN/OUT、箭头朝外)接反会整链不通。
- 必须用 **Chain Bridge** + 一个 Chain 主控(如 Chain DualKey)发起总线。
- X/Y 中点/量程标定看协议文档;不同个体有零偏,建议上电做一次居中校准。

---

## 7. 没有的东西 / 常见误解

- **没有 I2C 地址**,也**不直接输出模拟电压给 host**;X/Y 经节点数字化后走 UART。
- 内部 STM32 引脚不引出,别想直接采 PA6/PA7。
