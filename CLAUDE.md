# CLAUDE.md — 游戏 + IoT 评估平台(Core2 卡带机)

> **本仓库不是单个 app,是一台「游戏 + IoT 硬件评估台」卡带机平台**:硬件底座 = M5Stack Core2 +
> M5GO Bottom2;软件 = factory 分区常驻 **launcher 选择页** + 6 个 ota 槽,**ota_0~3 放四张
> 幼儿游戏卡带、ota_4~5 放两张 IoT 评估卡带**(单一职责:游戏卡带负责好玩,评估卡带负责
> 外设/单元评估、功耗/系统评估……)。改一个 app 只重编+单刷它自己,launcher 与其它 app 零风险。
>
> **2026-07-17 平台转向**(用户拍板,当天两次决策):早上一度整体转向 IoT 评估台——原「幼儿
> 游戏掌机」定位下线、6 张幼儿游戏卡带全部删除;**当晚用户修订为「游戏与 IoT 评估台共存」**:
> 四张已实现游戏(tilt_maze/busy_knobs/chick_pour/chain_lab)已从删除前的提交恢复重新上线,
> fish_pond/pipe_garden 当时仅有 SPEC 未实现、不恢复(git 历史留档,`git log` 可查完整过程)。
> 硬件平台不变;新增的两张评估卡带按"可观测评估"重心继续开发。经过与决策见 `docs/ROADMAP.md`。
>
> 本文件是**平台级说明书**(共守的设计原则、硬件事实、组件层、渲染/电源纪律、做新评估 app
> 的规矩、各 app 索引)。配合通用规范 `AGENTS.md` 和板级事实 `docs/platform/Core2_v1_0.md`
> (主控)、`docs/platform/M5GO_Bottom2.md`(**必装底座**)。**板级硬件事实以那两份
> HARDWARE.md 为准,本文不重复引脚表、只引用。**
>
> ⚠️ **硬件平台是「Core2 + M5GO Bottom2」组合体,不是 Core2 单体。** 本机 Core2 核缺背部扩展模块,
> **IMU 与电池均由 Bottom2 提供**;不接 Bottom2 就没 IMU、没电——绝大多数 app(游戏与评估台皆然)跑不起来。底座是硬依赖。
>
> **各卡带的东西不在本文**:某个 app 的**功能规格**见 `apps/<name>/SPEC.md`,**竣工现状/
> 定案数值/待实机项**见 `apps/<name>/README.md`。做新 app 前先读本文 §2(原则)、§4–§10
> (§2 六条是**评估 app** 的设计纪律;四张游戏是转向前的 as-built 遗产,原样保留、不回填新原则)。
>
> **暂不联网**:本阶段不引入 Wi-Fi/BLE/ESP-NOW/MQTT,先做本地评估台;网络阶段规划见
> `docs/ROADMAP.md` Phase 3(占位)。
>
> 目标读者:负责实现的 AI 协作者(Claude Code)。**涉及 ESP-IDF / 组件 API 的地方先查 MCP 再写**(`AGENTS.md` §1,清单见 §11)。

---

## 1. 平台定位

一台**游戏卡带 + IoT 评估卡带共存**的桌面卡带机(硬件形态与幼儿掌机时期完全相同):

- **物理形态**:Core2 主机 + Bottom2 底座(IMU / 500mAh 电池 / 10×SK6812 灯带 / PORT.A·B·C 扩展口)。
- **一机多卡带**:开机进 launcher 选择页(数据驱动渲染各槽工程名/版本)→ 点卡片进某 app →
  游戏卡带正常游玩,评估卡带数据实时上屏 + 可经串口导出 CSV;崩溃/断电自动回 launcher
  (app_slot 机制不变)。
- **每张评估卡带的共同体验**:接上被评估对象(外接单元 / 观察机身自身功耗)→ **状态实时可见**
  (数值卡/图表/状态栏)→ **数据可核对可导出**(串口 CSV,离线场景走 SPIFFS 录制)。四张游戏
  卡带是转向前的 as-built 遗产,体验不受本文档 §2 评估台原则约束。
- **多样的评估对象靠外接单元 + 板载遥测**:IMU(内置)、PORT.A 的 Grove I2C 单元(8Encoder /
  超声波 / DLight / 手势 …)、PORT.C 的 Chain UART 菊花链(编码器 / 摇杆)、AXP192 自身的电压/
  电流遥测(电池/VBUS)。单元接入套路见 §10。

### 各 App 索引

| App | 槽 | 外设 | 状态 | 文档 |
|---|---|---|---|---|
| **tilt_maze** 倾斜迷宫(游戏) | ota_0 | IMU 倾斜 | ✅ 已恢复,as-built | `apps/tilt_maze/SPEC.md` + `README.md` |
| **busy_knobs** 忙碌旋钮(游戏) | ota_1 | Chain Encoder | ✅ 已恢复,as-built | `apps/busy_knobs/FUN2_SPEC.md` + `README.md` |
| **chick_pour** 喂小鸡(游戏) | ota_2 | IMU 倾斜 | ✅ 已恢复,as-built | `apps/chick_pour/SPEC.md` + `README.md` |
| **chain_lab** 吊臂抓取(游戏) | ota_3 | Chain Joystick | ✅ 已恢复,as-built | `apps/chain_lab/SPEC.md` + `README.md` |
| **unit_bench** 外设/单元评估台 | ota_4 | PORT.A I2C(8Encoder/DLight/超声波/手势/CO2L-SCD41)+ Chain(Encoder/Joystick) | 🔨 已实现·build 通过·待实机 | `apps/unit_bench/SPEC.md` + `README.md` |
| **power_lab** 功耗/系统评估台 | ota_5 | AXP192 遥测(`power_monitor`)+ 全平台负载开关 | 🔨 已实现·build 通过·待实机 | `apps/power_lab/SPEC.md` + `README.md` |
| **launcher** 卡带机选择页 | factory | — | 🔄 数据驱动重写(自动发现槽位,工程名/版本/编译日期直读 `esp_app_desc_t`,无需为新 app 改 launcher 代码) | `launcher/README.md` |

