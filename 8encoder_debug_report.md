# 8Encoder 单元调试报告(2026-07-03)—— 供外部评审

> 目的:第二个游戏《旋钮忙碌台》(busy_knobs)接入 M5Stack Unit 8Encoder(U153)时,
> 单元始终无法通信。经 5 轮"改固件→用户烧录→回传日志"迭代排查,最终结论:**单元硬件
> DOA(到货即坏),建议换货**。本文完整记录症状、推理链、在线核实的事实、每一轮的
> 假设修正、以及尚存的不确定性,供另一个 AI 独立评审"结论是否可靠、有无遗漏的软件侧嫌疑"。
>
> ⚠️ **2026-07-05 最终定谳:本报告 §3 的 DOA 结论和 §7 的"固件死机"结论都错了。
> 单元无罪,根因是本仓库驱动用 repeated-start 组合读,而该单元固件只支持
> "写寄存器号+STOP 再读"。一行修复,实机验证通过。§1–§7 保留作过程记录,
> 连环误判的检讨见 §8。**

---

## 1. 环境与背景

- 主机:M5Stack **Core2 v1.0**(经典 ESP32,IDF v6.0)+ **M5GO Bottom2** 底座(必装,
  提供电池/IMU/灯带)。多 App 架构:factory=launcher,游戏单刷各 ota 槽。
- 被测单元:**Unit 8Encoder(U153)**,全新原厂到货。从机 MCU=STM32F030C8T6,I2C 默认
  地址 0x41,5V 供电(内部 DC-DC 降 3.3V),8 旋钮+8 按键+1 拨动开关+9 RGB。
- 接线:Core2 机身侧面红色 **PORT.A**(G32=SDA / G33=SCL,I2C_NUM_0;内部总线
  AXP192/触摸/IMU 占 I2C_NUM_1)。PORT.A 的 5V 来自 M-Bus 5V(SY7088 升压,
  AXP192 EXTEN 使能;本固件开机即开)。
- 驱动:自写 `components/units/unit_8encoder`,与官方 Arduino 库同粒度(一个值一次
  I2C 事务),100kHz,读 0xFE 固件版本作在位检查。
- 关键约束:开发机是 WSL 无法烧录,每轮迭代靠用户手动烧录+回传串口日志,因此排查
  策略偏向"一次固件搭载尽量多的自诊断"。

## 2. 症状时间线与每轮推理

### 第 1 轮:初始症状

```
W i2c.common: GPIO 32 is not usable, maybe conflict with others
W i2c.common: GPIO 33 is not usable, maybe conflict with others
E i2c.master: I2C software timeout
W unit_8encoder: 8Encoder 无响应(ESP_ERR_INVALID_RESPONSE)
（每 2s 重试,循环出现）
```

**当时的分析**:
- 查 IDF v6 `i2c_master.c` 源码:`ESP_ERR_INVALID_RESPONSE` = **NACK**(i2c_master.c:722
  注释 "NACK is received")→ 初判"总线通但 0x41 无人应答",首要嫌疑**插错口**
  (Bottom2 底座上的黑口 PORT.B/蓝口 PORT.C 不是 I2C;红色 PORT.A 在 Core2 机身侧面)。
- `GPIO 32/33 is not usable` 告警:查源码确认是**误导性症状而非引脚冲突**——经典 ESP32
  无硬件 FSM 复位(`!I2C_LL_SUPPORT_HW_FSM_RST`),事务失败后的总线恢复
  (`s_i2c_hw_fsm_reset`→`s_i2c_master_clear_bus`→`i2c_common_set_pins`)会对**自己已
  reserve 的引脚**再次 `esp_gpio_reserve` 并告警。另 grep 证实 BSP 与 sdkconfig 均不占
  G32/33(无 RTC 外部晶振配置,BSP 无此引脚引用)。
- **应对**:加 `core2_board_port_a_scan()` 全总线扫描自诊断。

### 第 2 轮:扫描全超时 → 修正为"总线被拉死"

```
E i2c.master: probe device timeout. Please check if xfer_timeout_ms and pull-ups are correctly set up
（112 个地址全部如此）
```

**修正推理**:空总线(插错口/没插)在内部上拉(45k,查源码确认驱动
`enable_internal_pullup` 对 G32/33 这类 RTC 脚经 `gpio_pullup_en` 真实生效)下应表现为
**快速 NACK**;全地址 probe 超时 = **SDA/SCL 被物理拽低**。初判嫌疑倒向"单元没供电:
死掉的 3.3V 轨让单元板载上拉变下拉拽死总线"。
**应对**:扫描前先直读线电平,拽死则直接判因并跳过无意义扫描。

