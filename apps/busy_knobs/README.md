# busy_knobs — 旋钮忙碌台(as-built)

> 本文件是 **busy_knobs 应用的竣工记录**(原单-app CLAUDE.md §20.14–20.15,2026-07 迁出)。
> 跨应用平台事实(单元接入/桌面玩法省电坑/MCU 固件 I2C 读协议)见根平台手册 `CLAUDE.md`(§10 做新 App 指南、§7 电源·休眠、§11 坑位)。
> **占 ota_1(0x390000)**,外设 = Unit 8Encoder(PORT.A @0x41,硬件事实见 `docs/units/Unit_8Encoder.md`)。
> 烧录:`tools/flash_one.sh busy_knobs`(= esptool write-flash 0x390000,launcher/其它槽不动)。

## 玩法(电子忙碌板,零失败)

**8 个旋钮 ↔ 屏上 8 根彩虹音柱**:
- **转** = 柱子升降(7px/档,0~24 档)+ C 大调五声音阶"叮"(音高=高度,怎么乱转都和谐)+ 旋钮就地 RGB 随档位变亮;
- **按** = 柱顶小脸唱歌弹跳 + 闪白 + 轻震;
- **8 柱全拉满 = 庆祝彩蛋**(SND_WIN + 三连震 + 底座灯彩虹 + 音柱波浪弹跳 + 旋钮灯跑马,然后缓缓落回 0 重玩);
- **拨动开关** = 白天/黑夜换景(天色 + 太阳↔月亮 + 星星)。

### 趣味增量(2026-07-06 实机验收通过,只动 `main/knobs_game.c` + `tuning.h`,不碰组件)

- **图案彩蛋(不止"全满")**:每次转旋钮改档后 `detect_pattern()` 扫 8 柱队形——上/下楼梯(严格增/减)、一条线(全等高且 >0)、小山/山谷(单峰/谷、不在两端)——命中即小庆祝 `pattern_reward()`:按柱高左→右弹 8 音(一次 `audio_fx_play_notes`,<400ms,天然"听出形状")+ 8 柱错峰波浪小跳 + 底座灯 `LED_FX_COLLECT` 扫圈 + 轻震。**不改档位、不重置**;`s_last_pattern` 记忆 → 同图案只贺一次。"全 8 满"仍是最高优先级大庆祝(`enter_win`,命中即 return)。
- **摇一摇 → 洗成新队形(用上平时只做休眠检测的 IMU)**:`game_task` 每帧读加速度算帧间三轴变化 `d`;`d > SHAKE_THRESH` 带**泄漏计数**(低于阈值就 −1),攒够 `SHAKE_NEEDED` 且不在冷却 + 清醒 + ST_PLAY + 单元在位 → `trigger_shuffle()`:8 柱洗成上/下楼梯或小山 + 滑音 + 中震 + 底座彩虹 + 就地灯刷新。泄漏 + 高阈值(桌面转旋钮 `d` 远达不到 1.2g)防单次磕碰/放桌误触。

### 趣味增量第二批(2026-07-08 实施,`FUN2_SPEC.md`;build 通过,⏳ 待实机验收)

只动 `main/knobs_game.c` + `tuning.h` + `components/ledstrip_fx`(仅尾部追加 5 个新枚举 + 对应渲染分支,既有 3 特效/4 基础模式字节未动,`idf.py -C apps/tilt_maze build` 已验证不破坏其它 app)。