> **历史**:2026-07-17 早间平台一度整体转向 IoT 评估台,6 张幼儿游戏卡带(tilt_maze/
> busy_knobs/chick_pour/chain_lab/fish_pond/pipe_garden)被整体删除;**当晚用户修订为共存**,
> 四张已实现游戏从删除前的提交恢复重新上线并重排到 ota_0~3,fish_pond/pipe_garden(当时仅有
> SPEC 未实现)不恢复。完整过程与代码在 git 历史中留存(`git log -- apps/`)。做新评估 app 从
> §10 起步:`tools/new_app.sh <名>` 脚手架;分区偏移/单刷命令见 `tools/flash_map.md`;组件
> 复用指南见 `docs/platform/BSP_GUIDE.md`。

---

## 2. 设计第一性原则(所有评估 app 共守)

每张评估卡带都要守这六条(取代幼儿掌机时期的"零失败/多通道冗余"叙事,硬件约束类原则原样继承):

1. **可观测优先**:被评估对象的状态屏上实时可见、串口可拿原始数据。评估台存在的意义就是
   "让人看清楚硬件在干什么",任何数值都不该只存在于代码变量里。
2. **错误显式呈现**:init 失败/总线卡死/拔线一律屏上红字+错误码,绝不静默(与幼儿掌机时期
   "零失败温柔弹开"**反向**——评估场景下"出错"本身就是有价值的信息,必须让人看见)。
3. **数据可导出**:数值带时间戳导出(串口 CSV 为主,`data_log` 组件;离线场景 SPIFFS 录制
   作为拔 USB 之后的补充手段,见 §10)。
4. **热插拔容错**(继承自幼儿掌机时期的既有形态):2s 重试 + 连续失败判拔线;单元 init 可重
   复调用,没插 = 无字提示卡 + 周期重试,插上即接管。
5. **渲染红线**(继承,硬件约束与受众无关):经典 ESP32 无 2D 加速、屏走 SPI,**永不每帧整屏
   重绘**;数值卡/chart 走脏矩形(见 §6)。
6. **省电纪律**(继承但可被评估 app 接管):默认 `core2_sleep` 两级省电;评估 app 可禁 DEEP、
   接管 5V 管理、或用 `core2_sleep_force_stage` 手动驱动休眠阶段做演练(power_lab 用途)。

> 不再有"幼儿零失败/无文字"的约束:受众识字,UI 可以直接用文字承载信息(字号规范见 §8)。
> 这是本次转向最大的架构红利之一——launcher 不再需要为每个新 app 手绘图标分支。

---

## 3. 硬件平台(Core2 + M5GO Bottom2)

### 3.1 主控:M5Stack Core2 初代(v1.0,SKU K010)

详见 `docs/platform/Core2_v1_0.md`。平台强相关的板级事实(**全部已核实,勿猜**):

| 用途 | 事实(出处:`docs/platform/Core2_v1_0.md`) |
|---|---|
| SoC | ESP32-D0WDQ6-V3(**经典 ESP32 / LX6,不是 S3**),IDF target = `esp32` |
| 内存 | 16MB flash + 8MB PSRAM,**但经典 ESP32 直映射上限 ≈4MB**,大缓冲按 4MB 算 |
| 屏 | ILI9342C,SPI,**320×240**,MOSI=23/MISO=38/SCK=18/CS=5/DC=15;RST/BL/PWR 走 AXP192 |
| 触摸 | FT6336U,内部 I2C(0x38),INT=39 |
| **IMU** | **MPU6886**(内部 I2C `0x68`)。**本机由 Bottom2 提供**(Core2 背板缺失);仍是 MPU6886、仍在 0x68,**不是** BMI270,**勿照搬 BMI270 代码** |
| 内部 I2C | G21(SDA)/G22(SCL):AXP192 0x34、FT6336U 0x38、BM8563 0x51、**MPU6886 0x68** |
| 音频 | NS4168(I2S class-D 功放);SPK_EN 在 **AXP192 IO2**;BCLK=12/LRCK=0/DATA=2 |
| **震动马达** | **AXP192 LDO3** —— 一条触觉反馈通道(见 §5) |
| 电源 | **AXP192(0x34)接管屏/触摸/喇叭/马达供电**;**不初始化 AXP192 → 屏黑、无声、无触摸** |
| 电池 | **由 Bottom2 提供 500mAh**(Core2 自身 390mAh 背板缺失);容量有限,**必须做 idle 省电**(见 §7) |

> ⚠️ **强制读一遍 `docs/platform/Core2_v1_0.md` 第 2 节(AXP192)和第 6 节(strapping)。** 音频脚 G0/G2/G12 都是
> strapping、已被板载占用,正常别动。

### 3.2 必装底座:M5GO Bottom2

详见 `docs/platform/M5GO_Bottom2.md`。**平台硬依赖底座**——本机 Core2 缺背部扩展模块,IMU 和电池都靠它:

- **IMU(MPU6886 @ 0x68,内部 I2C G21/G22)**:倾斜类评估的读数来源。**驱动与 Core2 自带 MPU6886 完全一致**(同芯片同地址同总线),代码无须为底座特化。
- **500mAh 电池 + TP4057 充电**:本机供电来源,也是 power_lab 评估的对象之一。
- **10 颗 SK6812 RGB 灯条(数据线 G25)**:反馈通道之一(见 §5),评估 app 可用作状态提示或作为
  power_lab 的负载源。
- **PORT.A(红,Core2 机身侧面,I2C G32/G33)/ PORT.B(黑,G26 DAC/G36 ADC)/ PORT.C(蓝,UART2 G13/G14)**:外接单元扩展口,是 unit_bench 的评估对象来源。PORT.A 接 Grove I2C 单元、PORT.C 接 Chain 菊花链(见 §10)。

> ⚠️ **叠底座后 G25 被灯条占用,不能再当 DAC。** 本机背板缺失,生效的 IMU 就是底座这颗(0x68 无歧义)。
> 🔴 **PORT.A/C 的 5V 由 M-Bus 5V 供**,使能在 AXP192 EXTEN——BSP 从不开,详见 §7 / §10。

