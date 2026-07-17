# launcher — IoT 评估台选择页(as-built)

> 本文件是 **launcher 工程 + 多 App 分区机制的竣工记录**。**占 factory 分区**。多 App 单刷纪律的
> 速查见根 `CLAUDE.md` §9;分区偏移权威表见 `tools/flash_map.md`。
>
> **2026-07-17 平台转向**:原「幼儿游戏卡带机选择页」重写为**数据驱动**的 IoT 评估台选择页——
> 不再需要为每个新 app 手绘图标分支 + 重刷 launcher,详见下方"数据驱动渲染"。

## 仓库形态:评估卡带机

factory 分区常驻 **launcher 选择页**(本工程),6 个 ota 槽各放一个独立评估 app bin。**改一个
app 只重编+单刷它自己(~600KB)**,launcher 与其它 app 零风险。

- **机制** = IDF otadata 启动选择(`esp_ota_set_boot_partition`,**无网络成分**),组件化为 `components/app_slot`。
- **分区表全表重写**(`partitions.csv`:factory 1.5M + ota_0~5 各 2M + spiffs ~2.4M),**定案冻结,改表=全量重刷**,烧录对照 `tools/flash_map.md`。

## 数据驱动渲染(2026-07-17 起,取代图标分支)

launcher 用 `app_slot_info(idx, &info)` 直接读每个槽里 app 编译进去的 `esp_app_desc_t`
(`project_name`/`version`/`date`/`time`),渲染成卡片文字(工程名 + 版本/编译日期 + `ota_N`
角标)。**加一个新评估 app 不需要碰 launcher 代码、也不需要重刷 launcher**——这是受众从不识字
幼儿改为识字评估者带来的架构简化(游戏时期每加一张卡带都要在 `app_main.c` 里加一个手绘图标
函数 + `strcmp` 分支,并重刷 launcher 才显示)。

空槽显示 `empty / ota_N`(深灰底色);有效槽显示彩色卡片(工程名等信息)。

## 各评估 app 接入 launcher 的改动

每个 app 的 `app_main.c` 需要:
1. 第一行 `app_slot_return_to_factory()`(此后任何复位/崩溃都回 launcher,**crash-safe**)。

🔴 **电源键触发的回 launcher 已于 2026-07-09 整体取消**(`app_slot_enable_button_exit()` /
`core2_power_pek_pressed()` 均已删除):电源键唯一剩下的行为是 AXP192 硬件本身按住 ≥4s
强制断电(纯硬件,不经软件)。评估台目前**没有回 launcher 的软件入口**(不再有幼儿掌机时期的
家长菜单概念),只能靠崩溃/复位或整机断电重开;若某 app 需要页内"返回"按钮,自行加一个调
`app_slot_return_to_factory()` + `esp_restart()` 的按钮即可。详见 `components/app_slot/README.md`。

各工程共享根部 `components/`、`partitions.csv`、`sdkconfig.platform`(经 `SDKCONFIG_DEFAULTS`/`EXTRA_COMPONENT_DIRS` 跨目录引用,已验证可行)。

## 🔴 构建 / 烧录纪律(多工程后变化)

- esp-idf MCP 的 build 固定指向仓库根、不支持选工程目录,多工程后编译改走命令行 `idf.py -C launcher|apps/<app> build`。
- 🔴 **评估 app 工程严禁 `idf.py flash`**(会烧 0x10000 覆盖 launcher);单刷用 `esptool write-flash <槽偏移> <bin>`(见 `tools/flash_map.md` / `flash_one.sh`)。
- 新评估 app 脚手架:`tools/new_app.sh <名>`。

## launcher 形态

工程风深灰配色(3×2 卡片,空槽灰显但点了也有轻反馈)+ 顶部状态栏(电池/USB);点卡片 → 校验
镜像 → 重启进 app(~2-3s 黑屏,物理卡带机固有体验);久置走 `core2_sleep` 两级省电。

**二期预留**:PORT.A 探测 I2C 单元(DLight 0x23/8Encoder 0x41/超声波 0x57/手势 0x73)→"插卡带
即启动"(`launcher/main/app_main.c` 头注释有预留位;与 IoT 评估台定位下是否还需要待评估)。

## 槽位分配现状

| 槽 | 偏移 | 评估 app | 外设 |
|---|---|---|---|
| factory | — | launcher | — |
| ota_0 | 0x190000 | unit_bench | PORT.A I2C + Chain |
| ota_1 | 0x390000 | power_lab | AXP192 遥测 |
| ota_2 | 0x590000 | (预留,2026-07-17 平台转向回收) | — |
| ota_3 | 0x790000 | (预留) | — |
| ota_4 | 0x990000 | (预留,2026-07-17 平台转向回收) | — |
| ota_5 | 0xB90000 | (预留,2026-07-17 平台转向回收) | — |

> 偏移以 `tools/flash_map.md` 为准。历史幼儿游戏卡带槽位分配见 git 历史
> (`git log -- launcher/README.md`)。

## 状态

🔄 数据驱动重写中(Phase 1);待重编 + 全量刷验收:状态栏电池读数合理、空槽/有效槽卡片渲染
正确、点卡片进 app / 断电重启回 launcher / 单刷迭代不动 launcher。
