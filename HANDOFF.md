# HANDOFF.md — 倾斜迷宫工程迁移交接总纲

> 本文件把**跨机器持续开发所需的全部上下文**固化进工程(打包即带走)。
> 原开发机 `~/.claude/` 里的私有「记忆」**不会随工程迁移**,故在此汇总。
> **规格与设计意图看 `CLAUDE.md`(§1–§20 完整);本文只讲「环境 / 迁移 / 当前进度 / 坑」。**
> 最近更新:2026-07-02。
>
> ✅ **2026-07-02 迁移已完成并实机验证**:应用已从拷贝目录 `~/esp/maze` 整体迁入本 git 仓库
> `core2-kids-platform`(原平台模板 demo 应用废弃,`bsp_core2`/`app_core` 等旧组件已删,
> git 历史可找回)。本机构建走 **esp-idf MCP**(见 `AGENTS.md` §0 铁律),不再手敲 `idf.py`。
> **本机 WSL 不具备烧录条件**(无 USB 直通):烧录由用户在 WSL 外手动完成;
> 2026-07-02 迁移版固件已烧入本机板子,**无异常、按预期工作**。

---

## 0. 文档地图(先读哪份)

| 文件 | 作用 | 状态 |
|---|---|---|
| `CLAUDE.md` | **应用规格 + 竣工记录**(§20)+ 休眠/背光机制(§20.6)+ 坑位速查(§17) | 在,权威 |
| `AGENTS.md` | 通用协作规范(先查 MCP 再写、组件划分、init 用 `ESP_ERROR_CHECK` 等) | 在 |
| `docs/platform/Core2_v1_0.md` | 主控 Core2 v1.0 板级事实(AXP192 / 引脚 / strapping / MPU6886) | 在 |
| `docs/platform/M5GO_Bottom2.md` | 底座板级事实(IMU/电池/灯带来源) | 在(本仓库自带,已补回) |
| `docs/units/*.md` | 各 UNIT 外设硬件事实(本应用未用,留作扩展) | 在 |
| `HANDOFF.md`(本文) | 迁移 / 环境 / 进度 / 待办 | 在 |

---

## 1. 硬件平台(不变的前提)

**Core2 初代 v1.0 + M5GO Bottom2 底座(硬依赖)。** 本机 Core2 缺背部扩展模块,
**IMU(MPU6886 @ I2C 0x68)和电池都来自底座**,不接底座游戏跑不了。详见 CLAUDE.md §2。

- IMU 是 **MPU6886 不是 BMI270**——寄存器/轴向都别照搬 BMI270。
- 屏 ILI9342C(用 ili9341 兼容驱动)320×240 SPI;触摸 FT6336U;音频 NS4168;震动 = AXP192 LDO3。
- **底座灯带 10×SK6812 @ G25**,吃 M-Bus 5V(见 §4 坑)。

---

## 2. 新机器环境搭建(ESP-IDF **v6.0.1**,版本要对齐)

`dependencies.lock` + `managed_components/` 按 **IDF v6.0.1** 锁定,换大版本可能要重解依赖。建议新机也装 v6.0.1。

```bash
# 1) 安装 ESP-IDF v6.0.1(用官方安装器或 git clone --branch v6.0.1)
#    装完会得到一个 IDF_PATH 和一份 export/activate 脚本(路径每台机器不同)。

# 2) 每个新 shell 先激活(命令随安装方式而定,二选一):
source <IDF_PATH>/export.sh
#   —— 或本项目原机器用的是 eim 安装器生成的脚本:
#   source ~/.espressif/tools/activate_idf_v6.0.1.sh   # ⚠️ 文件名带 .1,不是 v6.0

# 3) 首次进工程:设 target + 让组件管理器按 lock 拉齐依赖
idf.py set-target esp32
```

> **原开发机的具体路径**(仅供参考,新机不同):`IDF_PATH=/home/wang/.espressif/v6.0.1/esp-idf`;
> 激活脚本 `~/.espressif/tools/activate_idf_v6.0.1.sh`(该脚本里 `idf.py` 是 **alias**,非交互 shell 不生效,
> 脚本化时直接用 `"$IDF_PATH/tools/idf.py"` 或先 `source $IDF_PATH/export.sh`)。
>
> **本机(wang_b2,2026-07-02 迁移目标机)实测**:装有 v6.0 与 v6.0.1 两套;拷贝来的 maze 工程
> 用 **v6.0**(`/home/wang_b2/.espressif/v6.0/esp-idf`,激活脚本 `~/.espressif/tools/activate_idf_v6.0.sh`,
> 文件名**不带 .1**)编译通过并实机正常工作 → 本仓库沿用 v6.0,lock 里的依赖照常解析。
> 日常编译/刷写按 `AGENTS.md` §0 走 esp-idf MCP,不直接用 idf.py。