### 3.3 组件依赖(已查 ESP 组件库)

| 依赖 | 用途 | 备注 |
|---|---|---|
| `espressif/m5stack_core_2` (BSP, v3.0.1+) | AXP192 电源 / LCD / 触摸 / **LVGL** / NS4168 喇叭一站式 | target esp32;V1.0 配 `CONFIG_BSP_PMU_AXP192`;**`BSP_CAPS_IMU=0` → IMU 需自写** |
| `espressif/esp_codec_dev` | 喇叭播放(BSP 间接依赖) | NS4168 经 I2S,SPK_EN 由 BSP/AXP 管 |
| LVGL(随 `esp_lvgl_port`) | 全部 UI 渲染 | BSP 带 `bsp_display_start*`;评估台加 Montserrat 16/24 数值字体(见 §11) |
| `espressif/led_strip` | 底座灯条 10×SK6812 @ G25,RMT 后端 | 反馈通道 / power_lab 负载源 |
| (备选)`jbrilha/esp_lcd_ili9342` | BSP 的 ili9341 兼容初始化若偏色/偏移再换 | 默认先用 BSP 自带 |

> **关键结论**:BSP 把最容易踩的 AXP192 初始化、LCD/触摸/LVGL/喇叭都包好了;平台自己要管的是 IMU 驱动、
> **EXTEN 供 5V / DCDC3 背光这些 BSP 不碰的 AXP192 电源位**(见 §7)、AXP192 ADC/库仑计遥测(见 §11)、
> 以及各反馈/单元/评估 UI 组件。

---

## 4. 平台组件层(复用 = 平台价值)

外设层沉淀为**可复用组件**,做新 APP = 复制工程、保留组件层、换 `main/`。**应用逻辑不直接依赖裸驱动**(如
物理只问 IMU 要"倾斜向量",不碰 I2C 寄存器)。各组件职责/坑/示例见 `components/*/README.md`,复用指南见
`docs/platform/BSP_GUIDE.md`。

```
components/
  core2_board/     一键 bring-up:core2_board_init(enable_leds) 固化初始化顺序
                   (AXP192 → LCD/触摸/LVGL/喇叭 → 开 EXTEN 供 5V → 灯带 → PORT.A 懒加载)
  core2_power/     AXP192 直控:M-Bus 5V(EXTEN)/ 背光真开关(DCDC3)/ 底层寄存器读原语
  core2_sleep/     两级省电编排(打盹 / 深度省电 / 去抖唤醒;可 force_stage 手动驱动,见 §7)
  motion_detect/   "有没有人在动它"检测(帧间加速度差 + 去抖,纯逻辑)
  app_slot/        多 App 启动选择(otadata)+ 回 factory + app_slot_info(数据驱动 launcher 用)
  imu_mpu6886/     MPU6886 最小驱动:输出三轴加速度(g)
  audio_fx/        音效引擎(esp_codec_dev,程序化合成 + play_notes,见 §5)
  haptics/         震动马达(AXP192 LDO3)+ 震动模式库(见 §5)
  ledstrip_fx/     底座 SK6812 灯效(见 §5)
  power_monitor/   AXP192 遥测:电池/VBUS 电压电流 + 充电状态(+ 库仑计,视寄存器查证结果)
  kv_store/        NVS 封装(标定值/设置持久化,每 app 一个 namespace)
  ui_kit/          评估台 UI 控件:状态栏 / 数值卡 / chart / 列表菜单(全守 §6 渲染红线)
  data_log/        串口 CSV 导出(时间戳自动打点),SPIFFS 离线录制(规划中)
  units/           PORT.A/C 外接单元驱动:unit_8encoder / unit_ultrasonic / unit_dlight /
                   unit_gesture / unit_scd41(CO2L,CO₂/温/湿)/ unit_rgb / chain_bus +
                   unit_chain_encoder / unit_chain_joystick /
                   unit_probe(已知地址表 + 全总线扫描,见 §10)
  screenshot/      串口触发屏幕截图(调试设施,core2_board_init 代调):主机跑
                   tools/screenshot.py 把设备当前屏抓成 PNG,AI 协作者直接 Read"看屏"
                   验收 UI(**何时用/怎么用见 §10.1**)。依赖 CONFIG_LV_USE_SNAPSHOT
                   (已进 sdkconfig.platform)。屏下 BtnB 也可触发,主机用
                   `screenshot.py --watch` 常驻监听接收(见 touch_btns)
  touch_btns/      屏下三个圆圈键区(BtnA/B/C)的全局三键(core2_board_init 代调):
                   旁路 LVGL 直读 FT6336U(0x38)屏外坐标——默认 BtnA长按=回launcher /
                   BtnB短按=截屏 / BtnC长按=关机。所有 app(含游戏)白拿,app 侧零代码;
                   每键短/长按均可 touch_btns_bind() 覆盖(cb=NULL 复位默认)。命中阈值
                   (BTN_Y_MIN/x 分界)是唯一需实机标定的旋钮,见组件 README
```

### 任务 / 队列模型(FreeRTOS)

| 任务 | 频率 | 职责 |
|---|---|---|
| LVGL task | BSP 自带 | 屏幕刷新(**所有 LVGL 调用包 `bsp_display_lock/unlock`**) |
| `app_task`(各 app) | 按需(评估台通常 5~30Hz 足够,不必守游戏时期的 60Hz 手感线) | 读传感器/遥测 → 更新数值卡/chart(经 LVGL lock)→ 按需 `data_log_row`;**久置仍需 `core2_sleep_feed()` 喂加速度**(除非该 app 显式接管省电,见 §7) |
| `audio` / `haptics` / `ledstrip` | 事件驱动 / 低频 | 各自消费事件队列,异步执行,**绝不阻塞主循环** |

> 任务间用 queue 传"事件"解耦反馈通道。渲染跟不上时逻辑可多步、画面丢帧,数据完整性优先于帧率。