### 第 3 轮:线电平确证拽死;用户 A/B 测试洗清主机侧

```
W core2_board: PORT.A 总线被拉死(SDA=0 SCL=0,空闲应=1/1)
```

用户将**超声波单元(RCWL,0x57)插同一口、同一线**:

```
I core2_board: 扫描 PORT.A I2C (G32/G33,线电平 SDA=1 SCL=1)…
I core2_board:   发现 0x57  Ultrasonic (RCWL)
```

**至此被排除的嫌疑**:插错口、线缆、5V 供电路、主机 I2C 配置(引脚/速率/上拉)、
引脚冲突。问题锁定在 8Encoder 单元侧——但单元是全新原厂货,用户合理质疑
"是不是文档/软件还有问题",要求在线核实。

### 第 4 轮:在线核实(文档 vs 官方库 vs 开源内部固件)

出处三级核实(产品页 < Arduino 库 < 内部固件源码):

| 事实 | 核实结果 | 出处 |
|---|---|---|
| 寄存器 0x00/0x20/0x40/0x50/0x60/0x70/0xFE | 本地文档正确 | 库 + 固件源码一致 |
| **改地址寄存器** | 本地文档写 0xF0,**实际 0xFF**(已修正;驱动未用到,无功能影响) | 库 define + 固件 `rx_data[0]==0xFF` 写 flash |
| 按键极性 | **按下=0**(固件原样回传电平、无取反、MCU 内 NOPULL;官方例程 `if(!getButtonStatus)`) | 固件源码 + 官方例程 |
| 上电 LED | **默认全灭**("插上不亮灯"不能证明单元没电/坏) | 固件源码(sk6812_init 后无置色) |
| 总线速率 | 官方默认 100kHz(与自写驱动一致) | 库 begin() 默认参数 |
| **bootloader** | **常驻 I2C 0x54;上电瞬间检测 PB10/PB11(即 I2C 两线),双低→留在引导态不进应用,双高→跳应用** | `bootloader/basex_bootloader/Core/Src/main.c` |

**由 bootloader 事实推出的"陷阱理论"**(当时的主假设):
1. 主机启动顺序是"先开 5V、后建 I2C 总线"→ 单元上电瞬间 G32/33 悬空,可能被判双低
   → 困在引导态 0x54;
2. 困住后被主机每 2s 的 clear-bus 脉冲串(9 个 SCL 脉冲)打搅,STM32 I2C 从机卡死,
   拽住 SDA + 钳住 SCL → 总线双低(与实测一致);
3. **EXTEN 掉电不清零**(AXP192 由电池供着)→ 主机重启/拔 USB 都不给单元断电,
   卡死跨重启存活——解释"怎么重启都一样"。
   (旁证:M5 社区有"8Encoder 出现在 0x54"的悬案帖,与 bootloader 地址吻合。)

**应对(三层防护)**:
- `core2_board_init` 在开 5V **之前**预上拉 G32/33(让单元"睁眼"见双高);
- 新增 `core2_board_port_a_recover()`:软件切 M-Bus 5V 400ms 给单元真断电重启;
- 扫描认得 0x54 并明示"困在 bootloader"。

### 第 4.5 轮:恢复 v1 的缺陷(自己发现自己)

烧录后日志:断电重启执行了,总线**确实活了**(复电后 2ms 内快速 NACK,不再是 50ms
超时),但 0x41 仍无人。**根因是恢复函数自身**:切 5V 前主机 I2C 控制器 FSM 还卡在
失败事务里、开漏输出拽着两线(仅加 `gpio_pullup_en` 拗不过开漏低),单元复电"睁眼"
又见双低 → 又进 bootloader。
**修正(v2)**:断电窗口内先 `i2c_master_bus_reset()` 释放主机侧两线(此刻单元没电,
清总线脉冲无害),再复电;恢复失败后自动再扫一次让真凶(0x54?)现形。

### 第 5 轮(终局):恢复 v2 完美执行,单元在任何地址都不存在

