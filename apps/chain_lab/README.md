# chain_lab — 抓娃娃机(as-built)

> 本文件是 **chain_lab 应用的竣工记录**;玩法规格见 `SPEC.md`(重写规格,完工后保留)。
> 跨应用平台事实(桌面玩法省电坑)见根平台手册 `CLAUDE.md`(§10 做新 App 指南、§7 电源·休眠、§11 坑位)。
> **占 ota_4(0x990000)**。烧录:`tools/flash_one.sh chain_lab`(= esptool write-flash 0x990000)。

产出:在已跑通的 Chain UART 传输层(`chain_bus` + `unit_chain_encoder` + `unit_chain_joystick`)
之上盖了一层「抓娃娃机」——摇杆推吊臂、编码器像摇曲柄一样降爪子、按下抓取、战利品收进展示架、
集齐一批开派对。原本的仪表台(左圆环表盘 + 右光点方框)折叠成隐藏诊断模式,排障能力不丢
(见下「隐藏诊断模式」)。守零失败/即时因果/多通道冗余/渲染红线。

## 协议来源(逐字节核实,协议层本轮零改动)

官方 PDF 只给链接不含正文;帧格式**从官方 Arduino 库 `github m5stack/M5Chain` 源码逐字节核实**(AGENTS.md §1 的"MCP 缺板级细节 → 厂商 GitHub raw"退路)。已回填进 `docs/units/Chain_Encoder.md`/`Chain_Joystick.md` §4。

帧 = `AA55 | lenLo lenHi | id | cmd | data | crc | 55AA`,`len=3+dataLen`(小端)、`crc=(id+cmd+data)&0xFF`、`id`=链上位置(1 起);应答载荷小端。节点主动发心跳(0xFD~1/s)/枚举(0xFC)/按键上报(0xE0),主机轮询按 (id,cmd) 匹配、其余丢弃。

## 平台新增(可复用,本轮未改动)

- ①`components/units/chain_bus` —— Chain UART 传输层:装 UART2(G14 TX/G13 RX)、`chain_bus_request(id,cmd,data..)` 一次请求/应答,通用命令(设备类型/固件版本/RGB/亮度),`chain_bus_sniff(ms)` 抓原始字节做诊断;
- ②`components/units/unit_chain_encoder`(U207,GET_VALUE=0x10 绝对计数 int16 + BUTTON 0xE1);
- ③`components/units/unit_chain_joystick`(U205,GET_16ADC=0x30 原始 ADC + BUTTON 0xE1,应用侧软件归中)。

## 抓娃娃机玩法(as-built)

### 架构:传输层 / 游戏层拆分

- `chain_lab.c` 收窄为纯"传输/绑定层":`scan_bus`/`poll_enc`/`poll_joy`/`node_rgb`/`hue2rgb`/
  `joy_calibrate_center` 原样保留(SPEC §1 红线),`game_task` 20Hz 轮询节奏 + `core2_sleep`
  集成 + 深度省电重扫原样保留。新增一组 getter/RGB 包装(`chain_lab_enc_value()` 等,见
  `chain_lab.h`)把读数暴露给游戏层,游戏层不碰协议细节。
- `crane_game.c/.h` 新增:状态机、吊臂/爪子精灵、战利品坑/展示架、派对编排,只消费
  `chain_lab_*` getter。
- **SPEC §8 文件布局的一处落地差异**:`diag_view.c/.h` 未单独建文件,采用 SPEC §7 明确给出
  的备选做法——诊断台 UI(`ui_create`/`ui_enc`/`ui_joy`/`ui_status`)整段留在 `chain_lab.c`,
  用 `tuning.h` 的 `CHAIN_LAB_DIAG_MODE` 编译期宏包起来,和游戏层调用点(`chain_lab_start`/
  `game_task`)二选一分支。两种模式都已过编译验证(见「状态」)。

### 状态机(SPEC §3 落地)

`PLAY_IDLE ⇄ DESCENDING → GRABBING(~500ms)→ ASCENDING(~750ms)→ DEPOSIT(~500ms)→ PLAY_IDLE`,
展示架集满 5 种 → `PARTY`(~3.5s)→ 重置换新一批 → `PLAY_IDLE`。