---

## 5. 反馈通道能力(音频 / 触觉 / 灯带)

平台提供**四条反馈通道**;评估 app 按需编排(不强制像游戏时期那样每个事件都四通道齐鸣——评估场景
以"数值/图表"为主信息通道,音/震/灯带降级为状态提示,如"init 成功"一声、"错误"一次报警震动)。
视觉通道 = 各 app 自己画(守 §6 渲染纪律);音频/触觉/灯带三条是共享组件,共用一套反馈词汇表:
**hello / bump×3(轻中重)/ near / collect / win / wake**。

### 5.1 音频(`audio_fx`,基于 esp_codec_dev)

- **走 BSP 音频**:`bsp_audio_codec_speaker_init()` 拿 codec handle → `esp_codec_dev_open/write/close` 播 PCM。
  BSP 把 NS4168 经 I2S 封装好,SPK_EN(AXP192 IO2)由 BSP/AXP 管。
- **音效来源**:平台默认**程序化合成**(sine + 包络,首尾淡入淡出);也可播预制小 PCM 片段。`audio_fx_play_notes()` 播自定义音序(滑音/琶音,power_lab 的"喇叭测试音"负载项可直接用它放一段持续音)。
- 🔴 **防爆音纪律**:**整局保持喇叭 open**,空闲写静音,**不要每个音效 close+open / 反复 toggle SPK_EN**(会咔哒);片段首尾短淡入淡出;只在采样率/声道变化时才 reopen。
- **音量** `esp_codec_dev_set_out_vol(...)` 默认中等,留上限防过响。

### 5.2 触觉(`haptics`,AXP192 LDO3)

- 马达由 **AXP192 LDO3** 供电。优先用 BSP `bsp_feature_enable(BSP_FEATURE_VIBRATION, ...)`;要精细时长/节奏再直接控 LDO3(**先查 AXP192 寄存器再写**)。
- **震动模式库**(短而不腻):`BUMP_LIGHT/MED/HARD`(~30/60/100ms,力度随事件)、`COLLECT`(~25ms)、`WIN`(如 80ms×3 节奏)、`HELLO/WAKE`(一下轻震);评估台可复用作"错误报警"(如 BUMP_HARD)。
- 独立任务/定时器跑,**绝不阻塞主循环**。

### 5.3 底座灯带(`ledstrip_fx`,SK6812 ×10 @ G25)

- 10 颗 SK6812 单数据线 **G25**,WS2812 时序、GRB,`espressif/led_strip` + RMT 后端,灯数=10。
- 评估台用法:状态提示(init 成功/错误)、power_lab 的负载开关矩阵可直接把灯带亮度(0/48/255)
  当一个可控负载项观察电流跳变。
- **亮度可调**(`ledstrip_fx_set_max_brightness`,不再强制幼儿低亮上限,按评估可见性需要设置)。
- 🔴 **G25 叠底座后被灯条占用**,别再当别的用。🔴 **灯带吃 M-Bus 5V(EXTEN),BSP 从不开 → 不亮**(数据线照翻、`refresh` 照返回 OK,极易误判坏);EXTEN 由 `core2_board_init` 代开,详见 §7。

---

## 6. 渲染纪律与预算(经典 ESP32 硬约束)

> **Confirmed via espressif-docs / esp-component-registry**:Core2 BSP 用 `esp_lvgl_port` 起 LVGL,底层 esp_lcd
> SPI panel 用 `esp_lcd_panel_draw_bitmap` 局部刷新;移动对象只更新脏矩形,无需整屏重绘。

⚠️ **这是硬规矩,不是建议。** Core2 是经典 ESP32(LX6),**没有任何 2D 加速 / DMA2D / PPA**,所有合成都是 CPU
软件渲染;屏走 **SPI**。**渲染瓶颈是 SPI 带宽,不是算力。** 一上来"整屏刷新",帧率必死。**每张卡带都守这一章。**

### 6.1 三层渲染模型(成败核心)

| 层 | 何时画 | 代价 | 内容 |
|---|---|---|---|
| **静态层** | **每关/每场景载入时画一次**,之后不重画 | 付一次(~31ms) | 背景、面板边框、标签文字 |
| **动态层** | 每帧/每次推点,**只刷脏矩形** | 极小(~2ms) | 数值卡的数字、chart 新增的点、状态栏的电量/uptime |
| **特效层** | 事件触发,短生命周期 | 视情况,见 §6.5 | 阈值告警闪烁、连接成功提示 |

> **关键推论:静态层再"丰富"几乎免费(只付一次)。** 面板边框/标签这些不变的部分画进静态层、别每帧动。LVGL 的 dirty-rect 天生如此。

### 6.2 帧预算:瓶颈是 SPI 带宽

按 BSP 默认像素时钟 **40MHz**、RGB565(16bpp):吞吐 ≈ 40e6/16 ≈ **2.5M 像素/秒**(≈2.5 px/µs)。

- **整屏** 320×240 = 76,800 px → 一帧 ≈ **31ms** → 整屏重绘天花板仅 **~30fps**(还没算合成)。
- **小脏矩形** ~70×70 ≈ 4,900 px → ≈ **2ms** → 数值卡刷新可稳 **60fps**,只占带宽 ~12%。

**硬规矩:**
1. **永不每帧整屏重绘。** 整屏级刷新只允许出现在:**进页/切换视图** 和 **一次性提示弹窗**。
2. 静态层进页画一次(面板/标签/边框);页内只重画数值/chart 的脏矩形。
3. **每帧"在动的像素"预算**:常态 loop **≤ ~15,000 px/帧**(≤ ~6ms);评估台数值卡+chart 一般远低于此。
4. **LVGL 用部分缓冲**(BSP 默认 `H_RES*50`),**别开 `full_refresh`、别分配整屏 framebuffer 双缓冲**。
5. SPI flush 走 **DMA 异步**(esp_lcd/BSP 已是),刷屏时 CPU 不空等。
6. 所有 LVGL 操作包 `bsp_display_lock(0) … bsp_display_unlock()`。

