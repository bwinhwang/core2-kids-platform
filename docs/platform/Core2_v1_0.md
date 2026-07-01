# HARDWARE.md — M5Stack Core2 (初代 / v1.0) 板级 Ground Truth
 
本文件是板子级硬件事实,可复制到任意基于 **Core2 初代(v1.0,SKU K010)** 的项目。**项目特定逻辑请勿写在这里**,放 `CLAUDE.md`。
 
来源:
- M5Stack 官方 Core2 PinMap / 数据手册(SKU **K010**)
- `Core2-核心部分 / 拓展板部分 原理图 PDF`
- M5Stack 官方 I2C 地址索引表、Core2 产品页
- 板载芯片 datasheet:**ESP32-D0WDQ6-V3**、**AXP192**、**ILI9342C**、**FT6336U**、**NS4168**、**SPM1423**、**MPU6886**、**BM8563**、**SY7088**、**CP2104**
> ⚠️ **本文部分引脚级事实由 Core2 共享板级设计推导并与 v1.3 PinMap 交叉核对**(两版 PCB/M-Bus 布局一致,仅 IMU/USB 桥/电池不同)。落到具体单板时,以手上单元的原理图为准。
 
---
 
## 本板与 v1.3 的差异速览(先看这个)
 
| 项 | **Core2 初代(本板)** | Core2 v1.3 |
|---|---|---|
| **IMU** | **MPU6886**(I2C `0x68`) | BMI270(I2C `0x68`) |
| **USB-TTL 桥** | **CP2104**(后期批次也有 CH9102F) | CH9102 |
| **USB 驱动** | **CP210X(CP2104 版)** / 或 CH9102 VCP(CH9102F 版) | CH9102 VCP |
| **内置电池** | **390 mAh** 3.7V | 500 mAh 3.7V |
| **RTC 备份电池(MS412FE)** | **无**(v1.1 起才加入) | 有 |
| PMIC | **AXP192** | AXP192(相同) |
| 电源指示灯 | **绿色** | 绿色(相同) |
| SoC / Flash / PSRAM | ESP32-D0WDQ6-V3 / 16MB / 8MB | 相同 |
| LCD / 触摸 / 功放 / mic / RTC | ILI9342C / FT6336U / NS4168 / SPM1423 / BM8563 | 全相同 |
 
> 关键工程影响:
> 1. **IMU 驱动不通用**——MPU6886 与 BMI270 寄存器图、WHO_AM_I、量程配置完全不同,针对 v1.3 写死 BMI270 的代码不能直接套到本板;两者虽同在 `0x68`,但靠 WHO_AM_I 区分。
> 2. **USB 驱动要先认芯片**——本板可能是 CP2104 **或** CH9102F,装驱动前先确认丝印/枚举的 VID:PID,别默认 CH9102。
> 3. **没有 RTC 备份电池**——掉电后 BM8563 不保时,依赖 RTC 走时的项目要注意。
 
---
 
## 0. 一句话定位 + 与 AtomS3 的根本区别
 
2.0" 电容触摸屏主控,**经典 ESP32(LX6 双核,非 ESP32-S3)**。**没有原生 USB**——靠 **CP2104**(后期批次部分换 **CH9102F**)USB-TTL 桥接,烧录需装对应 VCP 驱动(CP2104→CP210X 驱动)。**电源与多数外设的供电/复位由 AXP192 PMU 接管**,不是直接 GPIO 控制(这是和 AtomS3 Lite 最不一样的地方)。板载 6 轴 IMU 为 **MPU6886**。
 
---
 
## 1. SoC & 内存
 
