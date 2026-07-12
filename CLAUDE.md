# CLAUDE.md — 幼儿游戏掌机平台(Core2 卡带机)

> **本仓库不是单个 app,是一台「幼儿游戏掌机 / 卡带机」平台**:硬件底座 = M5Stack Core2 + M5GO
> Bottom2;软件 = factory 分区常驻 **launcher 选择页** + 6 个 ota 槽各放一个独立「卡带」(零失败
> 幼儿小游戏)。改一个游戏只重编+单刷它自己,launcher 与其它游戏零风险。
>
> 本文件是**平台级说明书**(共守的设计原则、硬件事实、组件层、反馈/渲染/电源纪律、做新卡带的规矩、
> 各 app 索引)。配合通用规范 `AGENTS.md` 和板级事实 `docs/platform/Core2_v1_0.md`(主控)、
> `docs/platform/M5GO_Bottom2.md`(**必装底座**)。**板级硬件事实以那两份 HARDWARE.md 为准,本文不重复引脚表、只引用。**
>
> ⚠️ **硬件平台是「Core2 + M5GO Bottom2」组合体,不是 Core2 单体。** 本机 Core2 核缺背部扩展模块,
> **IMU 与电池均由 Bottom2 提供**;不接 Bottom2 就没 IMU、没电——绝大多数卡带跑不起来。底座是硬依赖。
>
> **各卡带的东西不在本文**:某个游戏的**玩法规格**见 `apps/<name>/SPEC.md`(目前 tilt_maze 与
> peekaboo 有完整 SPEC),**竣工现状/定案数值/待实机项**见 `apps/<name>/README.md`。做新卡带前先读本文 §2(原则)、§4–§10。
>
> 目标读者:负责实现的 AI 协作者(Claude Code)。**涉及 ESP-IDF / 组件 API 的地方先查 MCP 再写**(`AGENTS.md` §1,清单见 §11)。

---

## 1. 平台定位

一台给 **3~4 岁幼儿**(不识字、手部精度低、易过度操作、注意力短)的游戏掌机:

- **物理形态**:Core2 主机 + Bottom2 底座(IMU / 500mAh 电池 / 10×SK6812 灯带 / PORT.A·B·C 扩展口)。
- **一机多游戏**:开机进 launcher 卡带架 → 点图标进某游戏 → **家长菜单 Home 回 launcher**
  (电源键退出 2026-07-09 已取消,见 §9;⚠️ 目前仅 tilt_maze 接了家长菜单,其余卡带
  游玩中暂无回 launcher 入口,只能靠崩溃/复位或整机断电重开);崩溃/断电也自动回 launcher。
- **每张卡带的共同体验**:拿起某种输入(倾斜 / 旋钮 / 超声波 / 光 / 摇杆)→ **< 100ms 内多通道反馈**
  (画面+声+震动+灯带)→ **零失败**。没有文字、没有计时、没有 game over。
- **多样的输入靠外接单元**:IMU(内置)、PORT.A 的 Grove I2C 单元(8Encoder / 超声波 / DLight …)、
  PORT.C 的 Chain UART 菊花链(编码器 / 摇杆)。单元接入套路见 §10。

### 各 App 索引

| App | 槽 | 外设 | 状态 | 文档 |
|---|---|---|---|---|
| **tilt_maze** 倾斜迷宫 | ota_0 | IMU MPU6886 | ✅ 实机验证(M0–M5+打盹) | `apps/tilt_maze/SPEC.md` + `README.md` |
| **busy_knobs** 旋钮忙碌台 | ota_1 | 8Encoder | ✅ 实机验收通过 | `apps/busy_knobs/README.md` |
| **peekaboo** 躲猫猫昼夜屋 | ota_2 | DLight | 🔄 v1 实机试玩偏单调 → v2「夜里来客」重写(见 SPEC) | `apps/peekaboo/SPEC.md` + `README.md` |
| **feed_monster** 喂怪兽 | ota_3 | 超声波 | ⏳ build 通过,待烧录 | `apps/feed_monster/README.md` |
| **chain_lab** 抓娃娃机 | ota_4 | Chain Enc/Joy(UART) | ✅ v2.1 分层实机验证 → 🔄 v2.2 趣味批(玩偶造型+金星彩蛋)⏳ 待烧录 | `apps/chain_lab/SPEC.md` + `README.md` |
| **magic_wand** 魔法萤火虫 | ota_5 | Gesture(PAJ7620U2)+ RGB(P4) | ⏸️ 暂停:v1 九法术否决 → v2 光标跟手实机否决(占空比 50%)→ v2.1 在场+手势重写,P1 已烧录、在场信号标定通过,但整体体感欠佳(2026-07-11),暂不推进;P2–P4 未做 | `apps/magic_wand/SPEC.md` + `README.md` |
| **launcher** 卡带机选择页 | factory | — | ⏳ build 通过,待实机 | `launcher/README.md` |