```
W core2_board: PORT.A 总线被拉死(SDA=0 SCL=0,空闲应=1/1)…
W core2_board: PORT.A 单元断电重启:切 M-Bus 5V 一个来回…
I core2_board: 复电前线平 SDA=1 SCL=1(主机侧已释放)        ← 关键①
I core2_power: 已开启 M-Bus 5V(EXTEN)
W unit_8encoder: 8Encoder 无响应(ESP_ERR_INVALID_RESPONSE)   ← 复电150ms:快速NACK
I core2_board: 扫描 PORT.A I2C (G32/G33,线电平 SDA=1 SCL=1)…
W core2_board:   PORT.A 线电平正常但无器件应答(空总线)        ← 关键②:0x54也没有
…(约 2s 后)
E i2c.master: I2C hardware timeout detected                    ← 关键③:总线重新被拽死
```

**三个关键观测**:
1. **复电前(单元断电时)线平 1/1**:主机侧释放成功;且说明单元断电时不拽线。
2. **复电 150ms 后全总线扫描干净**(112 地址 31ms 全快速 NACK):总线健康、上拉正常,
   但单元在**任何地址都不存在——连 bootloader 的 0x54 都没有**。STM32 毫秒级即应启动,
   bootloader 或应用至少该有一个应答。都没有 = **STM32 根本没跑起来**。
3. **约 2s 后总线重新拽死**(先"hardware timeout"=事务中 SCL 被按住,后全拽死):
   在总线无任何主机活动的空闲期内,线被外部渐进式拽低。

## 3. 最终结论与解释模型

**结论:单元 5V→3.3V 电路故障(DC-DC 不启动或 3.3V 轨短路),STM32 从未运行——
硬件 DOA,建议换货。**

解释模型(拟合全部 5 轮观测):
- 单元 3.3V 轨因故障从 0 缓慢爬升却永远到不了 STM32 的 POR 阈值;
- **轨 ≈ 0V 阶段**(刚复电):引脚保护电路不导通 → 线干净(1/1、快速 NACK、扫描空);
- **轨爬到 1~2V 的中间区**(复电约 1-2s 后):STM32 半供电,I2C 引脚经保护结构/半开
  的驱动管开始下拉 → 线被渐进拽死(先 stretch 样的 hardware timeout,后双低);
- STM32 永不启动 → 任何地址(0x41 应用 / 0x54 bootloader)永远无应答;
- 主机侧一切正常由超声波 A/B 实测反证(同口、同线、同固件路径,即插即见 0x57)。

**已被证据排除的备选解释**:
| 备选假设 | 排除依据 |
|---|---|
| 插错口(底座黑/蓝口) | 超声波同口即通;且拽死≠空总线特征 |
| 线缆断/短 | 超声波同一根线即通 |
| 5V/EXTEN 没开 | 日志确认 EXTEN 已开;同轨灯带工作;超声波(也吃5V)工作 |
| 主机引脚冲突(GPIO 32/33 告警) | 源码确认是总线恢复的自我重复告警;BSP/sdkconfig 不占该脚 |
| 驱动配置错(速率/上拉/极性) | 100kHz=官方默认;上拉经源码+线平实测生效;超声波经同一条 code path 工作 |
| 单元地址被改(0xFF) | 全地址扫描空 |
| 困在 bootloader(0x54) | 恢复 v2 保证复电见双高后,0x54 仍无应答 |
| 主机 FSM 拽线(恢复 v1 缺陷) | v2 已修,复电前实测 1/1 |

## 4. 尚存的不确定性(评审重点)

1. **没有万用表实测**单元 3.3V 轨与 5V 入口——"轨缓慢爬升"是从三个时间窗的线平
   行为**推断**的,不是直接测量。
2. **"约 2s 后重新拽死"的时间常数**仅一次观测(第 5 轮日志),没有多次复现统计;
   第 4.5 轮复电后的短窗口内没等到再拽死就被下一轮事务打断,不构成矛盾但也非重复验证。
3. **单元是否有板载 I2C 上拉未确证**(原理图 PDF 抓取失败)。"断电时线平 1/1"与
   "板载上拉挂死轨会拽低"的经典模型有张力——若单元**有**板载上拉,断电时应拽低。
   自洽的解释是:上拉挂的 3.3V 轨经故障点(短路/死 DC-DC)对地阻抗不足以在轨=0 时
   显著分压(45k 主机上拉 vs 单元"4.7k+故障阻抗"串联),或单元根本没有板载上拉。
   **此点是模型里最弱的一环**,但不影响"STM32 从未应答任何地址"这一决定性事实。
4. 单元侧 HY2.0 插座**虚焊**(5V 脚)可产生同样表现——仍是单元侧缺陷、同样走换货,
   但严格说与"DC-DC 坏"不是同一故障点。已建议用户做"捏/摇线头看是否自动接管"的
   末位测试(固件每 30s 自愈重试,虚焊瞬间接通会自动"你好")。