---

## 3. 迁移清单(拷贝目录法,非 git 仓库)

**打包前务必**:
```bash
rm -rf build            # 186M,含本机绝对路径(CMake cache),换机必须重建,别带走
```
**要带走**:`main/ components/ managed_components/ CMakeLists.txt sdkconfig.defaults
partitions.csv dependencies.lock main/idf_component.yml tools/` + 所有 `*.md`。

**可带可不带**:`sdkconfig`(会按 `sdkconfig.defaults` 再生;**若改过 sdkconfig.defaults,新机先删 `sdkconfig` 再 fullclean**,见 AGENTS.md §3)。

**新机首次构建**:
```bash
idf.py set-target esp32     # 若带了旧 sdkconfig 又改过 defaults,先 rm sdkconfig
idf.py build
```

---

## 4. build / flash / 抓日志 工作流

```bash
idf.py build                              # 编译
idf.py -p /dev/ttyUSB0 flash              # 烧录(串口号按实际)
idf.py -p /dev/ttyUSB0 monitor            # 交互式串口监视(Ctrl+])

# 非交互抓日志(脚本化/给 AI 看时更好用,idf.py monitor 是交互式):
python3 tools/serial_capture.py /dev/ttyUSB0 60   # 硬复位到运行模式后抓 60s,每行带相对秒数
```

**串口注意**:
- 用户实机接的是 CP2104 → Core2,原机器上是 `/dev/ttyUSB0`(用户在 `dialout` 组,可直接烧)。
- 原**这台开发机时有时无串口**(记忆里一度「无串口设备」,后来又出现 `/dev/ttyUSB0`);节点消失=USB 掉线/板子断电,不是软件问题,重插即可。
- `tools/serial_capture.py` 会先做**运行态硬复位**——因为直接开串口默认拉 DTR/RTS 可能把芯片带进下载模式导致抓不到 App 日志(本次调试踩过)。

---

## 5. 当前进度(源码 = 最终干净版)

**M0–M7 七个里程碑核心功能全部完成,build 干净通过(target esp32)。** 可玩闭环:
倾斜 → 滚球 → 撞墙四通道反馈(画面/音/震/灯带)→ 到家全屏庆祝 → 4 关(草地/海边/星空/糖果)循环
→ idle 打盹 → 深度省电全黑 → 动一下唤醒 → 底部长按 3s 家长菜单。

**实机已验证**(设备 /dev/ttyUSB0,Core2 v1.0 + Bottom2):
M0(I2C 扫到 0x68、WHO_AM_I=0x19、屏/声/灯/震动)、M1(三轴打印)、M2(球随倾斜、重而稳)、
M3(滑行碰撞、到达)、M4(四通道反馈)、M5(状态机 + 4 关 + 难度档)、
**M6 idle 两级省电 + 唤醒(2026-07-01 用串口逐帧核对通过)**。

**待用户看屏最终确认**(逻辑已实现、开机不崩):家长菜单 UI、M7 视觉项(瞳孔朝向/挤压拉伸/收集星吸收/家脉动加快)。

**定案数值**全在 `main/tuning.h`,清单见 CLAUDE.md §20.2。**轴映射已实机定案**:`TILT_INVERT_X=1, TILT_INVERT_Y=0, TILT_SWAP_XY=0`。

---

## 6. 本次会话(2026-07-01)修的三件事 —— 休眠/背光,详见 CLAUDE.md §20.6

用户报「深度省电不生效」,串口逐帧诊断后定位并修复:

1. **打盹判据改为只看机身动作(IMU),不看球速**。原来 `sp>8` 也清打盹计数;机身平放但相对校准零点有残余倾斜(>死区 0.06g)时,球会永远以 ~20px/s 慢爬,`sp>8` 恒真 → **永不打盹**。改为只看帧间加速度变化 `s_motion`。
2. **唤醒去抖**:新增 `WAKE_DEBOUNCE_FRAMES=3`,要连续 3 帧 `s_motion>0.12` 才算真动。原来单帧 IMU 噪声尖峰就会误唤醒、把 60s 深度省电计时整段作废 → 熬不满。
3. 🔴 **`brightness_set(0)` 根本不熄屏**:Core2 背光=AXP192 **DCDC3**,BSP 亮度只调 DCDC3 电压(0% 仍 ~2.95V,有微光)。新增 `power_backlight(bool)`(`main/power.c`)在深度省电时**断 DCDC3 使能**(REG 0x12 bit1)真黑屏,唤醒时重启。与「灯带要开 EXTEN」同类坑。

上述均已实机验证:打盹→深度省电(真黑屏+灯带灭+切 5V)→拿起唤醒(+轻震)全链路通。源码已清掉调试脚手架、超时恢复正式值(12s/60s)。

> **迁移相关**:源码是**最终干净版**。原机器上那块板子当时因 USB 掉线,最后一次「正式版烧录」没成功——但这**只影响原机器的物理板**(它上面还留着带调试脚手架 6s/20s 的旧固件),**不影响迁移**:新机 build 出来的就是干净版,直接烧即可。

---

## 7. 关键坑 / 约束(勿踩,细节见 CLAUDE.md §17 / §20.6)

- **IMU 是 MPU6886 非 BMI270**;轴↔屏映射实测标定(已定案)。
- 🔴 **灯带全黑先查 AXP192 EXTEN**:底座 SK6812 吃 M-Bus 5V(SY7088 升压,EXTEN=REG 0x12 bit6);**BSP 从不开 EXTEN**,数据线正常翻转、`refresh` 返回 OK 也可能全黑。`main/power.c` bring-up 时开(须在 `bsp_display_start` 之后,BSP 初始化会清 REG 0x12)。
- 🔴 **`brightness_set(0)` 不熄屏**:真黑屏要断 DCDC3(REG 0x12 bit1,`power_backlight()`)。
- **打盹只看机身动作,别看球速**;唤醒要多帧去抖(见 §6)。
- **渲染红线:永不每帧整屏重绘**(经典 ESP32 无 2D 加速,SPI 40MHz 整屏 ~31ms);静态层进关画一次、关内只刷脏矩形。详见 CLAUDE.md §9。
- **关卡手工编排 + 加载 BFS 可解性校验**(§4.1/§19),别用纯随机。
- **改 `sdkconfig.defaults` 后删 `sdkconfig` 再 fullclean**。
- AXP192 不初始化 = 屏黑/无声/无触摸(BSP 已处理,别误判硬件坏)。

---

## 8. 剩余可选打磨(非阻塞,现已是完整可玩成品)

1. 烘焙美术精灵图(现为程序化色块/圆角块/圆形球,在帧预算内)+ 各世界给吉祥物「圆圆」换帽(§18.4)。
2. §18.5 待机微动(花摇/窄浪带/星眨/糖霜滴)——严格压在 §9.2 帧预算内。
3. 五角星形收集物(现为圆占位);静止眨眼;触屏直接开始(现仅倾斜触发 ATTRACT→CALIBRATE)。
4. 家长设置 **NVS 持久化**(现重启回默认:亮度/音量/震动/难度档)。
5. 更深省电可上真·light-sleep(需 MPU6886 INT→GPIO 唤醒,复杂度高)。
6. 中文化家长菜单(现英文标签+LVGL 符号;要中文需配 CJK 字体)。
7. 补回 `docs/platform/M5GO_Bottom2.md`(CLAUDE.md 有引用,工程内缺)。

---

## 9. 工程结构速览(与 CLAUDE.md §3.1 / §20.3 一致)

```
main/       app_main.c(bring-up + 起状态机)· game_state.c(60Hz 任务 + 状态机 + 两级省电)
            physics.c · maze.c(4 关 + BFS 校验 + 滑行碰撞)· render.c(三层渲染 + 特效)
            feedback.c(四通道编排器)· parent_menu.c · power.c(AXP192 EXTEN/DCDC3)· tuning.h(全部调参)
components/  imu_mpu6886 · maze_audio · ledstrip_fx · haptics
managed_components/  m5stack_core_2(BSP)· lvgl · esp_lcd_* · led_strip · esp_codec_dev …(按 lock 锁定)
tools/      serial_capture.py(非交互抓日志)
```