> 做新 app 从 §10 起步:`tools/new_app.sh <名>` 脚手架;分区偏移/单刷命令见 `tools/flash_map.md`;
> 组件复用指南见 `docs/platform/BSP_GUIDE.md`。

---

## 2. 设计第一性原则(所有卡带共守)

每张卡带都要守这五条 + 一条渲染红线(各 app README 里"守零失败/即时因果/多通道冗余/渲染红线"就是指这里):

1. **零失败**:任何"出错"都不是"输",只是被温柔弹开 + 一个搞笑反馈。永远只有"还没到"和"到了"。
2. **即时且可无限重复**:任何动作 < 100ms 内有反馈;随便玩多久、来回操作都行,无惩罚、无计时。
3. **多通道冗余反馈**:同一事件同时给 **画面 + 声音 + 震动 + 灯带(底座 10×SK6812)**,任一通道都能独立传达"发生了什么"(见 §5)。
4. **宽容物理**:速度封顶、强阻尼、撞墙滑行而非粘住、死区防漂——宁可慢一点也别飘。
5. **幼儿安全**:亮度压低、暖色、**无全屏快速频闪**(防光敏)、音量适中且软启停防爆音(见 §8)。
6. **渲染红线**(硬约束,见 §6):经典 ESP32 无 2D 加速、屏走 SPI,**永不每帧整屏重绘**。

> 不识字是硬前提:信息全靠**脸/颜色/动作/图标/声音**承载,文字仅装饰。大目标、大对象、慢反馈、强宽容——全方位降门槛。

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

- **IMU(MPU6886 @ 0x68,内部 I2C G21/G22)**:倾斜类游戏的命根子。**驱动与 Core2 自带 MPU6886 完全一致**(同芯片同地址同总线),代码无须为底座特化。
- **500mAh 电池 + TP4057 充电**:本机供电来源。
- **10 颗 SK6812 RGB 灯条(数据线 G25)**:**常驻反馈通道之一**(见 §5),不是可选氛围。
- **PORT.A(红,Core2 机身侧面,I2C G32/G33)/ PORT.B(黑,G26 DAC/G36 ADC)/ PORT.C(蓝,UART2 G13/G14)**:外接单元扩展口。PORT.A 接 Grove I2C 单元、PORT.C 接 Chain 菊花链(见 §10)。

> ⚠️ **叠底座后 G25 被灯条占用,不能再当 DAC。** 本机背板缺失,生效的 IMU 就是底座这颗(0x68 无歧义)。
> 🔴 **PORT.A/C 的 5V 由 M-Bus 5V 供**,使能在 AXP192 EXTEN——BSP 从不开,详见 §7 / §10。

### 3.3 组件依赖(已查 ESP 组件库)

| 依赖 | 用途 | 备注 |
|---|---|---|
| `espressif/m5stack_core_2` (BSP, v3.0.1+) | AXP192 电源 / LCD / 触摸 / **LVGL** / NS4168 喇叭一站式 | target esp32;V1.0 配 `CONFIG_BSP_PMU_AXP192`;**`BSP_CAPS_IMU=0` → IMU 需自写** |
| `espressif/esp_codec_dev` | 喇叭播放(BSP 间接依赖) | NS4168 经 I2S,SPK_EN 由 BSP/AXP 管 |
| LVGL(随 `esp_lvgl_port`) | 全部 UI 渲染 | BSP 带 `bsp_display_start*` |
| `espressif/led_strip` | 底座灯条 10×SK6812 @ G25,RMT 后端 | 常驻反馈通道 |
| (备选)`jbrilha/esp_lcd_ili9342` | BSP 的 ili9341 兼容初始化若偏色/偏移再换 | 默认先用 BSP 自带 |