- MCU:**ESP32-D0WDQ6-V3**(Xtensa 双核 LX6,最高 240 MHz,2.4GHz Wi-Fi + 3D 天线)
- Flash:**16 MB**
- PSRAM:**8 MB**
- IDF target:`esp32`(**不是** `esp32s3`)
> ⚠️ **PSRAM 8MB 但经典 ESP32 地址空间只能直映射 4MB**。超过 4MB 的部分需走 himem(bank-switch)API,不能当连续堆用。设计大缓冲时按"可直接 malloc 的 ≈4MB"来算。
 
 
ESP-IDF sdkconfig 片段(16MB flash + 8MB PSRAM):
```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
# 8MB PSRAM：>4MB 部分需 himem，确认 CONFIG_SPIRAM_BANKSWITCH_ENABLE 行为
```
 
> SoC/内存与 v1.3 完全一致,此节无差异。
 
---
 
## 2. 电源架构(AXP192 —— 必读)
 
- 输入:**USB-C 5V @ 500mA**,或经底部 BAT 接口的 **3.7V/390mAh** 锂电池。
- **PMU:AXP192(内部 I2C 地址 `0x34`)**。ESP32 主控、屏、外设的各路电源/复位都挂在 AXP192 上。
- 5V 升压:**SY7088**(给 M5-Bus 的 5V 输出)。
- **RTC 备份电池:初代无**(MS412FE 可充电微型锂电是 v1.1 起才加的)。**掉电后 BM8563 不保时**。
- 充电参数:充电电流 0.219A;充满(关机)0.055A;充满(开机)0.147A。
AXP192 通道分配(**这些不是 ESP32 GPIO,只能通过 I2C 写 AXP192 控制**):
 
| AXP192 通道 | 控制对象 |
|---|---|
| LDO2 | LCD 逻辑供电(`ILI9342C PWR`) |
| DC3 | LCD 背光(`ILI9342C BL`) |
| LDO3 | **震动马达** VCC |
| IO1 | **绿色电源指示灯** VCC |
| IO2 | **扬声器使能** `NS4168 SPK_EN` |
| IO4 | **LCD RST + 触摸 FT6336U RST**(两者共用) |
 
> ⚠️ **上电后不初始化 AXP192,屏会黑、触摸/喇叭/马达都不工作**。必须先经 I2C 配好上述 LDO/DC/IO 再用外设。用 M5Unified/M5GFX 时它替你做了;裸 IDF 要自己写 AXP192 初始化。
>
> 与 v1.3 的差异:**电池容量 390mAh(v1.3 为 500mAh)**;**初代无 RTC 备份电池**。AXP192 通道分配与 v1.3 相同。
 
---
 
## 3. 引脚映射(板载外设,经典 ESP32 GPIO)
 
### LCD(ILI9342C,SPI)+ microSD(共享同一 SPI 总线)
 
| 信号 | GPIO | 备注 |
|---|---|---|
| SPI MOSI | 23 | LCD 与 SD 共用 |
| SPI MISO | 38 | 共用(**输入只读脚**) |
| SPI SCK | 18 | 共用 |
| LCD CS | 5 | **strapping** |
| LCD DC | 15 | **strapping** |
| SD CS | 4 | SD 独立片选 |
| LCD RST / BL / PWR | — | 走 AXP192 IO4 / DC3 / LDO2 |
 
> 🔧 **裸 IDF v6 屏驱动实测(2026-06)**:用 `espressif/esp_lcd_ili9341`(**ILI9342C 兼容**),
> SPI2 / pclk 40MHz / spi_mode 0 / cmd+param 8bit;`reset_gpio_num=-1`(RST 走 AXP192 IO4,由 AXP 复位脉冲负责);
> **`esp_lcd_panel_init` 后必须 `esp_lcd_panel_invert_color(true)`**(否则负片色);横屏 320×240 用 `swap_xy(true)`;
> 颜色 R/B 反则按 BGR 处理。背光=DCDC3、逻辑电=LDO2,都要先经 AXP192 开。来源:esp-bsp `bsp/m5stack_core_2/`。
 
### 触摸(FT6336U,I2C 0x38,挂内部 I2C)
 
| 信号 | GPIO |
|---|---|
| SDA / SCL | 21 / 22(内部 I2C) |
| INT | 39(**输入只读脚**) |
| RST | AXP192 IO4 |
 