### 6.3 色彩:扁平优先,RGB565 注意 banding

屏是 **RGB565**(绿才 6 位),**软渐变会肉眼可见分层(banding)**。**扁平大色块优先**(最省,工程风 UI 天然扁平)。
真要渐变/纹理 → **烘进 flash 的预渲染图**,载入时贴一次、不每帧算。

### 6.4 alpha / 发光:烘进静态层,别每帧混合

经典 ESP32 上 **alpha blend 是纯 CPU,很贵**。半透明高亮/边框阴影 → **预渲染进背景或干脆用不透明色块**(评估 UI 不需要游戏时期的柔光/脉动效果,直接省掉这条负担)。

### 6.5 数值/波形 UI 脏矩形套路(评估台专属,`ui_kit` 落地要点)

- `ui_status_bar`(顶 24px):1Hz 更新,只重画自己这一条状态栏,不触碰下方内容区。
- `ui_value_card`(标签+大数值+单位):数值变化时只重画卡片内的数字文本区域(LVGL `lv_label`
  重设 text 天生只脏自身矩形),标签/边框/单位在创建时画一次不动。
- `ui_chart`(`lv_chart` 封装):用 `lv_chart_set_next_value` 做环形推点,LVGL 内部只重绘新增点
  影响的窄条,不整图重算;点数上限按屏幕分配的宽度定(如 ~180px 宽配 60~90 个点足够看出趋势)。
- 阈值变色(数值超界变红)是**离散状态切换**(正常色/告警色两态直接切换 `lv_obj_set_style_text_color`),
  不做渐变过渡——省去 alpha 混合成本,也更符合"错误显式呈现"的原则 2。

### 6.6 各动效帧率档位

| 动效 | 帧率 | 约束 |
|---|---|---|
| 传感器/遥测轮询 | 1~30Hz(视评估对象定,power_lab 遥测面板默认 1Hz) | 纯数学 + 读传感器,便宜,与渲染解耦 |
| 数值卡/状态栏刷新 | 跟随轮询频率 | 小脏矩形(~2ms) |
| chart 推点 | 2~10Hz | 各自小区域,合计动的像素压在 §6.2 预算内 |
| 一次性提示(连接成功/告警弹窗) | 单次,非循环 | 柔和、非频闪,§8 光敏安全仍适用 |

### 6.7 ili9342 偏色 / 偏移备选

若 BSP 的 ili9341 兼容驱动在本屏**偏色/偏移/镜像**,改用 `jbrilha/esp_lcd_ili9342` 或给 panel 设 `mirror/swap_xy/gap`
——ILI9342C vs ILI9341 常见差异,实测确认。**亮度** `bsp_display_brightness_set(...)` 按评估可见性需要设置(§8);idle 进一步调低(也省电,§7)。

---

## 7. 电源 / 休眠 / 背光(直接控 AXP192)

**AXP192 必须先配好**(BSP `CONFIG_BSP_PMU_AXP192`)——否则屏黑/无声/无触摸,别误判成代码或接线坏。每个驱动 init 用
`ESP_ERROR_CHECK` 包住,**静默 init 失败是第一耗时坑**(`AGENTS.md` §3)。改 `sdkconfig.defaults` 后**必须删 `sdkconfig` 再 fullclean**。

设备会被**摇晃/磕碰/猛倾**:传感器读数滤波 + 速度封顶 + 死区,避免异常值让画面瞬移或卡死。电池仅 Bottom2 500mAh,**评估台默认仍要省电**(power_lab 例外——它本身就是评估功耗的工具,会主动接管/演练休眠阶段)。

### 7.1 两级省电机制(`core2_sleep` + `core2_power`)

> 三级状态由 **IMU 单一信号**驱动;灯带与背光是**两条独立 AXP192 电源**,分别断/恢复。踩过的三个坑见本节末——都属
> 「BSP 不管、得直接控 AXP192」那一类。评估 app 每帧 `core2_sleep_feed()` 喂加速度即可(桌面玩法/单元评估另需 `core2_sleep_kick`,见 §10)。power_lab 可用 `core2_sleep_force_stage()` 手动触发 NAP/DEEP 做休眠演练(不必等真的静止 60s)。

**状态流**:`PLAY ──(机身静止 12s)──► IDLE 打盹 ──(再静止 60s)──► DEEP_IDLE 深度省电`;任一休眠态检测到「真的动了」→ 唤醒 → 回 PLAY。

**核心信号——机身动作量**(每帧算一次):`s_motion = |Δax| + |Δay| + |Δaz|`(帧间三轴加速度变化,g)。平放静止
≈0.005~0.03(噪声尖峰偶达 ~0.08),被拿起/倾斜 >0.12(`IDLE_WAKE_THRESH`)。**「有没有人在操作」只由此判定**,与评估对象自身的读数变化无关(unit_bench 例外,见 §10 桌面玩法省电坑)。

- **进入打盹(PLAY→IDLE)**:`s_motion` 连续低于阈值累计 750 帧(12s)→ 背光 60%→10%、灯带转慢呼吸。
- **进入深度省电(IDLE→DEEP_IDLE)**:再静止 3750 帧(60s)→ `brightness_set(0)` → **`core2_power_backlight(false)`(断 DCDC3 → 背光真全黑)** → 灯带熄 → `core2_power_bus_5v(false)`(切 M-Bus 5V,断灯带/单元供电 + 省 SY7088 静态电流)→ 轮询周期降到 120ms。
- **唤醒(去抖)**:要**连续 3 帧** `s_motion>0.12` 才算真动 → 依次 `bus_5v(true)` → `backlight(true)`(重启 DCDC3)→ 背光回 play → 灯带回常态 → `HAPTIC_WAKE` → 回 PLAY。

**两条独立电源**(均在 `core2_power` 经 AXP192 读改写):