- 摇杆 X 直接映射吊臂横坐标,**任何状态下都可移动**(不受深度状态门控,SPEC §3);
- 编码器帧间 delta 只在 `PLAY_IDLE`/`DESCENDING` 驱动深度(顺时针+/逆时针-,到 0 回
  `PLAY_IDLE`);接近底部(`DESCEND_MAX_PX - DESCEND_SNAP_TOL`)自动到底并直接判抓;
- 抓取键在 `PLAY_IDLE`/`DESCENDING` 任意深度都可提前触发抓取(不强制转到底);
- 抓取成功与否只看**横向对齐容差**(`GRAB_ALIGN_TOL_PX`,战利品中心点容差带),与深度无关——
  故意简化,回应 SPEC §11②"编码器精细动作对幼儿可能偏难"的开放风险:曲柄手感留着,
  但不因为没转到底而判失败;
- `GRABBING`/`ASCENDING`/`DEPOSIT`/`PARTY` 期间编码器按键/深度输入被状态门控吃掉(不排队、
  不触发二次动画),摇杆横移与摇一摇彩蛋不受影响。

### 反馈矩阵(SPEC §4 落地要点)

六类事件(移动/下降/抓取/抓中/抓空/集满)×六条通道全部接上;**抓空是"诶?"轻弹一下**,
不掉血、不计失败、零负面反馈(不用红色,用中性琥珀)。摇杆节点 RGB 平时沿用现状 `nx/ny→r/b`
映射,派对期间与编码器节点同步跑彩虹(`hue2rgb`)。

**一处降级(非结构性)**:SPEC §4"下降"行要求底座灯带"暖色微调暗一档",现状 `ledstrip_fx`
没有对应的第三档基础模式;落地时复用了已有的 `LED_BASE_NEAR`("接近目标:加亮向目标色")
代替——语义仍贴合("正在下降接近战利品"),没有为这一处观感新增组件 API。

### 战利品(SPEC §11④ 明确留给下一步创作,本轮先用色块占位)

`PRIZE_TYPES=5`,身份纯靠颜色区分(珊瑚红/天蓝/暖黄/草绿/紫粉),每种一个圆身+小高光点;
未来要做主题造型时照 `apps/peekaboo/SPEC.md` §5.1 的访客表格式(id/主色/造型要点/音签)
逐个设计,替换 `crane_game.c` 里的 `PRIZE_STYLE` 表即可,状态机/坑位/展示架逻辑不用动。

### 电源 / 省电

kick 判据复用 `poll_enc`/`poll_joy` 现有 `activity` 返回值。深度省电唤醒后除了原有的
"断电重扫"分支,新增 `crane_game_reset_position()`:爪子收起、吊臂回中、正在进行的抓取
（若被打断）战利品还给坑不计入收集,并重同步编码器基准(避免节点复位计数清零被误读成
一次大幅转动)。

## 隐藏诊断模式(SPEC §7,已选方案 B:编译期开关)

排障需要现状表盘/光点仪表台时:改 `main/tuning.h` 的 `CHAIN_LAB_DIAG_MODE` 从 `0` 改成
`1`,重新 `idf.py -C apps/chain_lab build` + 单刷,即可整包变回验证台(`chain_bus_sniff`
排障路径不受影响)。改回 `0` 重编重刷即恢复抓娃娃机。方案 A(运行时双键长按切换)按 SPEC
§11③ 建议留作后续体验加分项,本轮未做(避免长按误触风险,详见 CLAUDE.md §8 已知坑)。

## 验证台历史背景(v1,保留)

本 App **头一次接 Chain 系列外设**——不是 Grove I2C 而是 **UART 115200 8N1 菊花链**
(Core2 PORT.C 直接作 Chain host)。v1 是一张纯功能验证台:左圆环表盘(指针角=编码器绝对
计数)+ 中心数值;右方框光点(位置=摇杆归一化 X/Y)+ 原始 ADC 数值;按键 → 光点/指针变绿 +
节点板载 RGB 闪白 + 轻震;平时编码器灯随计数走彩虹、摇杆灯随方位换色——同时验证了读(RX)
与写节点 RGB(TX)两条路,证明 Core2 直连 Chain host 这条路线可行。这套 UI 现在整段保留在
`CHAIN_LAB_DIAG_MODE=1` 分支里,不是被删掉了。

