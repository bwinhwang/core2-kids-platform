# UNIT 8Encoder —— 硬件 Ground Truth

本文件是 **Unit 8Encoder** 这颗外设单元的板级硬件事实,可复制到任意挂载该单元的项目。**项目特定逻辑请勿写在这里**,放 `CLAUDE.md`。

来源:
- M5Stack 官方产品页 PDF(`8Encoder.pdf`,SKU **U153**)、[产品文档页](https://docs.m5stack.com/en/products/sku/U153)
- 官方 Arduino 库 [M5Unit-8Encoder](https://github.com/m5stack/M5Unit-8Encoder)(寄存器定义/100kHz/按键极性出处)
- **内部固件源码 [M5Unit-8Encoder-Internal-FW](https://github.com/m5stack/M5Unit-8Encoder-Internal-FW)(最硬 ground truth:寄存器处理/bootloader 行为,2026-07-03 逐条核实)**
- 板载芯片 datasheet:**STM32F030C8T6**、**WS2812C-2020**

---

## 1. 一句话定位

8 路旋转编码器一体化输入单元。从机 MCU = **STM32F030C8T6**,对上位机走 **I2C**。每路编码器:左右旋转(相对计数)+ 径向按下 + 1 颗 RGB。另有 1 路物理拨动开关 + 1 颗 RGB。内含 5V→3V3 DC-DC。

---

## 2. 接口与接线(HY2.0-4P / PORT.A)

| 颜色 | 信号 | 说明 |
|---|---|---|
| Black | GND | — |
| Red | 5V | **整板供电 5V**,不是 3V3 |
| Yellow | SDA | I2C 数据 |
| White | SCL | I2C 时钟 |

- I2C 从机地址:**0x41**(默认,可改,见寄存器 `0xFF`,范围 1~127,存 flash 掉电保持)。
  另有 **bootloader 常驻 0x54**(单元上电瞬间 I2C 两线被拉低时不进应用,见 §5.1)。
- 与其他常用 M5 I2C 单元地址不冲突(DLight 0x23 / Ultrasonic 0x57 / Gesture 0x73),可同口共存。
- 接 Core2 **PORT.A**(`I2C_NUM_0`;内部总线占 `I2C_NUM_1`=CONFIG_BSP_I2C_NUM):Yellow=**G32**(SDA)、White=**G33**(SCL)。**别抄 AtomS3 的 G1/G2**;PORT.A 独立总线,与内部 0x34/0x38/0x51/0x68 隔离。
- **驱动映射(本平台,已实现)**:组件 `components/units/unit_8encoder`;接入 `unit_8encoder_init(core2_board_port_a(), 0)`(0=默认地址 0x41)。
- 🔴 **供电坑:PORT.A 的 5V = M-Bus 5V(AXP192 EXTEN 使能的 SY7088 升压)**。电池供电时 EXTEN 没开单元就没电;插 USB 时 VBUS 直通会掩盖问题(症状:插电脑能玩、拔线失灵)。与底座灯带同一坑,见 `components/core2_power`。

> 物理:128 x 24 x 25 mm,43.9 g,2×LEGO 兼容孔。按压寿命 >50000 次。

---

## 3. I2C 寄存器映射(基址 0x41,V1 FW)

数值均为 **小端 int32**(byte0 为低字节),范围 `-2147483648 ~ 2147483647`,除非另注。

| 功能 | 基址 | 读写 | 单元数 / 布局 | 备注 |
|---|---|---|---|---|
| **Counter Value** 计数值 | `0x00`~`0x1F` | R/W | Cnt0..Cnt7,各 4 字节 | 写对应 `0x40` 可清零 |
| **Increment Value** 增量 | `0x20`~`0x3F` | R | Inc0..Inc7,各 4 字节 | **读后自动清零**(reset after get) |
| **Counter Reset** 计数清零 | `0x40`~`0x47` | W | Cnt0..Cnt7,各 1 字节 | 写 `1` 复位该路计数 |
| **Button Value** 按键 | `0x50`~`0x57` | R | BNT0..BNT7,各 1 字节 | 原始电平,**按下=0**(官方例程 `!getButtonStatus`) |
| **Button 按压计数** | `0x58`~`0x5F` | R | 各 1 字节 | 每路按压次数(内部固件源码核实,产品页未列) |
| **Switch** 拨动开关 | `0x60` | R | 1 字节 | 值 `0~1` |
| **变化标志 / 合并态** | `0x61` / `0x62` | R | 各 1 字节 | 源码核实,产品页未列;含义按源码,慎用 |
| **RGB** | `0x70`~`0x8A` | R/W | LED0..LED8,各 3 字节(R,G,B) | 每分量 `0~255`;**上电默认全灭**(灭≠没电) |
| **I2C Address** | `0xFF` | R/W | 1 字节 | 新地址 `1~127`,写 flash 掉电保持,改后立即生效(**不是 0xF0**,库+固件源码双重核实) |
| **Firmware Version** | `0xFE` | R | 1 字节 | 固件版本号 |

要点:
- **8 个编码器** → 计数/增量/按键各 8 路(索引 0~7)。
- **9 颗 RGB**:LED0~LED7 对应 8 个编码器,**LED8 对应拨动开关**。RGB 排布 `LED0` 起,每颗 3 字节 RGB(`0x70`=LED0-R,`0x71`=LED0-G,`0x72`=LED0-B,依次到 `0x88~0x8A`=LED8)。
- "增量"读后清零,适合做"上一帧旋转了多少格";"计数"是累计绝对值,需要时再手动 `0x40` 清。

---

## 4. 电源

- 输入:**5V**(经 Red 线灌入),内部 DC-DC 降到 3V3 给 STM32 + WS2812C 逻辑。
- I2C 逻辑电平:**3.3V**。
- RGB 为 WS2812C-2020,满亮度较刺眼,调试时降亮度。

---

## 5. 关键注意事项 / 易错点

### 5.1 🔴 bootloader 陷阱(0x54)与"总线被拽死"(内部固件源码核实,2026-07-03)

- 单元内有 **I2C bootloader,常驻地址 0x54**。上电瞬间 bootloader 检测 I2C 两线
  (STM32 PB10/PB11):**双低 → 留在引导态等固件更新(0x54),不进应用(0x41)**;
  双高才跳应用。社区"单元跑到 0x54"的悬案即此(M5 论坛 AtomS3+TailBat 案例)。
- 推论:**给单元上 5V 之前,主机必须保证 G32/33 为高**(上拉在位)。若主机先开 5V
  后建 I2C 总线(两线悬空),单元就可能困在引导态;再被主机的 clear-bus 脉冲串打搅,
  从机 I2C 可能卡死拽住 SDA+钳住 SCL → **整条总线双低拉死**(所有地址 probe timeout)。
- **自愈**:断电重启单元(本平台 `core2_board_port_a_recover()`:切 M-Bus 5V 400ms,
  恢复时 G32/33 已有上拉);`core2_board_init` 也已在开 5V 前预上拉 G32/33 防患未然。
- 注意 **EXTEN 掉电不清零**(AXP192 有电池供着):主机重启/拔 USB 都**不会**给单元
  断电,卡死会跨重启存活——必须真切 5V 或拔插 Grove 线。
- 上电时 9 颗 RGB **默认全灭**,"插上不亮灯"不能作为单元没电/坏了的证据。

### 5.1.1 🔴 读协议红线:必须"写寄存器号+STOP,再单独发起读"——repeated-start 组合读会钳死总线(2026-07-05 定谳)

- 内部固件**只在收到 STOP**(`HAL_I2C_ListenCpltCallback`)或收满 50 字节时才解析
  寄存器号并准备回读数据(`i2c2_set_send_data`);**repeated-start 组合读**(IDF 的
  `i2c_master_transmit_receive`、i2c_tools 的 `i2cget` 等)到达读方向 ADDR 时数据
  还没备好,从机拿着未初始化的 `tx_len=0` 进发送态 → TXDR 永远没数据 → **外设无限
  拉伸 SCL 钳死总线,断电才恢复**(固件的 505ms 自恢复只补 EnableListen+NACK,
  解不开发送态拉伸)。
- **症状签名**:`i2cdetect` 能看到 0x41(地址 ACK 是外设硬件行为,不代表固件读法
  兼容),**第一笔寄存器读即 timeout,随后全总线 probe 超时直到断电**。经典 ESP32
  与 S3 同症。⚠️ 这个签名极易被误判为"单元 DOA/固件坏"——**先改读法,再怀疑硬件**;
  这类 MCU 固件从机的通用读写规则见 `_MCU_Firmware_I2C_Units.md`。
- 正确姿势 = 官方 Arduino 库/UIFlow 库的做法:`write(reg)` + STOP,再 `read(n)`,
  **两笔独立事务**。本仓库驱动 `unit_8encoder.c reg_read()` 已按此实现(源码有红线
  注释),**勿改回 transmit_receive**。此红线适用于所有"从机是 MCU 固件"的 Unit;
  AXP192/MPU6886 这类硬件寄存器机不受影响。
- 原理图(`SCH_UNIT_8Encoder_V1.01`)硬事实:SDA/SCL 板载 **4.7k 上拉到单元 3V3**
  (R3/R4)→ 主机无须外加上拉;5V→3.3V 是 **BL8075 LDO**;板上有 **SWD 座
  (J2:SWCLK/SWDIO/NRST/3V3/GND)**,真需要时可重刷内部固件(§1 开源,§5.1.2 IAP)。

### 5.1.2 bootloader IAP 刷写协议(内部固件源码逐行核实,2026-07-05)——无需 ST-Link 的复活路径

源码:`M5Unit-8Encoder-Internal-FW/bootloader/basex_bootloader/Core/Src/main.c`(本地已下载,仓库根同名目录)。

- **进入引导态**:单元上电后 bootloader 延时 300ms **单次采样** PB10/PB11(=SCL/SDA):
  **双低 → 留在 IAP(0x54)**;否则校验 `0x08001000` 首字(应用初始 SP)后跳应用。
  主机可控地强制进入:G32/33 驱为低 → EXTEN 断电重启 5V → 保持低 ≥400ms → 释放。
  (官方 bug:`iap_gpio_init` 把上拉配到了 **GPIOA** 却检测 **GPIOB**,故判定完全依赖
  板载 R3/R4 外部上拉——单元 3V3 正常时无碍。)
- **刷写协议(I2C @0x54,全部为普通写事务)**:
  - `0x06`(WREN)+ 4 字节 flash 地址(**大端**)+ 2 字节长度 + 1 字节填充 + **1024 字节页数据**
    = 单笔 1032 字节写事务;bootloader 在 STOP 后擦该页并烧写整 1KB。
  - `0x77`(USRCD)= 跳转应用。
  - **没有读回/校验命令**(OPC_READ 被注释掉),验证方式只有"跳应用后 0x41 能否服务寄存器读"。
- ⚠️ **Write_Code 无地址保护**:传 `0x08000000~0x08000FFF` 会把 bootloader 自己擦掉,变真砖。
  **只允许写 `0x08001000` 起的应用区**(共 60 页 ×1KB 到 0x0800FFFF)。
  每页之间主机要等擦写完成(期间 IAP 关中断,从机会拉伸/不应答,建议页间延时 ≥100ms 或失败重试)。
- **官方成品固件**:`firmware/M5Unit-8Encoder-Internal-FW-V2.hex` = **完整 64KB 镜像**
  (0x08000000 起,含 bootloader+应用);IAP 复活只取其 0x1000 偏移起的应用部分逐页下发。
- 应用固件用 `IAP_Set()` 把向量表复制到 SRAM 并重映射(应用链接在 0x08001000,正常现象)。

### 5.2 其它易错点

- 🔴 **必须插 Core2 机身侧面的红色 PORT.A 口**。叠 M5GO Bottom2 后系统共 3 个 Grove 口,
  **底座上的黑口(PORT.B,G26/G36 模拟)和蓝口(PORT.C,G13/G14 串口)不是 I2C**,
  插上去的症状 = I2C NACK(IDF 报 `ESP_ERR_INVALID_RESPONSE`,"0x41 无应答"),
  单元灯不亮/不响应。附:超时后驱动做总线恢复会重配引脚,刷出
  `GPIO 32/33 is not usable, maybe conflict with others` 告警——那是对自己已占引脚的
  重复告警(经典 ESP32 无硬件 FSM 复位),**不是真引脚冲突**,别被带偏。
- **Increment 寄存器读一次就清零**,不要在一帧里重复读同一路还指望累加。
- 计数为有符号 32 位,会回绕到负数;按 int32 解析,不要按无符号。
- 改 I2C 地址(`0xFF`)后,后续访问要用新地址;固件写 flash **掉电保持**,改完先验证。
- RGB 是该单元自带的灯(STM32 驱动),**不是**主控直接驱动的 WS2812;主控只通过 I2C 写 `0x70~0x8A`。

---

## 6. 没有的东西 / 常见误解

- 编码器输出是**相对量**(增量/计数),**没有绝对角度**。
- **不是** GPIO/UART 单元,**只有 I2C**;不要去找 TX/RX。
- 拨动开关只有 1 路(`0x60`),不要和 8 路按键混淆。