| 通道 | 供电 | 使能位 | 熄灭方法 |
|---|---|---|---|
| **屏背光** | AXP192 **DCDC3**(电压在 REG `0x27`) | REG `0x12` **bit1**(0x02) | `core2_power_backlight(false)` 清 bit1 |
| **底座灯带 / PORT 单元 5V** | M-Bus 5V(SY7088 升压) | AXP192 **EXTEN** = REG `0x12` **bit6**(0x40) | `core2_power_bus_5v(false)` 清 bit6 |

**三个坑(为何这么设计)**:
1. **打盹判据不能用评估对象自身的读数**:机身平放但被评估单元仍在活动(如旋钮被转动)时,若用单元读数变化判定,会永不打盹或反过来永不唤醒。→ 只看 `s_motion`(unit_bench 需要额外 `core2_sleep_kick` 补桌面场景,见 §10)。
2. **唤醒必须去抖**:平放时 IMU 单帧噪声尖峰偶尔 >0.12,会误唤醒并把 60s 深度省电计时整段作废。→ 连续 3 帧才算真动。
3. **`brightness_set(0)` 根本不熄屏**:BSP 亮度只调 DCDC3 电压(公式 `reg=90+8*pct/100`,范围仅 90~98 ≈ 2.95~3.15V),0% 还剩 2.95V。→ 真熄屏必须**断 DCDC3 使能**(REG 0x12 bit1)。

> 未上真·light-sleep(避免 IMU INT → GPIO 唤醒的复杂度);关背光 + 切 5V 已是最大两个耗电点。
> `core2_sleep_force_stage(s, stage)`(2026-07-17 新增)复用组件内固化的这套顺序,供 power_lab
> 手动进 NAP/DEEP 做演练,**不许 app 散装重拼这套顺序**。

---

## 8. UI 可读性纪律 / 无障碍 / 数据安全

> 2026-07-17 起本节从「幼儿安全」改写为「UI 可读性纪律」——受众从幼儿改为识字的 IoT 评估者,
> 但硬件层面的光敏安全约束与显示器物理约束无关受众,原样继承。

**字号规范(评估台 UI 的可读性红线)**:
- 状态栏文字:14~16px(`CONFIG_LV_FONT_MONTSERRAT_16`)。
- 数值卡的核心数字:24px(`CONFIG_LV_FONT_MONTSERRAT_24`),标签/单位用 16px。
- 数值**必须带单位**(mV/mA/lx/mm 等),不裸给数字——评估场景下单位缺失是常见的误读来源。
- **错误显式红字**:init 失败/读超时/总线拉死,一律用告警色(暖红,如 `0xD9483A`)文字 + 简短
  错误码,不用图标/表情代替文字(§2 原则 2)。
- **密度上限**:320×240 单屏最多 **4 个数值卡 + 1 个 chart**(或等价信息量),超了分页,不挤成
  小字——挤爆一屏会同时违反可读性和 §6 渲染红线(小脏矩形太多、合计像素超预算)。

**光敏安全(硬件约束,受众无关,继续硬性遵守)**:
- **无全屏快速频闪 / 无高频强对比闪烁**;告警提示可以用告警色常亮或慢闪(≥1s 周期),不用快闪
  抓注意力。
- 亮度按评估可见性设置即可(不再强制幼儿低亮上限),idle 仍进一步调暗省电。

**家长控制章节已废弃**:评估台面向识字的成年评估者,不再需要"防误触长按菜单"这类幼儿场景专属
设计;设置/标定持久化改用 `kv_store`(NVS),无"家长专属"语义,任何评估者都可调。

---

## 9. 多 App 分区 / 构建 / 单刷(`app_slot`)

仓库 = **游戏 + 评估卡带机**:factory 常驻 launcher,6 个 ota 槽各一个独立 app bin(ota_0~3 游戏、
ota_4~5 评估)。机制 = IDF otadata 启动选择
(`esp_ota_set_boot_partition`,**无网络成分**)。形态/接入改动/槽位表见 `launcher/README.md` + `components/app_slot/README.md`。

**硬纪律(勿忘)**:
- 🔴 编译走命令行 `idf.py -C launcher|apps/<app> build`(esp-idf MCP build 固定指仓库根、多工程后不可用)。
- 🔴 **评估 app 工程严禁 `idf.py flash`**(会烧 0x10000 覆盖 launcher);单刷用 `tools/flash_one.sh <app>`(= `esptool write-flash <槽偏移> <bin>`,偏移见 `tools/flash_map.md`)。
- 🔴 `partitions.csv` **定案冻结,改表 = 全量重刷**。
- 每个 app `app_main` 首行 `app_slot_return_to_factory()`(崩溃/复位回 launcher)。**电源键 PEK 检测
  2026-07-09 已整体取消**(`core2_power_pek_pressed()` / `app_slot_enable_button_exit()` 均已删除,
  不再有任何电源键触发的软件动作);电源键唯一剩下的行为是 AXP192 硬件本身按住 ≥4s 强制断电
  (纯硬件,不经软件,原本就拦不住)。回 launcher 目前**没有软件入口**(评估台无游戏时期的家长
  菜单概念),只能靠崩溃/复位或整机断电重开——若某评估 app 需要页内"返回"按钮,自行在 UI 里加
  一个调 `app_slot_return_to_factory()` + `esp_restart()` 的按钮即可,不是平台强制项。
- 各工程共享根部 `components/`、`partitions.csv`、`sdkconfig.platform`(经 `SDKCONFIG_DEFAULTS`/`EXTRA_COMPONENT_DIRS` 跨目录引用)。

---

## 10. 做新 App 指南:PORT.A/C 单元接入 + 桌面评估省电坑

脚手架 `tools/new_app.sh <名>`;守 §2 原则、§6 渲染纪律,复用 §4/§5 组件。已接 5 类外接单元
(8Encoder / 超声波 / DLight / Chain Encoder / Chain Joystick),沉淀出通用套路:

- 🔴 **PORT.A(I2C @G32/G33,I2C_NUM_0)/ PORT.C(UART)供电 = M-Bus 5V(EXTEN)**:电池供电时 EXTEN 没开单元没电,
  插 USB 时 VBUS 直通会掩盖("插电脑能玩、拔线失灵")。`core2_board_init(enable_leds=true)` 已代开;与底座灯带同一路
  (灯带亮 = 5V 有电,可当探针)。PORT.A 外接 I2C 经 `core2_board_port_a()` 懒加载。