- 屏正面三处圆点是触摸热区,软件映射成 3 颗虚拟按键。
### 音频:扬声器(NS4168,I2S)+ 麦克风(SPM1423,PDM)
 
| 信号 | GPIO | 备注 |
|---|---|---|
| NS4168 BCLK | 12 | **strapping(MTDI)** |
| NS4168 LRCK | 0 | **strapping**;与 mic CLK 共用 |
| NS4168 DATA(SD) | 2 | **strapping** |
| NS4168 SPK_EN | AXP192 IO2 | — |
| SPM1423 CLK | 0 | 与 LRCK 同脚 |
| SPM1423 DATA | 34 | **输入只读脚** |
 
> 🔧 **裸 IDF v6 实测补充(2026-06,踩坑记录)**:
> - **NS4168 监听右声道**:I2S 用 `I2S_SLOT_MODE_MONO`(默认左槽)→ **完全没声**。正解 =
>   **Philips / 16bit / 立体声,左右声道写同一份 PCM**(或 mono 放右槽);喂流缓冲做成交织 `int16[N*2]`。
> - **SPK_EN 极性**:GPIO2 配 OD 输出(AXP192 reg 0x93 低 3 位=000),**reg 0x94 bit2 = 1 使能**功放
>   (M5 库与 esp-bsp 一致)。"使能位写对了却没声"基本都是上面的声道问题,不是极性。
> - **判障**:功放已使能但 I2S 格式不对时,喇叭只有**底噪/电流声、无干净音调** → 据此判断"功放通了、数据格式错"。
> - 调试手段:开机喂固定测试音 + 依次切几种槽格式各放一声,一次烧录即可听出对的格式。
 
### LED / 马达 / RTC / IMU
 
| 外设 | 连接 |
|---|---|
| 绿色电源 LED | AXP192 IO1 |
| 震动马达 | AXP192 LDO3 |
| RTC BM8563(0x51) | 内部 I2C 21/22 |
| **IMU MPU6886(0x68)** | 内部 I2C 21/22(在背部拓展小板) |
 
> 与 v1.3 的差异:**IMU 芯片为 MPU6886(v1.3 是 BMI270)**,挂载位置/总线相同(内部 I2C 21/22,地址 `0x68`)。其余引脚全相同。
 
---
 
## 4. 两条 I2C 总线(别搞混)
 
| 总线 | SDA | SCL | 挂载设备 |
|---|---|---|---|
| **内部 I2C** | G21 | G22 | AXP192 `0x34`、FT6336U `0x38`、BM8563 `0x51`、**MPU6886 `0x68`** |
| **外部 PORT.A** | G32 | G33 | 你外接的 Grove I2C 单元 |
 
HY2.0-4P(PORT.A)排线:
 
| 颜色 | 信号 | GPIO |
|---|---|---|
| Black | GND | — |
| Red | 5V | — |
| Yellow | SDA | 32 |
| White | SCL | 33 |
 
> 外接 M5 I2C 单元(DLight/Gesture/Ultrasonic/8Encoder 等)走 **PORT.A(G32/G33)**,**不要**接到内部 I2C(G21/G22),否则可能和 AXP192/IMU/触摸/RTC 抢地址或干扰。
>
> 与 v1.3 的差异:内部 I2C 上的 IMU 是 **MPU6886** 而非 BMI270(地址同为 `0x68`)。总线划分一致。
 
---
 
## 5. M5-Bus(30 针)对外引出
 
经主控 GPIO 引到 30 针 M5-Bus。按功能(GPIO)整理如下:
 
| 类别 | 引脚 |
|---|---|
| ADC(输入只读) | G35, G36 |
| DAC(真模拟输出) | G25, G26 |
| UART0(也是下载/串口监视) | TX0=G1, RX0=G3 |
| UART2 | TX2=G14, RX2=G13 |
| 内部 I2C | SDA=G21, SCL=G22 |
| PORT.A 外部 I2C | SDA=G32, SCL=G33 |
| SPI | MOSI=G23, MISO=G38, SCK=G18 |
| I2S | DOUT=G2, LRCK=G0, DATA=G34 |
| 相对空闲 GPIO | G27, G19 |
| 电源/其他 | 3V3, 5V, BAT, GND, RST, EN |
 