- **柱顶小脸活化**:全局 `face_tick()`(仅 ST_PLAY 跑)驱动——①随机眨眼(2~6s 间隔,合眼 ~100ms,20% 概率双眨,眼白子对象靠 LVGL 默认裁剪自动挡住瞳孔,无需手动 HIDDEN);②看向:转哪个旋钮全排瞳孔偏向它,1s 无操作回中,只在目标变化/超时两个时机批量重对齐一次(不逐帧对齐 16 颗瞳孔);③嘴随高度三档(平嘴 <9 档、微笑 9~17 档、张嘴笑 ≥18 档,深一号橙);④到顶(=24 档)鼓腮,跌下即收。嘴型/鼓腮只在跨阈值时改 LVGL 对象,`trigger_shuffle`/WIN 收场落回 0/`unit_attach` 重接管这几个绕开 `apply_rotation` 直接改 `s_level` 的地方都补了 `faces_refresh_all()` 全量刷新。
- **小鸟访客**:状态机 `bird_tick()`(ABSENT→FLY_IN→PERCHED→FLY_OUT,或图案命中时插入 RIDE)。自发拜访间隔 20~45s 随机,飞向当前最高柱(并列取最左);栖息 8~15s,期间所栖柱档位变化会受惊小跳 + 短啾;摇一摇/全满庆祝/拔线都会把它吓飞或立即收场;进 NAP/DEEP 瞬时隐藏不放动画,唤醒后拜访计时重新随机。移动全走 `lv_anim`(x/y 各一条独立 exec 回调),完成判定用 game_task 侧帧倒计时而非 anim ready 回调,避免跨任务碰状态;鸟的逻辑坐标存在影子变量 `s_bird_x/y` 里,不反查 LVGL 对象。
- **图案彩蛋按形状差异化**:五种图案各自一套音/跳方向/灯效/震动组合(上楼梯=正向 arp+`WAVE_L2R`+`LED_FX_SWEEP_L2R`;下楼梯=反向取音但听感仍上行+`WAVE_R2L`+`LED_FX_SWEEP_R2L`;一条线=齐唱长音+`WAVE_ALL`+`LED_FX_FLASH`+`HAPTIC_BUMP_MED`;山=`WAVE_IN`+`LED_FX_GATHER`;谷=`WAVE_OUT`+`LED_FX_SPREAD`),命中时按規則触发小鸟 RIDE(鸟在场则从当前栖息柱直接骑向终点柱,不在场则先快速飞入起点柱再骑;RIDE 进行中忽略新命中)。`ledstrip_fx` 新增的 5 个特效尾部追加到 `led_fx_t`,沿用 `LED_FX_COLLECT` 的索引序(0..9 直接当"环序"用,不做物理左右重映射)。
- **夜晚声音世界 + 星星微闪**:`level_hz()` 按 `s_night` 切换白天/夜晚两张等长五声音阶表(夜表整体下移纯四度,不是降八度——NS4168 小喇叭低频还原差);转动"叮"/按键"唱歌"/图案 arp/小鸟落地音四处响度和时长各自有夜值,集中在 `fx_tick_ms/amp()` 等几个内联小工具里取,不在各处写三目。3 颗星星仅夜晚+清醒时独立随机切换亮暗两种尺寸(6×6↔4×4,纯色块换尺寸+挪 1px 保持视觉中心,无 alpha),`scene_apply()` 每次都会把星星复位为亮态(不论切哪个方向),确保下次入夜从亮态起跳。
- **多键齐按和弦彩蛋**:按键改边沿检测后不立即 `sing()`,先记 pending 并开一个 `CHORD_WINDOW_FRAMES`(2 帧≈66ms,仍 <100ms 红线)收集窗口,窗口到期按 pending 数量结算——1 个走原单按 `sing()`,≥2 个走 `chord_burst()`(按档位升序组一次快速琶音 + 被按柱一起弹跳 + 一次 `HAPTIC_BUMP_MED` + 底座 `LED_FX_FLASH` + 各柱就地灯闪白)。窗口期若状态跑离 `awake_play`(如全满触发 WIN),直接丢弃 pending 不结算。`sing()` 拆成了纯动画的 `sing_anim()` + 音/震/灯部分,单按路径行为与改前一致。

