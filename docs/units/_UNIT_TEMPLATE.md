# UNIT __UNIT__ —— 硬件 Ground Truth

> 某个 M5 UNIT 外设的硬件事实,仿平台文档(docs/platform/Core2_v1_0.md)的结构。
> 每接入一个新 UNIT 就沉淀一份本文件。**项目逻辑请勿写这里**,放 CLAUDE.md。

接入口:**PORT.__PORT__**

---

## 0. 一句话定位
<这个 UNIT 是什么、测什么、输出什么(如:超声波测距 3~450cm)>

## 1. 接线 / 端口
| 项 | 值 |
|---|---|
| 接入口 | PORT.__PORT__ |
| 接口类型 | I2C / UART / ADC / DAC |
| I2C 地址(若 I2C) | 0x?? ⚠️ 确认不撞内部总线 0x34/0x38/0x51/0x68 |
| 供电 | 5V / 3V3 |
| 对应 Core2 GPIO | 见 docs/platform/M5GO_Bottom2.md §3 |

## 2. 寄存器 / 协议要点
<关键寄存器、命令帧、时序、单位换算。查 MCP / 厂商 GitHub 后填,
并写一句 `Confirmed via <来源>`,不要凭训练数据猜(AGENTS.md §1)>

## 3. 驱动映射
- 组件:`components/units/unit___UNIT__`
- 托管驱动(若有):`<namespace/component>`(在组件 idf_component.yml 声明)
- 接入代码:`unit___UNIT___init(bsp_i2c_port_a(), 0x??)`

## 4. 坑 / 现象 → 结论
<上板踩坑随手记(仿平台文档的“现象→结论”),如:读数跳变=供电不稳/线太长>