## ⚠️ Core2 直连 Chain host 未经官方背书

硬件文档标注未验证,官方范式要挂独立 Chain 主控(如 DualKey)。诊断模式下 `chain_bus_sniff`
仍可抓 PORT.C 原始字节自诊:有心跳=链路通只是没认到,一字节都无=没供电/接反/直连不成立。

## 🔴 供电与省电

- **PORT.C 供电 = M-Bus 5V(EXTEN)**,与 PORT.A 单元同源(`core2_board_init(enable_leds=true)` 已代开);深度省电切 5V → 节点断电复位,唤醒后重扫接管 + 游戏层复位安全位置。
- **桌面玩法省电坑**照旧:机身不动≠没人玩,转/推/按都 `core2_sleep_kick`。

## 待实机标定(tuning.h)

**沿用 v1(摇杆/编码器标定)**:`JOY_HALF_SPAN`(归一化满量程)、`JOY_DEADZONE/JOY_MOVE_KICK`、`JOY_INVERT_X/Y/SWAP_XY`(摇杆轴向;**实测左右反已默认 X 取反**)、`ENC_DEG_PER_STEP`(诊断台表盘度/格)、`NODE_RGB_BRIGHTNESS`。

**新增(抓娃娃机机制,SPEC §9,以下默认值均未经实机验证)**:`CRANE_X_RANGE_PX`(吊臂横向范围)、`DESCEND_PER_TICK`(编码器每格对应下降像素)、`DESCEND_MAX_PX`/`DESCEND_SNAP_TOL`(下降深度/自动到底容忍)、`GRAB_ALIGN_TOL_PX`(抓取横向容差)、`GRAB_MS`/`ASCEND_MS`/`DEPOSIT_MS`(动画时长)、`PARTY_HOLD_MS`。重点待验证 SPEC §11②:曲柄式深度控制对幼儿是否偏难,不好玩的话考虑改成"编码器只控方向、深度自动匀速下降"。

## launcher 图标

launcher 沿用既有 chain_lab 专属图标(旋钮+摇杆方框,**要重刷 launcher 才显示**;不刷也能玩、显示通用笑脸)。本轮玩法重写未改图标,是否需要换成抓娃娃机主题图标留给后续。

## 状态

✅ **两种编译模式均 build 通过**(`idf.py -C apps/chain_lab build`,ESP-IDF v5.3.3):
`CHAIN_LAB_DIAG_MODE=0`(抓娃娃机,默认)bin ≈ 0xa9c80 字节(约 679KB),槽内(0x180000)余 56%;
`CHAIN_LAB_DIAG_MODE=1`(诊断台)bin ≈ 0xa91e0 字节,槽内余 56%。

⏳ **待烧录实机**(SPEC §12 验收清单,逐条待用户烧录后点检):
- [ ] 摇杆推吊臂左右移动跟手;编码器顺/逆时针转对应爪子下降/回收,中途反悔不留痕迹。
- [ ] 抓取按键触发闭合→上升→落架/空爪全套动画;抓空是"诶?"而不是任何负面反馈。
- [ ] 抓中的战利品正确落入展示架;集满当前批次触发派对,派对后清空换新一批。
- [ ] 编码器节点 RGB 随下降深度变化可辨认;抓取瞬间两节点按预期闪白/跑彩虹。
- [ ] 打盹/深度省电如常进出;DEEP 唤醒后爪子/吊臂复位安全位置、节点重扫接管。
- [ ] 诊断模式(`CHAIN_LAB_DIAG_MODE=1`)重编重刷后能正常进入,原表盘/光点读数准确,`chain_bus_sniff` 排障路径未受影响。
- [ ] 拔线(任一节点)→ 无字提示卡行为不变,插回即恢复。
- [ ] SPEC §11⑤:正常玩法强度(反复拧曲柄、推摇杆到底)下两节点级联链路稳定性抽查。
