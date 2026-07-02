# CHAIN Encoder —— 硬件 Ground Truth

本文件是 **Chain Encoder** 这颗外设节点的板级硬件事实,可复制到任意使用 Chain 总线的项目。**项目特定逻辑请勿写在这里**,放 `CLAUDE.md`。

来源:
- M5Stack 官方产品页 PDF(`Chain_Encoder.pdf`,SKU **U207**)
- 节点 MCU datasheet:**STM32G031G8U6**、灯珠 **WS2812C**

---

## 1. 一句话定位

M5Stack **Chain 系列**旋转编码器输入节点。内置 AB 旋转编码器(方向+脉冲计数)+ 旋钮中心按键 + 1 颗可编程 RGB。节点自带 **STM32G031G8U6**,对外走 **UART 串口级联**,不是 I2C/GPIO。

---

## 2. 总线 / 接线(Chain = UART 级联)

- 通信:**UART,115200 bps,8N1**。
- 接口:**2× HY2.0-4P**(一个 IN、一个 OUT),用随附的 **Chain Bridge** 连接器在节点间桥接。
- **方向有意义**:三角箭头从主控(Chain DualKey 等)指向外侧;`IN` 朝主控、`OUT` 朝下一节点,接反不通信。
- 总线携带 `5V / GND` + 两根串口数据线;主控只需一组 UART 即可挂多个 Chain 节点。
- **接 Core2 PORT.C**(UART2,`BSP_PORT_C_UART_NUM`):Yellow=**G13**(Core2 RXD2)、White=**G14**(Core2 TXD2)。Bottom2 §3 明确 PORT.C 可直挂 Chain 系列(115200 8N1);**别抄 AtomS3 的脚**。
- **驱动映射(本平台)**:组件 `components/units/unit_chain_encoder`;走 PORT.C UART2,用 `BSP_PIN_PORTC_RX(G13)` / `BSP_PIN_PORTC_TX(G14)`;帧格式见 §4(写代码前查 MCP / 官方 Chain 协议文档,AGENTS.md 铁律)。
- ⚠️ Core2 能否**直接**作 Chain host(不经独立 Chain 主控如 DualKey)未上板验证;§6 表述以官方协议为准。
- 待机电流:**22.51mA**。工作温度 0~40℃。

> 物理:23.9 x 23.9 x 29.8 mm,10.0 g。编码器帽带 LEGO 兼容孔。
> 随包装含 **1× Chain Bridge**。

---

## 3. 节点内部引脚(STM32G031 侧,仅供参考)

> ⚠️ 下表是节点内部 STM32 的连线,**不是主控/host 的接线**。主控通过 Chain UART 协议访问,无需关心这些脚。

| 功能 | STM32G031 引脚 |
|---|---|
| RGB(WS2812C) | PA8 |
| 编码器 A1 / B1 / 按键 BTN1 | PA6 / PA7 / PB0 |
| UART1(TXD1 / RXD1) | PB6 / PB7 |
| UART2(TXD2 / RXD2) | PA2 / PA3 |

两路 UART 分别对应级联的两个方向(IN/OUT),节点在内部转发,实现菊花链。

---

## 4. 协议

- 具体寄存器/帧格式见官方 **Chain Encoder 通信协议**文档(本 PDF 仅给链接,未含正文)。
- 可读:旋转方向 / 脉冲计数 / 按键状态;可写:RGB 颜色、节点地址等(以协议文档为准)。

---

## 5. 电源

- 输入 **DC 5V**(经 Chain 总线),节点内部 LDO(ME6206A33XG)降 3V3 供 STM32 + WS2812C。

---

## 6. 关键注意事项 / 易错点

- **Chain ≠ Grove I2C**:虽然也是 HY2.0-4P,但走 UART 级联,**不能**当普通 I2C 单元接。
- 接线方向(IN/OUT、箭头朝外)接反会整链不通。
- 必须用 **Chain Bridge** 桥接,且需要一个 Chain 主控(如 Chain DualKey)来发起总线。
- 编码器是**相对量**(方向+计数),无绝对角度。

---

## 7. 没有的东西 / 常见误解

- **没有 I2C 地址**,不要去扫 I2C 总线找它。
- 内部 STM32 引脚不引出给 host,别想直接读 PA6/PA7。
