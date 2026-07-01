# HARDWARE.md — M5Stack Base M5GO Bottom2 板级 Ground Truth
 
本文件是底座级硬件事实,可复制到任意"Core2 + M5GO Bottom2"的项目。**项目特定逻辑请勿写在这里**,放 `CLAUDE.md`。
 
来源:
- M5Stack 官方 PinMap PDF(`m5go_bottom2.pdf`,SKU **A014-C**)
- `Base M5GO Bottom2 原理图 PDF`
- 板载芯片 datasheet:**MPU6886**、**SPM1423**、**SK6812**、**TP4057**
---
 
## 0. 一句话定位
 
**仅适配 Core2** 的功能底座(M5-Bus 30 针对接)。给 Core2 加了:**10 颗 SK6812 RGB 灯条 + 数字麦克风 + 6 轴 IMU + 500mAh 电池 + pogo 磁吸充电 + 两个新 Grove 口(PORT.B / PORT.C)**。它**不含主控**,所有外设都挂在 Core2 的 ESP32 GPIO 上。
 
> 最大价值:把 Core2 M5-Bus 上的 **ADC/DAC(PORT.B)** 和 **UART(PORT.C)** 引到了实体 Grove 口。叠上底座后,Core2 系统总共有 **3 个 Grove 口**:PORT.A(Core2 自带,I2C)+ PORT.B + PORT.C。
 
---
 
## 1. 底座给 Core2 加了什么(外设 → 占用的 Core2 GPIO)
 
| 底座外设 | Core2 GPIO | 说明 |
|---|---|---|
| **SK6812 RGB 灯条 ×10**(左右各 5) | DATA = **G25** | 单数据线串 10 颗;磨砂遮光条,柔光 |
| **SPM1423 数字麦克风**(PDM) | CLK = **G0**,DAT = **G34** | **与 Core2 板载 mic 同脚**(见 §6) |
| **MPU6886 6 轴 IMU**(I2C **0x68**) | 内部 I2C G21 / G22 | **与 Core2 BMI270 同地址 0x68**(见 §6) |
| **Pogo pin I2C 引出** | 内部 I2C G21 / G22 | 磁吸口同时引出主控 I2C(见 §4) |
| **PORT.B**(新增) | G26 / G36 | DAC + ADC(见 §3) |
| **PORT.C**(新增) | G13 / G14 | UART2(见 §3) |
 
> 物理:54 x 54 x 15 mm,30 g。内置磁铁,背面 LEGO 兼容孔。随附 M3*16 ×2、M3*18 ×2 螺丝。
 
---
 
## 2. RGB 灯条(10× SK6812)
 
- 10 颗 **SK6812**,**单数据线 G25** 串联(左 5 + 右 5)。
- 协议同 WS2812 时序,GRB,每颗 24-bit;ESP-IDF 用 `led_strip`+RMT,灯数设 **10**。
- ⚠️ **G25 在 Core2 上本是 DAC / M5-Bus 引脚**:叠底座后 G25 被灯条占用,**不能**再当 DAC 外用。
- 幼儿/常态使用降亮度,SK6812 满亮度刺眼且耗流。
---
 
## 3. 新增的两个 Grove 口(底座最实用的部分)
 
### PORT.B —— ADC / DAC / IO
 
| 颜色 | 信号 | Core2 GPIO | 备注 |
|---|---|---|---|
| Black | GND | — | — |
| Red | 5V | — | — |
| Yellow | G26 | 26 | **DAC**(真模拟输出) |
| White | G36 | 36 | **ADC**(**输入只读脚**) |
 
### PORT.C —— UART2
 
| 颜色 | 信号 | Core2 GPIO | 备注 |
|---|---|---|---|
| Black | GND | — | — |
| Red | 5V | — | — |
| Yellow | G13 | 13 | **RXD2** |
| White | G14 | 14 | **TXD2** |
 