**待实机点检**(WSL 环境无法烧录,以下需用户在真机上确认,清单见 `FUN2_SPEC.md` §9):眨眼/看向观感、嘴型三档与鼓腮阈值、小鸟拜访节奏与受惊反应、五种图案的方向感是否与直觉一致(尤其 LED 扫向的物理左右对应,`FUN2_SPEC.md` §6 已标注"待实机确认,不符则序表取反,一行改")、夜晚音色是否过闷、和弦窗口手感、`game_task` 栈(现 4096)是否需要按规格提到 4608。确认无误后按 `FUN2_SPEC.md` 开头的收尾说明归档该文件。

## 平台新增(可复用,已沉淀进组件)

- ①`core2_board_port_a()`——PORT.A 外接 I2C 懒加载(G32/G33,**I2C_NUM_0**;内部总线占 I2C_NUM_1=CONFIG_BSP_I2C_NUM);
- ②`components/units/unit_8encoder` 驱动——与官方库同粒度逐值事务,Increment 读后硬件自清零=天然"本帧转动量",init 可重复调用(支持热插拔重试);
- ③`tools/new_app.sh` 脚手架的 EXTRA_COMPONENT_DIRS 增加 `components/units`。

## 省电三个新知识点(桌面玩法特有)

1. 机身不动 ≠ 没人玩,**旋钮活动必须 `core2_sleep_kick`**(否则玩着玩着打盹);
2. 打盹中旋钮活动可唤醒(NAP 时 5V 还在,继续轮询);
3. 深度省电切 5V → **8Encoder 断电复位**(灯灭/计数清零),唤醒后 `unit_attach` 重建(重写就地灯 + 重建开关基线防幻影翻转),深度态只能拿起机身唤醒(单元没电)。

**容错**:开机没插单元 → 无字提示卡(旋钮+插头图)+ 2s 周期重试,插上即"你好"接管;游玩中连续 20 帧 I2C 失败判拔线 → 回提示卡模式。

## 🔴 8Encoder 排障史(重要,踩坑签名要记住)

### 排障终局(2026-07-05 三审定谳:单元无罪,是本仓库驱动的读法错了)

真凶 = `unit_8encoder` 驱动用 `i2c_master_transmit_receive`(**repeated-start 组合读**),而 8Encoder 内部固件只在收到 **STOP** 时才解析寄存器号、准备回读数据——repeated-start 读让从机拿着 `tx_len=0` 进发送态 → 无限拉伸 SCL 钳死总线、断电才恢复。**修复 = `reg_read` 拆成"写寄存器号+STOP,再单独读"两笔事务**(一行级改动),2026-07-05 实机验证即插即用(`8Encoder 就绪 @0x41,FW v1`)。

定谳对照:用户给 Core2 刷 UIFlow2(官方库=写完 STOP 再读)同机同口正常游玩;S3 `i2cget` 挂死只因它同用 repeated-start,不是单元坏。**此前两轮误判(DOA 换货/应用固件死机)全部作废,换货无必要**;读协议红线见 `docs/units/Unit_8Encoder.md` §5.1.1,通用规则见 `docs/units/_MCU_Firmware_I2C_Units.md`。

### 首刷时的两种症状签名(2026-07-03,总线级)

- ①**全地址 probe timeout("probe device timeout...pull-ups")= 总线被拉死**(SDA/SCL 低)——典型是**单元没吃到 5V**:没电的 STM32 单元,板载上拉挂在死 3V3 轨上变"下拉"把线拽低;查 5V(底座灯带同一路,亮=5V 有电)/换 Grove 线/插紧。
- ②**快速 NACK(`ESP_ERR_INVALID_RESPONSE`,i2c_master.c:722)= 总线空但 0x41 没人应答**——插错口(红色 PORT.A 在 **Core2 机身侧面**,Bottom2 黑口 PORT.B/蓝口 PORT.C 不是 I2C)或地址被改过。伴随的 `GPIO 32/33 is not usable` + `I2C software timeout` 连环告警**不是引脚冲突**:经典 ESP32 无硬件 FSM 复位,事务失败后的总线恢复对自己已占引脚重复告警(已核对 BSP/sdkconfig 均不占 G32/33)。
- 自诊断:`core2_board_port_a_scan()` 先读线电平(拉死直接判因,跳过扫描),线平正常才扫全总线;开机失败即跑、之后每 ~30s 一次。