> **关键结论**:BSP 把最容易踩的 AXP192 初始化、LCD/触摸/LVGL/喇叭都包好了;平台自己要管的是 IMU 驱动、
> **EXTEN 供 5V / DCDC3 背光这些 BSP 不碰的 AXP192 电源位**(见 §7)、以及各反馈/单元组件。

---

## 4. 平台组件层(复用 = 平台价值)

外设层沉淀为**可复用组件**,做新 APP = 复制工程、保留组件层、换 `main/`。**应用逻辑不直接依赖裸驱动**(如
物理只问 IMU 要"倾斜向量",不碰 I2C 寄存器)。各组件职责/坑/示例见 `components/*/README.md`,复用指南见
`docs/platform/BSP_GUIDE.md`。

```
components/
  core2_board/     一键 bring-up:core2_board_init(enable_leds) 固化初始化顺序
                   (AXP192 → LCD/触摸/LVGL/喇叭 → 开 EXTEN 供 5V → 灯带 → PORT.A 懒加载)
  core2_power/     AXP192 直控:M-Bus 5V(EXTEN)/ 背光真开关(DCDC3)
  core2_sleep/     两级省电编排(打盹 / 深度省电 / 去抖唤醒,机制见 §7)
  motion_detect/   "有没有人在玩"检测(帧间加速度差 + 去抖,纯逻辑)
  app_slot/        多 App 启动选择(otadata)+ 回 factory(见 §9;电源键退出 2026-07-09 已取消)
  imu_mpu6886/     MPU6886 最小驱动:输出三轴加速度(g)
  audio_fx/        音效引擎(esp_codec_dev,程序化合成 + play_notes,见 §5)
  haptics/         震动马达(AXP192 LDO3)+ 震动模式库(见 §5)
  ledstrip_fx/     底座 SK6812 灯效(见 §5)
  units/           PORT.A/C 外接单元驱动:unit_8encoder / unit_ultrasonic / unit_dlight /
                   chain_bus + unit_chain_encoder / unit_chain_joystick(接入套路见 §10)
```

### 任务 / 队列模型(FreeRTOS)

| 任务 | 频率 | 职责 |
|---|---|---|
| LVGL task | BSP 自带 | 屏幕刷新(**所有 LVGL 调用包 `bsp_display_lock/unlock`**) |
| `game_task`(各 app) | 30–60 Hz 固定 dt | 读传感器 → 逻辑 → 检测事件 → 更新画面(经 LVGL lock)→ 发反馈事件;**每帧 `core2_sleep_feed()` 喂加速度** |
| `audio` / `haptics` / `ledstrip` | 事件驱动 / 低频 | 各自消费事件队列,异步执行,**绝不阻塞 game_task** |

> 任务间用 queue 传"事件"(撞墙/接近/到达/收集/idle 等)解耦反馈通道。渲染跟不上时逻辑可多步、画面丢帧,但手感不变。

---

## 5. 反馈通道能力(音频 / 触觉 / 灯带)

平台提供**四条反馈通道**;app 按"事件 → 各通道"编排(§2 原则 3)。视觉通道 = 各 app 自己画(守 §6 渲染纪律);
音频/触觉/灯带三条是共享组件,共用一套"幼儿反馈词汇表":**hello / bump×3(轻中重)/ near / collect / win / wake**。

### 5.1 音频(`audio_fx`,基于 esp_codec_dev)

- **走 BSP 音频**:`bsp_audio_codec_speaker_init()` 拿 codec handle → `esp_codec_dev_open/write/close` 播 PCM。
  BSP 把 NS4168 经 I2S 封装好,SPK_EN(AXP192 IO2)由 BSP/AXP 管。
- **音效来源**:平台默认**程序化合成**(sine + 包络,首尾淡入淡出);也可播预制小 PCM 片段。`audio_fx_play_notes()` 播自定义音序(滑音/琶音)。
- 🔴 **防爆音纪律**:**整局保持喇叭 open**,空闲写静音,**不要每个音效 close+open / 反复 toggle SPK_EN**(会咔哒);片段首尾短淡入淡出;只在采样率/声道变化时才 reopen。
- **音量** `esp_codec_dev_set_out_vol(...)` 默认中等,家长菜单可调,留上限防过响。持续 BGM 默认关(以免盖过事件音)。

### 5.2 触觉(`haptics`,AXP192 LDO3)