> 颜色↔信号顺序按 M5 标准 Grove 约定(B 口 Yellow=第一脚、C 口 Yellow=RXD)。若做精密接线,silk 丝印再核对一次。
> PORT.C 是标准 UART,可直接挂 **Chain 系列(115200 8N1)** 或其它串口传感器,无需再占 Core2 的 PORT.A。
 
---
 
## 4. Pogo Pin(磁吸充电 + I2C)
 
- 底部 4 触点 pogo,磁吸到充电底座时经 **TP4057** 给内部电池充电。
- **同时引出主控 I2C(G21/G22 = SDA/SCL)**,可用磁吸方式外接 I2C 拓展。
- ⚠️ pogo 引出的是**内部 I2C**(和 AXP192/RTC/IMU/触摸同总线),外接设备别和 0x34/0x38/0x51/0x68 撞地址。
---
 
## 5. 电源 / 电池
 
- 内置 **500mAh** 锂电池(替代/补充 Core2 电池功能)。
- 充电芯片:**TP4057**,经 pogo 磁吸口充电。
- 电池/5V 经 M5-Bus 与 Core2 互通(BAT / 5V 针)。
---
 
## 6. 与 Core2 的引脚冲突(必读,最易踩坑)
 
> Core2 v1.3 自身就带 IMU(BMI270)、mic(SPM1423)、电池。底座又带了一套 **MPU6886 / SPM1423 / 500mAh**,两者落在**同一组 GPIO/地址**上,不是相加而是**共用/二选一**:
 
- **IMU 地址撞车**:底座 MPU6886 与 Core2 BMI270 **都在内部 I2C 的 0x68**,同总线无法共存。实际用的是**物理上接着的那一颗**。叠底座通常意味着用底座的 **MPU6886**。
  - ⚠️ **驱动必须匹配实际芯片**:BMI270 与 MPU6886 寄存器完全不同,代码里别写死某一个;按版本/探测结果选驱动(M5Unified 会自动识别)。
- **麦克风共线**:底座 SPM1423 用 **CLK=G0 / DAT=G34**,与 Core2 板载 SPM1423 **同脚**,本质是同一条 PDM 接口,只会有效一颗,不能当两路独立 mic。
- **G25 被灯条占用**:见 §2,叠底座后 G25 不能再做 DAC。
- **结构**:Core2 v1.3 自带震动马达,**勿与普通 Base 系列堆叠**(机械干涉);要叠模块又想保留 mic/IMU/电池,官方推荐的正是 **M5GO Bottom2**。
---
 
## 7. M5-Bus 透传(底座实际使用的针)
 
底座只引出/使用了 M5-Bus 的一部分信号(其余 NC):
 
| 类别 | 信号 |
|---|---|
| RGB LED | G25 |
| PORT.B | G26(DAC)/ G36(ADC) |
| UART(PORT.C) | UART_RX=G13 / UART_TX=G14 |
| 内部 I2C(IMU/pogo) | IMU_SDA=G21 / IMU_SCL=G22 |
| I2S(mic) | I2S_LRCK / I2S_DIN(对应 G0 / G34 一侧) |
| 电源 | 3V3 / 5V / BAT / GND |
 
> 精确针位号请对照《Base M5GO Bottom2 原理图 PDF》;上表的 信号↔GPIO 映射本身确定。
 
---
 
## 8. 版本区别(选型时别搞混)
 
| 底座 | 适配主控 | RGB | IMU | IR | MIC |
|---|---|---|---|---|---|
| M5GO Bottom | Core Basic | 10×SK6812 | — | 有 | SPQ2410 |
| **M5GO Bottom2(本款)** | **Core2** | 10×SK6812 | **MPU6886** | — | SPM1423 |
| M5GO Bottom3 | CoreS3 | 10×WS2812 | — | 有 | — |
 
> Bottom2 是 **Core2 专用**;Bottom/Bottom3 的主控、灯珠型号、是否带 IR/IMU 都不同,**不要混用底座**。
 