### 8Encoder bootloader 陷阱(2026-07-03 在线核实内部固件源码后定案)

单元内有 I2C bootloader 常驻 **0x54**,上电瞬间 I2C 两线双低 → 困在引导态不进应用(0x41);再被主机 clear-bus 脉冲打搅可从机卡死**把总线双低拽死**。且 **EXTEN 掉电不清零** → 主机重启不给单元断电,卡死跨重启存活。修复:①`core2_board_init` 开 5V 前预上拉 G32/33;②`core2_board_port_a_recover()` 切 5V 断电重启单元——🔴 v2 修正:**断电窗口内必须 `i2c_master_bus_reset()` 释放主机侧 FSM 拽住的两线**,否则单元复电又见双低、再困 0x54;③扫描认得 0x54 并提示。

> 顺带修正 Unit 文档:改地址寄存器是 **0xFF 非 0xF0**、按键**按下=0**、上电 LED 默认全灭、补 0x58~0x62 寄存器(官方例程+固件源码双核实)。

## 待实机标定(tuning.h,一行改一个)

- 旋转方向 `ENC_DIR`、一格计数数 `ENC_COUNTS_PER_LEVEL`、就地灯亮度 `KNOB_LED_MAX=110`(按键极性已核实无需标定);
- 趣味增量第一批:`ARP_MS=42`/`ARP_AMP=55`、`SHAKE_THRESH=1.2`(g,**最可能要标定**:没反应调低、误触调高)、`SHAKE_NEEDED=3`(想更跟手降到 2)、`SHAKE_COOLDOWN_MS=1500`、`SHUFFLE_MS=520`。
- 趣味增量第二批:`BLINK_MIN_S/MAX_S=2~6`、`BLINK_FRAMES=3`、`BLINK_DOUBLE_PCT=20`、`GAZE_HOLD_MS=1000`、`GAZE_DX=2`、`MOUTH_SMILE_LV=9`/`MOUTH_OPEN_LV=18`(嘴型阈值,**较可能要标定**:三档观感不明显就调档位线);`BIRD_VISIT_MIN_S/MAX_S=20~45`、`BIRD_PERCH_MIN_S/MAX_S=8~15`、`BIRD_FLY_MS=700`、`BIRD_HOP_MS=110`、`BIRD_NOTE_MS/AMP=45/45`、`BIRD_NOTE_AMP_NIGHT=32`;`WAVE_STEP_MS=45`、`EQUAL_NOTE_MS/AMP=280/60`、`ARP_AMP_NIGHT=40`;`TICK_MS_NIGHT/AMP_NIGHT=60/30`、`SING_AMP_NIGHT=55`、`TWINKLE_MIN_F/MAX_F=24~60`(**较可能要标定**:星星切换太突兀就调窄区间或加中间态);`CHORD_WINDOW_FRAMES=2`(**最可能要标定**:齐按不跟手就得权衡窗口↔灵敏度)、`CHORD_NOTE_MS/AMP=35/60`。
- `ledstrip_fx` 新增 5 特效的扫向物理左右对应关系待实机确认(不符则 `ledstrip_fx.c` 里的索引序表取反,一行改,见 `FUN2_SPEC.md` §6)。

## launcher 图标

launcher 加 busy_knobs 专属图标分支(三旋钮+音柱,**要重刷 launcher 才显示**;不刷也能玩、显示通用笑脸)。

## 状态

✅ **2026-07-06 实机验收通过**(忙碌台 + 图案彩蛋 + 摇一摇)。
⏳ 趣味增量第二批(2026-07-08):build 通过(app 0x9aee0,槽内余 60%;`apps/tilt_maze` 联动验证 `ledstrip_fx` 改动无回归),**待实机验收**(WSL 环境无法烧录)。