> ⚠️ **M5-Bus 上很多脚是和板载外设复用的**:G18/G23/G38 = LCD+SD 的 SPI;G0/G2/G34/G12 = 音频 I2S/PDM;G21/G22 = 内部 I2C;G5/G15 = LCD CS/DC。**外部要用这些脚前,先确认对应板载功能是否在用**,否则会冲突。真正"较自由"的只有 G27、G19,以及 DAC 的 G25/G26、ADC 的 G35/G36(只读)。
>
> 注:M5-Bus 顶部 GND/RST/EN 几行原理图文本排版有歧义,精确的针位号请对照《Core2-核心部分 原理图 PDF》。上表的 GPIO↔功能映射本身是确定的。
>
> M5-Bus 与 v1.3 完全一致,此节无差异。
 
---
 
## 6. Strapping pin 警示(经典 ESP32)
 
ESP32 strapping pin:**GPIO0 / GPIO2 / GPIO5 / GPIO12(MTDI) / GPIO15(MTDO)**。
 
- 这些脚在 Core2 上**已被板载外设占用**:G0/G2=音频,G5=LCD CS,G15=LCD DC,G12=NS4168 BCLK。
- 硬件已按可正常启动设计,**正常使用别动**;尤其 **G12(MTDI)**关系到 flash 电压选择,外部乱拉可能导致启动异常。
- 想把任意 strapping 脚拿去做别的用途,先查原理图确认复位电平。
### 输入只读脚(ESP32 特性)
 
**GPIO 34/35/36/37/38/39 只能做输入**,无输出、无内部上下拉。板上 MISO=G38、触摸 INT=G39、mic DATA=G34、ADC=G35/G36 都落在这里。**别把它们配成输出或当作需要上拉的按键脚。**
 
> Strapping / 只读脚与 v1.3 一致,此节无差异。
 
---
 
## 7. IMU 方向(MPU6886)
 
板载 6 轴 IMU 为 **MPU6886**(I2C `0x68`),非 v1.3 的 BMI270。
 
- **驱动差异**:MPU6886 与 BMI270 寄存器图不同(WHO_AM_I、量程/带宽配置、数据寄存器布局都不一样),**v1.3 的 BMI270 驱动不能直接套用**。M5Unified/M5GFX 会按板型自动选驱动并做坐标归一化;裸 IDF 自己写驱动时按 MPU6886 datasheet 来。
- **量程**:加速度 ±2/±4/±8/±16 g,陀螺 ±250/±500/±1000/±2000 dps(寄存器配置)。
- **方向**:MPU6886 芯片自身坐标系与 BMI270 的安装朝向不一定一致;换算姿态请以本板 MPU6886 的实测/官方示意为准,**不要照搬针对 BMI270 写死的轴向映射**。用 M5Unified 的 `getAccel/getGyro` 已做板级归一化,跨版本一般可移植;手搓寄存器读数则需各自处理。
---
 
## 8. 板载外设地址速查
 
| 设备 | 总线/接口 | 地址 |
|---|---|---|
| AXP192 PMU | 内部 I2C | 0x34 |
| FT6336U 触摸 | 内部 I2C | 0x38 |
| BM8563 RTC | 内部 I2C | 0x51 |
| **MPU6886 IMU** | 内部 I2C | **0x68** |
| ILI9342C LCD | SPI | — |
| NS4168 功放 | I2S | — |
| SPM1423 mic | PDM | — |
| microSD | SPI | — |
 
> 与 v1.3 的差异:`0x68` 上是 **MPU6886**(v1.3 是 BMI270)。两者地址相同,代码靠 WHO_AM_I 寄存器区分芯片型号。
 