5. 理论上未 100% 排除:8Encoder 的**浪涌/启动电流**与 SY7088 升压的交互问题
  (超声波功耗更小)。反证是灯带(同轨,10×SK6812)未见异常、且单元静态电流很小;
   若换货后新单元同症状,此项升为首要嫌疑。

## 5. 本轮排查沉淀到代码库的产物(全部 build 通过,未提交)

- `components/core2_board`:`core2_board_port_a()`(PORT.A 总线懒加载,I2C_NUM_0)、
  `core2_board_port_a_scan()`(线电平判决+全总线扫描,认得 0x54)、
  `core2_board_port_a_stuck()`、`core2_board_port_a_recover()`(断电重启 v2:
  断电窗口内 `i2c_master_bus_reset()` 释放主机两线)、init 开 5V 前预上拉 G32/33。
- `components/units/unit_8encoder`:完整驱动(增量/按键/开关/RGB/计数/固件版本),
  错误信息按 NACK/超时分诊。
- `apps/busy_knobs`:完整游戏(8 音柱+五声音阶+庆祝+昼夜+两级省电+热插拔自愈)。
- `docs/units/Unit_8Encoder.md`:0xFF 修正、bootloader 0x54 陷阱、按键极性、
  上电灯全灭、EXTEN 掉电不清零、0x58~0x62 补充寄存器、排查签名速查。
- 根 `CLAUDE.md` §20.14:as-built 全记录。

## 6. 给评审 AI 的问题

1. 第 3 节的解释模型是否有更简洁的替代解释未被排除?
2. 第 4 节不确定性 3(断电时线平 1/1 vs 板载上拉模型)是否动摇 DOA 结论?
3. 在"无烧录条件、每轮迭代成本高"的约束下,是否还有一次固件迭代能获得的
   决定性证据被遗漏?(已有:线电平、全地址扫描、断电重启、复电时序观测)
4. 换货后若新单元同症状,排查应从哪里重启?(我方预案:SY7088 浪涌交互 → 用
   USB 直供 5V 的对照实验;以及量单元 3V3。)

## 7. 终审(2026-07-05):结论修正——单元确实是坏的,但坏法不同

本报告 §3 的 DOA 模型(5V→3.3V 失效、STM32 从未运行)被两个新证据推翻并改判:

1. **ESP32-S3 + IDF i2c_tools 独立复测**(不同主机、不同 I2C IP、同一套 IDF 驱动):
   - `i2cdetect` **能看到 0x41**(→ §2 第 5 轮"任何地址都无应答"的观测未能复现);
   - `i2cget -c 0x41 -r 0xfe -l 1` → `I2C transaction timeout`;
   - 读失败后再 `i2cdetect` → 全总线 probe timeout,**断电才恢复**——与 Core2 上的
     症状序列完全一致。
2. **单元原理图(SCH_UNIT_8Encoder_V1.01)**:SDA/SCL 板载 **4.7k 上拉**(R3/R4 →
   单元 3V3);5V→3.3V 是 **BL8075 LDO**(非 DC-DC)。§4 不确定性 3 就此关闭;
   一切"弱上拉/信号质量/升压交互"类主机侧假设出局。

**终审判决:单元故障,故障层是"应用固件死机",不是供电电路。**
机理:STM32 的 I2C 外设对**地址 ACK 是硬件自动完成的**,不需要固件参与;**数据相
必须固件服务,否则外设无限拉伸 SCL**。该单元地址应答正常、任何数据相把总线挂死 →
固件跑完 I2C 初始化(外设已配成 0x41)后内核死掉(死循环/hardfault/flash 损坏)。
此模型拟合 Core2 与 S3 的全部观测;主机侧(经典 ESP32 FSM、上拉、SY7088、本仓库
软件)全部洗清。

中间过程补记(2026-07-03 深夜诊断会话,全文见 debug2.txt,代码改动已全部丢弃):
撤掉扫描风暴的"温柔模式"与 `scl_wait_us=12ms` 均无效;**pristine 线电平**(未建
总线、未通信、仅内部上拉)**双高**——证明历轮"总线被拽死"都是失败事务的**后果**
而非原因;位带 v1 因时序缺陷(半周期过短、无 SDA 等高)未构成有效测试。