- 🔴 **MCU 固件从机的 I2C 读协议**:8Encoder / 8Angle 这类内部跑 MCU 固件的单元,**只在收到 STOP 才解析寄存器号**,
  用 `i2c_master_transmit_receive`(repeated-start 组合读)会钳死总线、断电才恢复。**读必须拆成"写寄存器号+STOP,再单独读"两笔事务**。
  纯传感器芯片(RCWL 超声波 / BH1750 光)不吃这套,但照此写也对。通用规则见 `docs/units/_MCU_Firmware_I2C_Units.md`,踩坑始末见 git 历史(busy_knobs 已删,过程留档 `git log`)。
- 🔴 **桌面评估省电坑**(机身不动 ≠ 没人在评估):旋钮转 / 手挥 / 光变 / 摇杆推都要 `core2_sleep_kick`,否则评估中打盹。
  NAP 时 5V 还在、单元活动可唤醒;DEEP 切 5V → 单元断电复位,唤醒后 `unit_attach` 重接管(深度态只能拿起机身唤醒)。
  **unit_bench 场景建议直接 `manage_bus_5v=false` 且不主动进 DEEP**——深睡切 5V 会杀掉被测单元的供电,
  这是评估台语境下的错误行为(不同于游戏时期"省电优先"的默认取向)。
- **热插拔 / 容错通用形态**:单元 init 可重复调用(支持热插拔重试);没插 = 无字提示卡 + 2s 周期重试,插上即"你好"接管;连续 ~20 帧读失败判拔线回提示卡。
- **Chain(PORT.C UART 菊花链)**:传输层 `chain_bus`(UART2 G14 TX/G13 RX,`chain_bus_request`/`chain_bus_sniff`),协议从官方库逐字节核实;⚠️ Core2 直连 Chain host 官方未背书,不通时 `chain_bus_sniff` 抓原始字节自诊。`chain_bus` 已内置互斥锁(多任务并发访问总线安全),app 层不要重复加锁。
- **launcher 自动发现**:每加一个评估 app,launcher 用 `app_slot_info()` 直读该 app 编译进去的
  `esp_app_desc_t`(工程名/版本/编译日期)数据驱动渲染卡片,**无需改 launcher 代码、无需重刷
  launcher**(与游戏时期"必须加图标分支+重刷"彻底不同,这是识字受众带来的架构简化)。

### 10.1 屏幕截图自查(`screenshot` 组件 + `tools/screenshot.py`)

AI 协作者可以自己"看"设备屏幕,不必事事等人对着实机描述画面:

```bash
python3 tools/screenshot.py [/dev/ttyUSB0] [out.png]   # 最后一行打印 PNG 绝对路径 → Read 它
```

**何时用(主动用,别闲置)**:
- **单刷/重刷后的实机点检第一步**:待验证清单里凡是"看画面就能判断"的项(布局、配色、数值卡/
  chart 是否正常渲染、状态画面、launcher 槽卡片数据驱动是否正确)先截图自查,把需要人来的验证
  压缩到手感/声/震动/灯带/真实读数准确性这些截图看不见的通道。
- **排查"画面不对"类 bug**:先截一张拿证据再改代码,别凭描述猜。
- **改 UI 前后各截一张**,对比确认改动生效(配合 git 里的截图描述留档更佳)。

**前提与限制**:
- 截图只有**视觉通道**:数据准确性、音效、震动、灯带、打盹时序仍要真人/串口日志验收。
- 只截 LVGL 活动屏;与 `idf.py monitor` 互斥(串口独占),触发瞬间持 LVGL 锁 ~50ms,不打断评估循环。
- 协议与设计取舍见 `components/screenshot/README.md`。

---

## 11. 必查 MCP 清单 + 坑位速查

### 11.1 必查 MCP(`AGENTS.md` §1 强制;每次查后在对话里写 "Confirmed via …")

实现前/中**先查 MCP 再写**,至少覆盖:
- **MPU6886**:寄存器图(WHO_AM_I / 电源管理 / ACCEL_CONFIG / 数据寄存器 / 量程)。查 datasheet,**勿照搬 BMI270**。
- **BSP `espressif/m5stack_core_2`**:`bsp_display_start*` / `bsp_display_lock/unlock` / `bsp_display_brightness_set` / `bsp_i2c_get_handle` / `bsp_audio_codec_speaker_init` / `bsp_feature_enable(BSP_FEATURE_VIBRATION)` 的签名与 Kconfig(`CONFIG_BSP_PMU_AXP192`)。
- **esp_codec_dev**:`esp_codec_dev_open/write/close/set_out_vol` 的格式结构体与生命周期。
- **LVGL**(随 BSP 版本):canvas / `lv_chart` / `lv_anim` / 坐标更新 / 脏矩形;**确认部分缓冲(BSP 默认 `H_RES*50`)、未开 `full_refresh`**;SPI 像素时钟 40MHz 与 flush DMA 异步——决定 §6 帧预算是否成立;字体宏 `CONFIG_LV_FONT_MONTSERRAT_16/24` 的 Kconfig 路径。
- **led_strip**:RMT 后端在经典 ESP32 上的配置(SK6812/WS2812 时序、灯数=10、G25)。
- **AXP192**(绕过 BSP 直控 LDO3/DCDC3/EXTEN,以及 `power_monitor` 要用的电池/VBUS 电压电流 ADC、
  充电状态位、库仑计寄存器时):对应寄存器/通道**必须逐一核实**,查不到就在 README 记「待查证」
  砍掉对应字段,不许猜(本会话若 Espressif MCP 不可用,退路是 WebFetch/WebSearch 核对
  `github.com/m5stack/M5Unified`、`github.com/m5stack/M5Core2` 的 AXP192 驱动源码与 X-Powers
  AXP192 datasheet)。
