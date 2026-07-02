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

## 4. 协议

- 具体寄存器/帧格式见官方 **Chain Joystick 通信协议**文档(本 PDF 仅给链接,未含正文)。
- 可读:X / Y 模拟值、Z 按键;可写:RGB 颜色、节点地址等(以协议文档为准)。

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