- 马达由 **AXP192 LDO3** 供电。优先用 BSP `bsp_feature_enable(BSP_FEATURE_VIBRATION, ...)`;要精细时长/节奏再直接控 LDO3(**先查 AXP192 寄存器再写**)。
- **震动模式库**(短而不腻):`BUMP_LIGHT/MED/HARD`(~30/60/100ms,力度随事件)、`COLLECT`(~25ms)、`WIN`(如 80ms×3 节奏)、`HELLO/WAKE`(一下轻震)。
- 独立任务/定时器跑,**绝不阻塞 game_task**。家长菜单给总开关(有的家庭夜里不要震动)。

### 5.3 底座灯带(`ledstrip_fx`,SK6812 ×10 @ G25)

- 10 颗 SK6812 单数据线 **G25**,WS2812 时序、GRB,`espressif/led_strip` + RMT 后端,灯数=10。
- 效果:开机暖色呼吸 / 游戏中主题色微光 / 接近目标向目标色加亮 / **过关彩虹迸发或转圈** / idle 极慢呼吸。
- **亮度压低**(满亮刺眼且耗流;`ledstrip_fx_set_max_brightness`,tilt_maze 用 48)。屏幕仍是主信息通道,灯带做氛围与情绪放大,**不让灯带成为某事件的唯一线索**。
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
| **静态层** | **每关/每场景载入时画一次**,之后不重画 | 付一次(~31ms) | 背景、墙体/装饰、烘好的柔光/阴影 |
| **动态层** | 每帧,**只刷脏矩形** | 极小(~2ms) | 主角/移动对象(`lv_image`,每帧改坐标) |
| **特效层** | 事件触发,短生命周期 | 视情况,见 §6.5 | 泛光、亮点、庆祝彩纸 |

> **关键推论:静态层再"丰富"几乎免费(只付一次)。** 想好看就把东西画进静态层、别每帧动。LVGL 的 dirty-rect 天生如此。

### 6.2 帧预算:瓶颈是 SPI 带宽

按 BSP 默认像素时钟 **40MHz**、RGB565(16bpp):吞吐 ≈ 40e6/16 ≈ **2.5M 像素/秒**(≈2.5 px/µs)。

- **整屏** 320×240 = 76,800 px → 一帧 ≈ **31ms** → 整屏重绘天花板仅 **~30fps**(还没算合成)。
- **小脏矩形** ~70×70 ≈ 4,900 px → ≈ **2ms** → 移动对象可稳 **60fps**,只占带宽 ~12%。

**硬规矩:**
1. **永不每帧整屏重绘。** 整屏级刷新只允许出现在:**进关/换场景载入** 和 **庆祝**。
2. 静态层进关画一次(可放 **PSRAM canvas**,~150KB,在直映射 4MB 内);关内只重画动态/特效的脏矩形。
3. **每帧"在动的像素"预算**:常态 loop **≤ ~15,000 px/帧**(≤ ~6ms);超了砍氛围动效、别砍主角流畅。
4. **LVGL 用部分缓冲**(BSP 默认 `H_RES*50`),**别开 `full_refresh`、别分配整屏 framebuffer 双缓冲**。
5. SPI flush 走 **DMA 异步**(esp_lcd/BSP 已是),刷屏时 CPU 不空等。
6. 所有 LVGL 操作包 `bsp_display_lock(0) … bsp_display_unlock()`。

### 6.3 色彩:扁平优先,RGB565 注意 banding

屏是 **RGB565**(绿才 6 位),**软渐变会肉眼可见分层(banding)**。**扁平大色块优先**(最省,也正是更对的幼儿"chunky"审美)。
真要渐变/柔光/纹理 → **烘进 flash 的预渲染图**,载入时贴一次、不每帧算(可在烘图时 dithering 缓解 banding)。

### 6.4 alpha / 发光 / 脉动:烘进静态层,别每帧混合

经典 ESP32 上 **alpha blend 是纯 CPU,很贵**。半透明发光/阴影 → **预渲染进背景**(付一次)。脉动/呼吸 → 缩放一个
**不透明**小精灵或切几张预渲染状态图,**只动那一小块**。渐隐效果 → 用几级**预渲染好透明度的图**切换,不每帧重算 alpha。

### 6.5 各动效帧率档位