---
 
## 9. 烧录 / USB 驱动
 
- USB-TTL = **CP2104**(部分后期批次为 **CH9102F**,二者功能等价但驱动不同)。**装驱动前先确认本机芯片**:
  - **CP2104** → 装 **CP210X(CP210x VCP)** 驱动(Silicon Labs)。
  - **CH9102F** → 装 **CH9102 VCP(CH34X / CP34X)** 驱动(沁恒)。
  - 不确定就两个都装;macOS 上 CH9102 安装偶报错多为误报,通常已装好可忽略。
- 上传速率官方用 921600;失败/超时(如 `Failed to write to target RAM`)先换线/换口/重装驱动。
- 开机:单击左侧电源键;关机:长按左侧电源键 6 秒;复位:单击底侧 RST。
- 经典 ESP32 没有 USB-Serial-JTAG,**不能像 AtomS3 那样靠长按进下载模式**;由 USB-TTL 桥 + DTR/RTS 自动复位进 boot。
> 与 v1.3 的差异:**v1.3 固定是 CH9102**;本板可能是 **CP2104 或 CH9102F**,驱动选择需先认芯片。
 
---
 
## 10. 没有的东西 / 易错点(避免误判)
 
- **不是 ESP32-S3**:无原生 USB、无 USB-OTG;外设/库选型按经典 ESP32。
- **IMU 是 MPU6886 不是 BMI270**:针对 v1.3 写的 BMI270 寄存器代码不能直接用;靠 WHO_AM_I 识别。
- **USB 桥可能是 CP2104 或 CH9102F**:别默认 CH9102 驱动。
- **初代无 RTC 备份电池**:掉电不保时,依赖 BM8563 走时的项目要注意。
- **外设供电要先配 AXP192**,否则屏黑、无声、无触摸——别误以为代码或接线坏了。
- **8MB PSRAM 不等于 8MB 可直用**(经典 ESP32 直映射上限 4MB,见 §1)。
- **两条独立 I2C 总线**(§4),外接单元接 PORT.A(G32/33)而非内部 G21/22。
- **M5-Bus 多数脚复用板载功能**(§5),不是自由 GPIO。
- 结构:**勿与 Base 系列底座堆叠**(自带震动马达会机械干涉);要堆模块需拆电池底,想保留 mic/IMU/电池用 **M5GO Bottom2**。
- 屏幕边缘触摸非线性可用 **M5Tool** 升级屏固件解决。
---
 
## 11. 版本差异(代码自动识别时有用)
 
| 项 | **Core2 初代 v1.0(本板)** | Core2 v1.1 | Core2 v1.3 |
|---|---|---|---|
| SKU | **K010** | K010-V11 | K010-V13 |
| IMU | **MPU6886** | MPU6886 | BMI270 |
| PMIC | **AXP192** | AXP2101 + INA3221 | AXP192 |
| USB-TTL | **CP2104 / CH9102F** | CH9102 | CH9102 |
| 电源指示灯 | **绿色** | 蓝色 | 绿色 |
| RTC 备份电池 | **无** | 有 | 有 |
| 内置电池 | **390 mAh** | — | 500 mAh |
 
> v1.0 的识别特征组合:**MPU6886 + AXP192 + 绿色 LED + 无 RTC 备份电池**。
>
> - 注意 **v1.1 换的是 AXP2101(+INA3221)**,寄存器/通道与 AXP192 不同,电源驱动不能直接套用;v1.1 指示灯改蓝色。
> - **v1.3 换的是 BMI270 IMU**,但 PMIC 又退回 AXP192、指示灯仍绿色——所以单看 LED 颜色无法区分 v1.0 与 v1.3,**要靠 IMU 的 WHO_AM_I(MPU6886 vs BMI270)+ USB 桥** 来判定。
> - 三版的 IMU 都在 I2C `0x68`,PMIC 都在 `0x34`,靠芯片 ID 寄存器区分。
 

