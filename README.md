# Core2 幼儿应用平台 + 示例应用「倾斜迷宫」

本工程有**双重身份**:

1. **可复用平台(核心价值)**:Core2 v1.0 + M5GO Bottom2 的外设层已实机调通一次,
   沉淀为 `components/` 下的可复用组件(一键 bring-up / AXP192 直控 / 灯带 / 音效 /
   震动 / IMU / 动作检测)。**做新 APP = 复制本工程 → 保留组件层 → 换 `main/`。**
   复用指南:**`docs/platform/BSP_GUIDE.md`(新应用必读)**。
2. **示例应用「倾斜迷宫」**:给 3~4 岁幼儿的滚珠迷宫——倾斜机身滚球进「家」触发庆祝,
   四关循环,零失败、无文字、四通道反馈(屏/声/震/灯带)。它同时是平台组件的用法参考。

- **应用规格 + 竣工记录**:`CLAUDE.md`(§20 是 as-built 事实,§17 坑位速查)
- **平台复用指南**:`docs/platform/BSP_GUIDE.md`;各组件用法见 `components/*/README.md`
- **AI 协作规范(MCP 铁律)**:`AGENTS.md`
- **板级硬件事实**:`docs/platform/Core2_v1_0.md`、`docs/platform/M5GO_Bottom2.md`

> 2026-07-02 由拷贝工程 `~/esp/maze` 整体迁入本仓库;原「幼儿应用平台模板」demo 及
> `bsp_core2`/`app_core` 等自写组件已废弃删除(git 历史可找回)。现应用基于官方
> `espressif/m5stack_core_2` BSP(AXP192/LCD/触摸/LVGL/喇叭)+ 自写组件层
> (平台化改造后共 8 个,见下表;详见 `CLAUDE.md` §20.7)。

## 硬件(硬依赖,缺一不可)

**Core2 初代 v1.0 + M5GO Bottom2 底座。** 本机 Core2 缺背部扩展模块,
**IMU(MPU6886 @0x68)与电池都来自底座**,不接底座游戏跑不了。灯带 10×SK6812 @ G25。

## 构建 / 烧录

AI 协作时编译/刷写走 **esp-idf MCP**(`AGENTS.md` §0 铁律);人工命令行:

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh   # 本机 IDF v6.0
idf.py build
idf.py -p /dev/ttyUSB0 flash
python3 tools/serial_capture.py /dev/ttyUSB0 60   # 非交互抓日志(先硬复位到运行态)
```

改 `sdkconfig.defaults` 后:`rm -f sdkconfig` → fullclean → build。

### 环境(换机器先看)

- 依赖按 `dependencies.lock` + `managed_components/` 锁定(**IDF v6.0 系**;原开发机 v6.0.1、
  本机 v6.0 均实测可用;换大版本可能要重解依赖)。新机首次:`idf.py set-target esp32`
  (组件管理器按 lock 拉齐);`build/` 含本机绝对路径,换机必删重建。
- eim 安装器的激活脚本名跟版本走:本机 `~/.espressif/tools/activate_idf_v6.0.sh`(**不带 .1**),
  原开发机是 `activate_idf_v6.0.1.sh`(**带 .1**)——别拿错。
- 激活脚本里 `idf.py` 是 **alias,非交互 shell 不生效**;脚本化直接用 `"$IDF_PATH/tools/idf.py"`
  或先 `source $IDF_PATH/export.sh`。
- **本机 WSL 无 USB 直通,不能烧录**:烧录由用户在 WSL 外手动完成。

### 串口 / 抓日志注意

- 实机经 CP2104 → `/dev/ttyUSB0`(烧录用户需在 `dialout` 组);**节点消失 = USB 掉线/板子断电**,
  不是软件问题,重插即可。
- 直接开串口默认拉 DTR/RTS,可能把芯片带进**下载模式**、抓不到 App 日志(踩过);
  `tools/serial_capture.py` 会先做运行态硬复位再抓——脚本化/给 AI 看日志用它,`idf.py monitor` 是交互式。

## 目录

| 路径 | 作用 |
|---|---|
| `components/core2_board/` | ★ 平台一键 bring-up(固化初始化顺序,新应用从这里开始) |
| `components/core2_power/` | AXP192 直控:M-Bus 5V(EXTEN)/ 背光真开关(DCDC3)两大坑固化 |
| `components/imu_mpu6886/` | MPU6886 三轴加速度(0x68,复用 BSP I2C;不是 BMI270) |
| `components/ledstrip_fx/` | 底座灯带效果引擎(基础模式+瞬态特效,40fps 后台任务) |
| `components/audio_fx/` | 音效引擎(程序化合成+自定义音序,防爆音纪律内置) |
| `components/haptics/` | 震动模式库(事件队列,非阻塞) |
| `components/motion_detect/` | "有没有人在玩"检测(帧间差+唤醒去抖,纯逻辑) |
| `components/core2_sleep/` | 两级省电编排器(打盹→深度省电→去抖唤醒,时序知识固化) |
| `main/` | 示例应用:game_state(60Hz+状态机+两级省电)· physics · maze(4 关+BFS 校验)· render(三层渲染)· feedback(四通道编排)· parent_menu · tuning.h(全部调参) |
| `docs/platform/` | Core2 / Bottom2 板级真值(勿改)+ `BSP_GUIDE.md` 复用指南 |
| `docs/units/` | 各 UNIT 外设硬件真值(本应用未用,留作扩展) |
| `tools/` | `serial_capture.py` 非交互串口抓取 |
