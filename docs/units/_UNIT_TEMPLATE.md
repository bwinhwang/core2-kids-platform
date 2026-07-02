# UNIT __UNIT__ —— 硬件 Ground Truth

> 某个 M5 UNIT 外设的板级硬件事实,仿平台文档(docs/platform/Core2_v1_0.md)的结构。
> 每接入一个新 UNIT 就沉淀一份本文件。**项目逻辑请勿写这里**,放 CLAUDE.md。
>
> ⚠️ **主控固定为 Core2 v1.0 + M5GO Bottom2**。引脚一律以 §2 的 Core2 三口映射(取自
> `components/bsp_core2/include/bsp_port.h`)为准 —— **不要照抄别的板子(如 AtomS3 Lite 的
> G1/G2)的脚**。命名:文档文件名用 `Unit_<名>.md` / `Chain_<名>.md`;组件名一律小写
> `components/units/unit_<名>`。

- **接入口:PORT.__PORT__**
- **来源**:<M5 产品页 PDF / SKU、板载芯片 datasheet —— 查 MCP / 厂商源后写,见 AGENTS.md §1>

---

## 1. 一句话定位

<是什么、核心 IC、测什么/输出什么、关键量程。例:超声波测距,核心 RCWL-9620,量程 2~450cm>

---

## 2. 接口与接线(Core2 PORT.__PORT__)

| 项 | 值 |
|---|---|
| 接入口 | PORT.__PORT__ |
| 接口类型 | I2C / UART / ADC / DAC / GPIO |
| I2C 地址(若 I2C) | 0x?? |
| 供电 | 5V 输入 / 逻辑 3V3 |

**Core2 三口引脚(固定,取自 `bsp_port.h`)—— 按接口类型选对应那口:**

| PORT | Grove 线 → Core2 GPIO | 用途 |
|---|---|---|
| **A** | Yellow=**G32**(SDA) / White=**G33**(SCL) | 外部 I2C,`I2C_NUM_1`,与内部总线物理隔离 |
| **B** | Yellow=**G26**(DAC/可输出) / White=**G36**(ADC,**输入只读**) | 模拟/GPIO;输出只能用 G26,G36 不能驱动 |
| **C** | Yellow=**G13**(RXD2) / White=**G14**(TXD2) | UART2,可直接挂 Chain 系列(115200 8N1) |

> Grove 线序:Black=GND / Red=5V / Yellow / White。信号落哪个 Core2 脚按上表对号,别抄 AtomS3。
> **I2C 地址冲突**:PORT.A 是独立总线,与内部 0x34/0x38/0x51/0x68 **物理隔离**,无需担心撞内部;
> 只需保证 PORT.A 上**多个 UNIT 之间**地址不撞。

---

## 3. 寄存器 / 协议要点

<关键寄存器、命令帧、时序、单位换算。查 MCP / 厂商 GitHub 后填,并写一句 `Confirmed via <来源>`,
不要凭训练数据猜(AGENTS.md §1)。非 I2C(UART/Chain)填帧格式;纯 GPIO/模拟填电平/时序。>

---

## 4. 电源

<5V 输入 / 板载 LDO 降 3V3 / I2C 逻辑电平 / 工作电流。幼儿应用防接错,单列一节。>

---

## 5. 驱动映射(接到本平台代码)

- 组件:`components/units/unit___UNIT__`(名字小写)
- 托管驱动(若有):`<namespace/component>`(在组件 `idf_component.yml` 声明)
- 接入代码:
  - **I2C UNIT**:`unit___UNIT___init(bsp_i2c_port_a(), 0x??)`
  - **GPIO / ADC / DAC / UART**:用 `bsp_port.h` 对应宏(`BSP_PIN_PORTB_DAC` / `BSP_PIN_PORTB_ADC` / `BSP_PORT_C_UART_NUM` 等)

---

## 6. 关键注意事项 / 易错点

<上板踩坑随手记(仿平台文档的"现象→结论"),如:上电稳定期、近距盲区、读太快拿旧值、
接线方向(Yellow/White 哪根是信号)、供电不稳读数跳变…>

---

## 7. 没有的东西 / 常见误解

<防误用一句话清单:不是 XXX、只有 I2C 没有模拟输出、输出是相对量无绝对角度、Chain≠I2C…>
