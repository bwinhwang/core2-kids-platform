# Core2 IoT 评估平台(多 App 平台 + 示例应用「外设/单元评估台」)

本仓库是一台"评估卡带机":**factory 分区常驻 launcher 选择页,6 个 ota 槽各放一个
独立评估 app bin,改哪个 app 只重编+单刷它自己(~600KB,launcher 与其它 app 分毫不动)**。
机制见 `components/app_slot/README.md`,烧录对照 `tools/flash_map.md`。

> **2026-07-17 平台转向**:原「幼儿游戏掌机」定位与 6 张幼儿游戏卡带已整体下线(git 历史
> 留档),硬件平台不变,重心改为 **IoT 硬件评估**(外设/单元评估、功耗/系统评估)。决策与
> 三个 Phase 见 `docs/ROADMAP.md`。

1. **可复用平台(核心价值)**:Core2 v1.0 + M5GO Bottom2 的外设层已实机调通一次,
   沉淀为 `components/` 下的可复用组件(一键 bring-up / AXP192 直控 / 灯带 / 音效 /
   震动 / IMU / 动作检测 / 分区自举 / AXP192 遥测 / NVS 封装 / 评估 UI 控件 / CSV 导出)。
   **做新 APP = `tools/new_app.sh <名字>` 在 `apps/` 下生成工程 → 写评估逻辑 → 单刷进一个
   评估槽。** 复用指南:**`docs/platform/BSP_GUIDE.md`(新应用必读)**。
2. **示例应用「外设/单元评估台」**(`apps/unit_bench`,ota_0):扫描 PORT.A I2C / Chain
   菊花链,识别已知单元、展示数值+趋势图、支持热插拔与标定持久化。它同时是平台组件的用法
   参考。另有「功耗/系统评估台」(`apps/power_lab`,ota_1)评估 AXP192 电压/电流与休眠功耗。

- **应用规格 + 竣工记录**:各 `apps/<name>/SPEC.md` + `README.md`;平台级规范见 `CLAUDE.md`
- **平台复用指南**:`docs/platform/BSP_GUIDE.md`;各组件用法见 `components/*/README.md`
- **AI 协作规范(MCP 铁律)**:`AGENTS.md`
- **板级硬件事实**:`docs/platform/Core2_v1_0.md`、`docs/platform/M5GO_Bottom2.md`

> 2026-07-02 由拷贝工程 `~/esp/maze` 整体迁入本仓库;原「幼儿应用平台模板」demo 及
> `bsp_core2`/`app_core` 等自写组件已废弃删除(git 历史可找回)。现应用基于官方
> `espressif/m5stack_core_2` BSP(AXP192/LCD/触摸/LVGL/喇叭)+ 自写组件层(详见
> `CLAUDE.md` §4)。2026-07-17 平台转向后新增评估类组件(`power_monitor`/`kv_store`/
> `ui_kit`/`data_log`),历史幼儿游戏卡带代码见 git 历史。

## 硬件(硬依赖,缺一不可)

**Core2 初代 v1.0 + M5GO Bottom2 底座。** 本机 Core2 缺背部扩展模块,
**IMU(MPU6886 @0x68)与电池都来自底座**,不接底座游戏跑不了。灯带 10×SK6812 @ G25。

## 构建 / 烧录(多工程:launcher/ + apps/*)

> ⚠️ esp-idf MCP 的 build 工具固定指向仓库根、不支持选工程目录,多工程化后**编译改走
> 命令行 `idf.py -C <工程目录>`**(或把 MCP server 重配到某个工程目录)。

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh   # 本机 IDF v6.0
idf.py -C launcher build                    # 编 launcher
idf.py -C apps/unit_bench build              # 编一个评估 app
python3 tools/serial_capture.py /dev/ttyUSB0 60   # 非交互抓日志(先硬复位到运行态)
```

**烧录规则(🔴 详见 `tools/flash_map.md`,别背偏移)**:

```bash
idf.py -C launcher -p <PORT> flash          # 全量刷:首次/改分区表/救砖(带 bootloader+表+otadata)
python -m esptool --chip esp32 -p <PORT> write-flash 0x190000 apps/unit_bench/build/unit_bench.bin
                                            # 日常:单刷一个评估槽(launcher/其它 app 不动)
```

🔴 **评估 app 工程严禁 `idf.py flash`**——会烧到 factory 偏移**覆盖 launcher**(误刷用全量刷恢复)。

改 `sdkconfig.platform` / 工程 `sdkconfig.defaults` 后:对应工程 `rm -f sdkconfig` → fullclean → build;
**改 `partitions.csv` = 所有分区错位,全量刷 + 所有评估槽重刷**。

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
| `partitions.csv` | ★ 分区表唯一真源(factory=launcher + ota_0~5 评估槽;定案冻结) |
| `sdkconfig.platform` | 共享 sdkconfig 默认值(各工程 CMakeLists 引入) |
| `launcher/` | factory 分区常驻的评估台选择页(上电入口,数据驱动渲染各槽点击进入) |
| `apps/unit_bench/` | 评估 app:外设/单元评估台(ota_0;PORT.A I2C + Chain 扫描/详情/CSV 导出) |
| `apps/power_lab/` | 评估 app:功耗/系统评估台(ota_1;AXP192 遥测 + 负载矩阵 + 休眠演练) |
| `components/app_slot/` | 多 App 分区自举(launch / 回 launcher / app_slot_info 数据驱动) |
| `components/core2_board/` | ★ 平台一键 bring-up(固化初始化顺序,新应用从这里开始) |
| `components/core2_power/` | AXP192 直控:M-Bus 5V(EXTEN)/ 背光真开关(DCDC3)+ 底层寄存器读原语 |
| `components/power_monitor/` | AXP192 遥测:电池/VBUS 电压电流、充电状态(+库仑计,视查证结果) |
| `components/kv_store/` | NVS 封装(标定/设置持久化,每 app 一个 namespace) |
| `components/ui_kit/` | 评估台 UI 控件:状态栏/数值卡/chart/列表菜单(守渲染红线) |
| `components/data_log/` | 串口 CSV 导出(自动时间戳),SPIFFS 离线录制(规划中) |
| `components/imu_mpu6886/` | MPU6886 三轴加速度(0x68,复用 BSP I2C;不是 BMI270) |
| `components/ledstrip_fx/` | 底座灯带效果引擎(基础模式+瞬态特效,40fps 后台任务) |
| `components/audio_fx/` | 音效引擎(程序化合成+自定义音序,防爆音纪律内置) |
| `components/haptics/` | 震动模式库(事件队列,非阻塞) |
| `components/motion_detect/` | "机身有没有被动过"检测(帧间差+唤醒去抖,纯逻辑) |
| `components/core2_sleep/` | 两级省电编排器(打盹→深度省电→去抖唤醒,可 force_stage 手动驱动) |
| `components/units/` | 8 个外接单元驱动(8Encoder/超声波/DLight/手势/RGB/Chain Encoder/Joystick/unit_probe) |
| `docs/platform/` | Core2 / Bottom2 板级真值(勿改)+ `BSP_GUIDE.md` 复用指南 |
| `docs/units/` | 各 UNIT 外设硬件真值(unit_bench 识别/详情要用) |
| `docs/ROADMAP.md` | 平台转向决策记录 + 三个 Phase 路线图(含幼儿掌机时期路线图存档) |
| `tools/` | `serial_capture.py` 抓日志 · `flash_map.md`/`flash_one.sh` 烧录对照 · `new_app.sh` 新评估 app 脚手架 |