| 动效 | 帧率 | 约束 |
|---|---|---|
| 逻辑步进 | 30–60Hz | 纯数学 + 读传感器,便宜,与渲染解耦 |
| 主角渲染 | 跟 60fps | 小脏矩形(~2ms),第一优先,永不为别的让路 |
| **氛围微动**(星眨/窄浪) | **5–10fps** | 各自小区域,合计动的像素压在 §6.2 预算内;**绝不整面在动** |
| **庆祝彩纸** | 可掉到 **15–20fps** | 此刻用户察觉不到掉帧;**限数量/限面积**或预渲染精灵序列;柔和、非频闪 |

### 6.6 ili9342 偏色 / 偏移备选

若 BSP 的 ili9341 兼容驱动在本屏**偏色/偏移/镜像**,改用 `jbrilha/esp_lcd_ili9342` 或给 panel 设 `mirror/swap_xy/gap`
——ILI9342C vs ILI9341 常见差异,实测确认。**亮度** `bsp_display_brightness_set(...)` 压到舒适档(§8);idle 进一步调低(也省电,§7)。

---

## 7. 电源 / 休眠 / 背光(直接控 AXP192)

**AXP192 必须先配好**(BSP `CONFIG_BSP_PMU_AXP192`)——否则屏黑/无声/无触摸,别误判成代码或接线坏。每个驱动 init 用
`ESP_ERROR_CHECK` 包住,**静默 init 失败是第一耗时坑**(`AGENTS.md` §3)。改 `sdkconfig.defaults` 后**必须删 `sdkconfig` 再 fullclean**。

设备会被**摇晃/磕碰/猛倾**:传感器读数滤波 + 速度封顶 + 死区,避免异常值让画面瞬移或卡死。电池仅 Bottom2 500mAh,**必须省电**。

### 7.1 两级省电机制(`core2_sleep` + `core2_power`)

> 三级状态由 **IMU 单一信号**驱动;灯带与背光是**两条独立 AXP192 电源**,分别断/恢复。踩过的三个坑见本节末——都属
> 「BSP 不管、得直接控 AXP192」那一类。游戏侧每帧 `core2_sleep_feed()` 喂加速度即可(桌面玩法另需 `core2_sleep_kick`,见 §10)。

**状态流**:`PLAY ──(机身静止 12s)──► IDLE 打盹 ──(再静止 60s)──► DEEP_IDLE 深度省电`;任一休眠态检测到「真的动了」→ 唤醒 → 回 PLAY。

**核心信号——机身动作量**(每帧算一次):`s_motion = |Δax| + |Δay| + |Δaz|`(帧间三轴加速度变化,g)。平放静止
≈0.005~0.03(噪声尖峰偶达 ~0.08),被拿起/倾斜 >0.12(`IDLE_WAKE_THRESH`)。**「有没有人在玩」只由此判定**,与游戏对象的运动无关。

- **进入打盹(PLAY→IDLE)**:`s_motion` 连续低于阈值累计 750 帧(12s)→ 背光 60%→10%、灯带转慢呼吸。
- **进入深度省电(IDLE→DEEP_IDLE)**:再静止 3750 帧(60s)→ `brightness_set(0)` → **`core2_power_backlight(false)`(断 DCDC3 → 背光真全黑)** → 灯带熄 → `core2_power_bus_5v(false)`(切 M-Bus 5V,断灯带/单元供电 + 省 SY7088 静态电流)→ 轮询周期降到 120ms。
- **唤醒(去抖)**:要**连续 3 帧** `s_motion>0.12` 才算真动 → 依次 `bus_5v(true)` → `backlight(true)`(重启 DCDC3)→ 背光回 play → 灯带回常态 → `HAPTIC_WAKE` → 回 PLAY。

**两条独立电源**(均在 `core2_power` 经 AXP192 读改写):

| 通道 | 供电 | 使能位 | 熄灭方法 |
|---|---|---|---|
| **屏背光** | AXP192 **DCDC3**(电压在 REG `0x27`) | REG `0x12` **bit1**(0x02) | `core2_power_backlight(false)` 清 bit1 |
| **底座灯带 / PORT 单元 5V** | M-Bus 5V(SY7088 升压) | AXP192 **EXTEN** = REG `0x12` **bit6**(0x40) | `core2_power_bus_5v(false)` 清 bit6 |