- **`esp_pm` / DFS 动态调频**(power_lab 的 CPU 锁频负载项要用):`esp_pm_configure` 签名与
  `CONFIG_PM_ENABLE`/`CONFIG_FREERTOS_USE_TICKLESS_IDLE` 等相关 Kconfig,查不通就降级(只做
  两档或砍掉该负载项,不猜 API)。
- **外接单元**:MCP 缺板级细节时,退到厂商 GitHub raw(如 Chain 帧格式取自 `m5stack/M5Chain` 源码)。

> 已核实记录(供人工核对):**Confirmed via esp-component-registry** — `espressif/m5stack_core_2` v3.0.1(target esp32,V1.0 用
> `CONFIG_BSP_PMU_AXP192`,`BSP_CAPS_IMU=0`,`BSP_I2C` SDA=21/SCL=22,含 `BSP_FEATURE_VIBRATION`);`espressif/bh1750` v2.0.0
> 连续模式;`jbrilha/esp_lcd_ili9342` 备选。**Confirmed via espressif-docs** — esp_lcd SPI panel 以 `esp_lcd_panel_draw_bitmap` 做局部刷新。

### 11.2 坑位速查(开工前再扫一眼)

- **AXP192 不初始化 = 屏黑/无声/无触摸**,别误判硬件坏(BSP 已处理,但 Kconfig 选错 PMU 版本会中招)。
- **IMU 是 MPU6886 不是 BMI270**,寄存器与轴向**都别照搬 BMI270**;轴↔屏映射实测标定。
- **音频脚 G0/G2/G12 是 strapping**、已被板载占用,正常别动;喇叭别反复 toggle SPK_EN(咔哒)。
- **经典 ESP32 PSRAM 直映射 ≤4MB**,大缓冲按 4MB 算。**电池仅 Bottom2 500mAh**,评估 app 默认仍做 idle 省电(power_lab 例外,见 §7)。
- **改 sdkconfig.defaults 后删 sdkconfig 再 fullclean。**
- **底座必装**:IMU(0x68)与电池都来自 Bottom2;灯带 G25 常驻,G25 不能再做别的。开机先确认底座在位、I2C 扫得到 0x68。
- 🔴 **灯带/PORT 单元全黑或无响应先查 EXTEN**:吃 M-Bus 5V(SY7088 升压,AXP192 EXTEN 使能),**BSP 从不开**;数据线正常翻转、`refresh` 返回 OK 也可能全黑。必须自己开 EXTEN(`core2_power`、§7)。**别误判成硬件坏**。
- 🔴 **`brightness_set(0)` 不熄屏**:背光=AXP192 DCDC3,0% 仍 ~2.95V。深度省电真黑屏必须断 DCDC3 使能(REG 0x12 bit1,`core2_power_backlight()`,§7)。
- **屏下三个圆圈键区走 `touch_btns`,不是 LVGL 按钮**:FT6336U 面板 320×280,圆圈在屏外 y≥240,LVGL indev(240 高画布)接不到 → 组件旁路 LVGL 直读 0x38 原始坐标。三键=BtnA长按回launcher/BtnB短按截屏/BtnC长按关机,全局固定所有 app 白拿。命中阈值(`BTN_Y_MIN`/x 分界)**需实机标定**(首刷看串口 `raw touch` 日志回填,见组件 README)。**关机=AXP192 0x32 bit7 RMW**(`core2_power_shutdown()`),别整字节写(0x32 还管电池检测/CHGLED)。
- **打盹判据只看机身动作(IMU),别看评估对象自身读数**(单元评估场景下用 `core2_sleep_kick` 补桌面场景,§10)。
- 🔴 **MCU 固件单元用 repeated-start 组合读会钳死总线**:读拆两笔事务(§10、`docs/units/_MCU_Firmware_I2C_Units.md`)。
- **渲染红线:永不每帧整屏重绘**(§6)。静态层进页画一次、页内只刷脏矩形;扁平色优先(RGB565 banding)。
- 🔴 **LVGL 文本格式化不能用 `%f`**:`lv_label_set_text_fmt` / `lv_snprintf` 走 LVGL 自带精简 printf,平台 **`CONFIG_LV_USE_FLOAT` 未开**(默认关),`%f` 分支被条件编译掉、格式符 `f` 落到 default 分支被**原样打印**——数值卡会显示字面 **"f"** 而非数字(症状:`ui_value_card` 只出 "f 单位",chart 却在动)。要显示小数,手工四舍五入拆整数/小数位用 `%d.%d`(`ui_value_card.c` 已如此),或全局开 `CONFIG_LV_USE_FLOAT=y`(需 fullclean 重编全工程)。整数格式 `%d/%lu/%s` 不受影响。
- **AXP192 电流测量的坑**(power_lab 专属,写进其 SPEC/README):只测电池路和 VBUS 路,无 per-rail;USB 插着时电池电流≈0,要看 VBUS 电流;真实续航测试必须拔 USB → 没串口 → SPIFFS 离线录制是唯一手段;几 mA 小负载(震动)可能淹在 ADC 噪声里,chart 侧加滑动平均。

---

## 12. 各 App 竣工索引

各 app 的 **as-built(定案数值 / 落地差异 / 待实机 / 特有踩坑)在各自 README**,功能规格在 SPEC。
**App→槽→外设→状态→文档 一览见 §1「各 App 索引」**(同一张表,此处不重复)。

> 四张游戏 2026-07-17 早随平台转向被整体删除、当晚用户修订为共存后从删除前提交原样恢复
> (仅补丁 `CORE2_BOARD_CFG_KIDS_DEFAULT`→`CORE2_BOARD_CFG_DEFAULT` 改名以配合 Phase 0 的宏
> 改名,代码逻辑不做任何改动),槽位从原分配重排至 ota_0~3。平台层跨应用踩坑(EXTEN/DCDC3/
> repeated-start/桌面省电)已归入 §7 / §10 / §11。历史演进(幼儿掌机时期 6 张游戏卡带的完整
> 设计/踩坑记录、2026-07-17 平台转向决策过程)见 git log 与 `docs/ROADMAP.md`。