遗留小疑点(不影响判决):§2 第 5 轮"复电 150ms 全地址扫描干净(0x41 也无)"与
S3 上 detect 稳定可见 0x41 不一致,可能与固件死机时刻的随机性有关,未再深究。

**判决的第三块基石(2026-07-05,读内部固件应用源码后补)**:应用固件主循环里
**自带 I2C 卡死看门狗**(`trans_state` + 505ms 超时 → 重新 EnableListen + 强制
NACK 自恢复)。若内核活着,任何一次挂死事务都会在 ~1s 内自愈——实测该单元挂死后
**永不恢复,断电才解**,证明主循环已死,不是 I2C 状态机的暂时性卡顿。结合
bootloader `Jump_APP` **只校验应用首字**(初始 SP)就跳转:一个**出厂烧写不完整/
flash 部分损坏**的应用镜像能通过校验并启动,跑到坏区即 hardfault——与"地址 ACK
正常(I2C 初始化已完成)、数据相永不服务、就地灯全灭"完全吻合,是目前最自洽的
出厂缺陷模型。

**处置**:换货(已在途)。坏件复活有两条路:①**I2C IAP 重刷,不需要 ST-Link**——
bootloader 刷写协议已从源码完整还原(强制进入 0x54 + 逐页 `0x06` 写 + `0x77` 跳转,
官方 64KB 成品 hex 在固件仓库 `firmware/` 下),协议细节与地址保护红线见
`docs/units/Unit_8Encoder.md` §5.1.2,Core2 自己就能当刷机器;②板上 SWD 座
(J2:SWCLK/SWDIO/NRST)+ ST-Link 刷完整 hex(连 bootloader 一起恢复,更彻底)。

**沉淀的纪律**:①单元在位检查**不得只用地址 probe,必须做一次寄存器读**(地址 ACK
是外设硬件行为,证明不了固件活着;本驱动读 0xFE 正是为此)。②跨主机 A/B 测试要
对齐到**同一种事务**——"detect 得到"和"读得到"是两回事,本案两次误判(DOA、
主机侧 FSM)都源于把前者当成了后者。

## 8. 最终定谳与检讨(2026-07-05):单元无罪,是我的驱动读法错了

### 8.1 决定性对照(用户完成)

用户给 Core2 刷 **UIFlow2** 固件、照官方示例写程序:同一台 Core2、同一个 PORT.A
(G32/33)、同样 100kHz、同样 0x41——**8Encoder 完全正常**(读计数/按键/开关、写 LED)。
主机、线、口、供电、单元瞬间全部无罪,唯一剩下的变量是**事务的组织方式**。

### 8.2 真·根因(内部固件源码逐行定位)

- 从机固件里,解析寄存器号并准备回读数据的 `i2c2_receive_callback` **只在
  `HAL_I2C_ListenCpltCallback`(= 检测到 STOP)** 或收满 50 字节时被调用
  (`i2c_ex.c`);1 字节的寄存器号写入永远靠 STOP 触发解析。
- 本仓库驱动 `reg_read` 用的 `i2c_master_transmit_receive` 是 **repeated-start**
  组合读(写和读之间没有 STOP)→ 读方向 ADDR 到来时数据未备好,AddrCallback 拿着
  **从未初始化的 `tx_len=0`** 调 `HAL_I2C_Slave_Transmit_IT` → HAL 拒绝、TXDR 永远
  没数据 → 外设(时钟拉伸使能)**无限拉住 SCL,总线钳死**;固件的 505ms"自恢复"只补
  EnableListen+NACK,解不开发送态拉伸 → **断电才恢复**。
- 官方 Arduino 库(`endTransmission()` 带 STOP 再 `requestFrom`)和 UIFlow 库都是
  "写完 STOP 再读",所以官方生态从不踩雷。IDF i2c_tools 的 `i2cget` 与我们同用
  repeated-start,所以 **S3 上复现了同样的挂死——它不是"单元坏"的证据,而是同一把
  错钥匙在第二把锁上又试了一次**。
- 连"复电 150ms 全地址扫描连 0x41 都没有"(§2 第 5 轮)也解了:bootloader 上电固定
  等 **300ms** 才跳应用,150ms 时谁都没上线。至此**全部历史观测无一例外归因此一行**。

**修复**:`unit_8encoder.c reg_read()` 拆成两笔事务(`i2c_master_transmit` 写寄存器号
+STOP,再 `i2c_master_receive`)。2026-07-05 实机验证:`8Encoder 就绪 @0x41(FW v1)`,
游戏即插即用。**换货无必要。**