**三个坑(为何这么设计)**:
1. **打盹判据不能用游戏对象速度**:机身平放但有残余倾斜(斜着拿/板偏置 > 死区)时,tilt_maze 的球会**永远慢爬**,用它判定会永不打盹。→ 只看 `s_motion`。
2. **唤醒必须去抖**:平放时 IMU 单帧噪声尖峰偶尔 >0.12,会误唤醒并把 60s 深度省电计时整段作废。→ 连续 3 帧才算真动。
3. **`brightness_set(0)` 根本不熄屏**:BSP 亮度只调 DCDC3 电压(公式 `reg=90+8*pct/100`,范围仅 90~98 ≈ 2.95~3.15V),0% 还剩 2.95V。→ 真熄屏必须**断 DCDC3 使能**(REG 0x12 bit1)。

> 未上真·light-sleep(避免 IMU INT → GPIO 唤醒的复杂度);关背光 + 切 5V 已是最大两个耗电点。

---

## 8. 幼儿安全 / 无障碍 / 家长控制

**安全(硬性)**:
- **亮度压低 + 暖色**,避免满屏纯白长时间直射眼睛;idle 进一步调暗(也省电)。
- **无全屏快速频闪 / 无高频强对比闪烁**(光敏安全);庆祝特效柔和飘落,泛光为**单次**非连闪。
- **音量有上限**,默认适中,软启停防爆音突响吓到孩子(见 §5.1)。

**无障碍(3~4 岁)**:不识字 → 信息全靠脸/颜色/动作/图标/声音;大目标、大对象、慢反馈、强宽容;永不失败、不卡死。

**家长控制(隐藏,防误触)**:
- 入口:**底部触摸虚拟键长按 3s** 或特定手势(幼儿不会无意触发)。
- 菜单项:**音量、屏幕亮度、震动开关、难度档、背景音乐开关**;大图标 + 滑块,家长一眼可用。
- ⚠️ 已知**家长菜单长按判定偏难触发**(tilt_maze 实测反馈,待排查 FT6336U 触摸/LVGL 长按);热区太窄/长按中途手指微移会打断。

---

## 9. 多 App 分区 / 构建 / 单刷(`app_slot`)

仓库 = **游戏卡带机**:factory 常驻 launcher,6 个 ota 槽各一个独立游戏 bin。机制 = IDF otadata 启动选择
(`esp_ota_set_boot_partition`,**无网络成分**)。形态/接入改动/槽位表见 `launcher/README.md` + `components/app_slot/README.md`。

**硬纪律(勿忘)**:
- 🔴 编译走命令行 `idf.py -C launcher|apps/<app> build`(esp-idf MCP build 固定指仓库根、多工程后不可用)。
- 🔴 **游戏工程严禁 `idf.py flash`**(会烧 0x10000 覆盖 launcher);单刷用 `tools/flash_one.sh <app>`(= `esptool write-flash <槽偏移> <bin>`,偏移见 `tools/flash_map.md`)。
- 🔴 `partitions.csv` **定案冻结,改表 = 全量重刷**。
- 每个 app `app_main` 首行 `app_slot_return_to_factory()`(崩溃/复位回 launcher)+ 家长菜单加 Home
  (回 launcher 的唯一软件入口)。**电源键 PEK 检测 2026-07-09 已整体取消**(`core2_power_pek_pressed()`
  / `app_slot_enable_button_exit()` 均已删除,不再有任何电源键触发的软件动作);电源键唯一剩下的行为是
  AXP192 硬件本身按住 ≥4s 强制断电(纯硬件,不经软件,原本就拦不住)。
- 各工程共享根部 `components/`、`partitions.csv`、`sdkconfig.platform`(经 `SDKCONFIG_DEFAULTS`/`EXTRA_COMPONENT_DIRS` 跨目录引用)。

---

## 10. 做新 App 指南:PORT.A/C 单元接入 + 桌面玩法省电坑

脚手架 `tools/new_app.sh <名>`;守 §2 原则、§6 渲染纪律,复用 §4/§5 组件。已接 5 类外接单元
(8Encoder / 超声波 / DLight / Chain Encoder / Chain Joystick),沉淀出通用套路:

- 🔴 **PORT.A(I2C @G32/G33,I2C_NUM_0)/ PORT.C(UART)供电 = M-Bus 5V(EXTEN)**:电池供电时 EXTEN 没开单元没电,
  插 USB 时 VBUS 直通会掩盖("插电脑能玩、拔线失灵")。`core2_board_init(enable_leds=true)` 已代开;与底座灯带同一路
  (灯带亮 = 5V 有电,可当探针)。PORT.A 外接 I2C 经 `core2_board_port_a()` 懒加载。