### 8.3 检讨:为什么五轮排查 + 两次"定谳"都没抓到一行代码的错

1. **契约核实不完整(根因的根因)**。写驱动时只核实了寄存器表,没有核实官方库的
   **事务组织方式**。文档里写着"与官方 Arduino 库同粒度(一个值一次事务)"——粒度
   抄对了,**框架抄错了**:官方是"写+STOP+读",我用了 repeated-start。对 MCU 固件
   从机,线上每笔事务的形状(START/Sr/STOP)与寄存器表同等地位,都是契约。
2. **用硬件叙事解释确定性的软件签名**。故障永远死在**同一笔事务**(第一笔读)上,
   100% 确定性——这是软件问题的典型形状。五轮排查却全部在电气层打转(插口/线缆/
   上拉/EXTEN/SY7088/DOA),没有一轮把"我们发的事务序列"本身当嫌疑人。
3. **每个 A/B 实验都没控制真正的变量**。超声波能通(不同设备、恰好不吃这套读法)→
   误判"主机洗清";S3 `i2cdetect` 见 0x41(probe 不是读)→ 误判"单元健康";S3
   `i2cget` 挂死(同样的 repeated-start)→ 误判"单元坏"。唯一真正改变了"事务框架"
   这个变量的实验是 UIFlow——**而它是用户自己做的**。
4. **拿着源码问错了问题**。2026-07-05 上午我已经通读了 `i2c_ex.c` 的 AddrCallback,
   `HAL_I2C_Slave_Transmit_IT(hi2c, tx_buffer, tx_len)` 就在眼前,却带着"证明固件
   死机"的预设去读,没有问"tx_len 什么时候被填、repeated-start 下会怎样"。答案在
   手里,被翻过去了。
5. **层层继承上一轮的框架,从不归零**。每轮会话都在前一轮结论上加码(总线拽死 →
   DOA → 固件死机),包括我用"自带看门狗没生效 → 主循环必死"做推断——但没验证
   那个看门狗对发送态拉伸是否有效(无效),推断链条断在自己没查的一环上。
6. **结论说得太满**。"定谳""洗清""基石"这类词被我用在两轮错误结论上。证据只支持
   "在已尝试的读法下单元不应答",我却说成了"单元坏了"。

### 8.4 落地的防再犯规则

- ✅ 已落地:`reg_read` 修复 + 驱动源码红线注释 + `docs/units/Unit_8Encoder.md`
  §5.1.1 改写为"读协议红线"。
- 行为规则(适用于所有 MCU 固件从机的 Unit 外设):
  1. **写驱动前,把官方宿主库每笔事务的线上形状(START/Sr/STOP、长度)核实并写进
     Unit 文档**,再动手;"寄存器表对了"不等于"契约对了"。
  2. **通信排障第一实验 = 把自己的事务序列换成官方库的事务序列**——成本最低、
     变量最准,必须排在一切硬件假设(供电/上拉/换货)之前。
  3. 每次死在同一笔事务 = 软件形状的签名,禁止直接跳硬件叙事。
  4. 做 A/B 前先写下"本实验控制的变量是什么";"换了个设备能通"≠"主机无罪"。
  5. **下硬件结论(坏件/换货)之前,必须有"官方参考实现在同一硬件上也失败"的
     证据**——本案里 UIFlow 一测就翻案,正说明此前从未满足过这条标准。

## 附:原始资料索引

- IDF v6.0 `esp_driver_i2c/i2c_master.c`(NACK=INVALID_RESPONSE@722、FSM 复位、
  clear_bus 重配引脚)、`i2c_common.c`(reserve 告警@327/376、gpio_pullup_en 生效路径)
- [Unit 8Encoder 产品文档](https://docs.m5stack.com/en/products/sku/U153)
- [M5Unit-8Encoder 官方 Arduino 库](https://github.com/m5stack/M5Unit-8Encoder)
- [M5Unit-8Encoder-Internal-FW 内部固件源码](https://github.com/m5stack/M5Unit-8Encoder-Internal-FW)
  (应用 `8encoder/Core/Src/main.c`;bootloader `bootloader/basex_bootloader/Core/Src/main.c`)
- [社区 0x54 案例](https://community.m5stack.com/topic/5847/strange-issue-using-8encoder-with-atoms3-and-tailbat)
- 本仓库:`docs/units/Unit_8Encoder.md`、`components/core2_board/core2_board.c`、
  `apps/busy_knobs/main/knobs_game.c`、`CLAUDE.md` §20.14