- 🔴 **MCU 固件从机的 I2C 读协议**:8Encoder / 8Angle 这类内部跑 MCU 固件的单元,**只在收到 STOP 才解析寄存器号**,
  用 `i2c_master_transmit_receive`(repeated-start 组合读)会钳死总线、断电才恢复。**读必须拆成"写寄存器号+STOP,再单独读"两笔事务**。
  纯传感器芯片(RCWL 超声波 / BH1750 光)不吃这套,但照此写也对。通用规则见 `docs/units/_MCU_Firmware_I2C_Units.md`,踩坑始末见 `apps/busy_knobs/README.md`。
- 🔴 **桌面玩法省电坑**(机身不动 ≠ 没人玩):旋钮转 / 手挥 / 光变 / 摇杆推都要 `core2_sleep_kick`,否则玩着玩着打盹。
  NAP 时 5V 还在、单元活动可唤醒;DEEP 切 5V → 单元断电复位,唤醒后 `unit_attach` 重接管(深度态只能拿起机身唤醒)。
- **热插拔 / 容错通用形态**:单元 init 可重复调用(支持热插拔重试);没插 = 无字提示卡 + 2s 周期重试,插上即"你好"接管;连续 ~20 帧读失败判拔线回提示卡。
- **Chain(PORT.C UART 菊花链)**:传输层 `chain_bus`(UART2 G14 TX/G13 RX,`chain_bus_request`/`chain_bus_sniff`),协议从官方库逐字节核实;⚠️ Core2 直连 Chain host 官方未背书,不通时 `chain_bus_sniff` 抓原始字节自诊(详见 `apps/chain_lab/README.md`)。
- **launcher 图标**:每加一个游戏在 launcher 加专属图标分支,**要重刷 launcher 才显示**(不刷也能玩、显示通用笑脸)。

---

## 11. 必查 MCP 清单 + 坑位速查

### 11.1 必查 MCP(`AGENTS.md` §1 强制;每次查后在对话里写 "Confirmed via …")

实现前/中**先查 MCP 再写**,至少覆盖:
- **MPU6886**:寄存器图(WHO_AM_I / 电源管理 / ACCEL_CONFIG / 数据寄存器 / 量程)。查 datasheet,**勿照搬 BMI270**。
- **BSP `espressif/m5stack_core_2`**:`bsp_display_start*` / `bsp_display_lock/unlock` / `bsp_display_brightness_set` / `bsp_i2c_get_handle` / `bsp_audio_codec_speaker_init` / `bsp_feature_enable(BSP_FEATURE_VIBRATION)` 的签名与 Kconfig(`CONFIG_BSP_PMU_AXP192`)。
- **esp_codec_dev**:`esp_codec_dev_open/write/close/set_out_vol` 的格式结构体与生命周期。
- **LVGL**(随 BSP 版本):canvas / `lv_anim` / 坐标更新 / 脏矩形;**确认部分缓冲(BSP 默认 `H_RES*50`)、未开 `full_refresh`**;SPI 像素时钟 40MHz 与 flush DMA 异步——决定 §6 帧预算是否成立。
- **led_strip**:RMT 后端在经典 ESP32 上的配置(SK6812/WS2812 时序、灯数=10、G25)。
- **AXP192**(绕过 BSP 直控 LDO3/DCDC3/EXTEN 时):对应寄存器/通道。
- **外接单元**:MCP 缺板级细节时,退到厂商 GitHub raw(如 Chain 帧格式取自 `m5stack/M5Chain` 源码)。

> 已核实记录(供人工核对):**Confirmed via esp-component-registry** — `espressif/m5stack_core_2` v3.0.1(target esp32,V1.0 用
> `CONFIG_BSP_PMU_AXP192`,`BSP_CAPS_IMU=0`,`BSP_I2C` SDA=21/SCL=22,含 `BSP_FEATURE_VIBRATION`);`espressif/bh1750` v2.0.0
> 连续模式;`jbrilha/esp_lcd_ili9342` 备选。**Confirmed via espressif-docs** — esp_lcd SPI panel 以 `esp_lcd_panel_draw_bitmap` 做局部刷新。

### 11.2 坑位速查(开工前再扫一眼)

- **AXP192 不初始化 = 屏黑/无声/无触摸**,别误判硬件坏(BSP 已处理,但 Kconfig 选错 PMU 版本会中招)。
- **IMU 是 MPU6886 不是 BMI270**,寄存器与轴向**都别照搬 BMI270**;轴↔屏映射实测标定。
- **音频脚 G0/G2/G12 是 strapping**、已被板载占用,正常别动;喇叭别反复 toggle SPK_EN(咔哒)。
- **经典 ESP32 PSRAM 直映射 ≤4MB**,大缓冲按 4MB 算。**电池仅 Bottom2 500mAh**,务必做 idle 省电(§7)。
- **改 sdkconfig.defaults 后删 sdkconfig 再 fullclean。**
- **底座必装**:IMU(0x68)与电池都来自 Bottom2;灯带 G25 常驻,G25 不能再做别的。开机先确认底座在位、I2C 扫得到 0x68。
- 🔴 **灯带/PORT 单元全黑或无响应先查 EXTEN**:吃 M-Bus 5V(SY7088 升压,AXP192 EXTEN 使能),**BSP 从不开**;数据线正常翻转、`refresh` 返回 OK 也可能全黑。必须自己开 EXTEN(`core2_power`、§7)。**别误判成硬件坏**。
- 🔴 **`brightness_set(0)` 不熄屏**:背光=AXP192 DCDC3,0% 仍 ~2.95V。深度省电真黑屏必须断 DCDC3 使能(REG 0x12 bit1,`core2_power_backlight()`,§7)。
- **打盹判据只看机身动作(IMU),别看游戏对象速度**(残余倾斜下对象会永远慢爬 → 永不打盹);唤醒连续多帧去抖防单帧噪声误唤醒(§7)。
- 🔴 **MCU 固件单元用 repeated-start 组合读会钳死总线**:读拆两笔事务(§10、`docs/units/_MCU_Firmware_I2C_Units.md`)。
- **渲染红线:永不每帧整屏重绘**(§6)。静态层进关画一次、关内只刷脏矩形;扁平色优先(RGB565 banding);发光/脉动烘进静态层别每帧 alpha。
- **关卡/内容手工编排 + 加载时校验**(如 BFS 可解性),别用纯随机(tilt_maze 见 `apps/tilt_maze/SPEC.md` §4.1/§19)。

---

## 12. 各 App 竣工索引

各 app 的 **as-built(定案数值 / 落地差异 / 待实机 / 特有踩坑)在各自 README**,玩法规格在 SPEC(目前仅 tilt_maze):

| App | 槽 | 外设 | 状态 | 竣工记录 |
|---|---|---|---|---|
| **tilt_maze** 倾斜迷宫 | ota_0 | IMU MPU6886 | ✅ 实机验证(M0–M5+打盹) | `apps/tilt_maze/README.md`(规格 `SPEC.md`) |
| **busy_knobs** 旋钮忙碌台 | ota_1 | 8Encoder | ✅ 实机验收通过 | `apps/busy_knobs/README.md` |
| **peekaboo** 躲猫猫昼夜屋 | ota_2 | DLight | 🔄 v1 实机试玩偏单调 → v2「夜里来客」重写(见 SPEC) | `apps/peekaboo/SPEC.md` + `README.md` |
| **feed_monster** 喂怪兽 | ota_3 | 超声波 | ⏳ build 通过,待烧录 | `apps/feed_monster/README.md` |
| **chain_lab** 抓娃娃机 | ota_4 | Chain Enc/Joy(UART) | ✅ v2.1 分层实机验证 → 🔄 v2.2 趣味批(玩偶造型+金星彩蛋)⏳ 待烧录 | `apps/chain_lab/SPEC.md` + `README.md` |
| **magic_wand** 魔法萤火虫 | ota_5 | Gesture(PAJ7620U2)+ RGB(P4) | ⏸️ 暂停:v1 九法术否决 → v2 光标跟手实机否决(占空比 50%)→ v2.1 在场+手势重写,P1 已烧录、在场信号标定通过,但整体体感欠佳(2026-07-11),暂不推进;P2–P4 未做 | `apps/magic_wand/SPEC.md` + `README.md` |
| **launcher** 卡带机选择页 | factory | — | ⏳ build 通过,待实机 | `launcher/README.md` |

> 平台层跨应用踩坑(EXTEN/DCDC3/repeated-start/桌面省电)已归入 §7 / §10 / §11;各 app README 里那些坑的**具体现场**保留作案例。历史演进(关卡从 4→…→16×12、8Encoder 排障、多 App 分区改造等)见 git log 与各 README。
