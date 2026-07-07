# launcher — 游戏卡带机选择页(as-built)

> 本文件是 **launcher 工程 + 多 App 分区机制的竣工记录**(原单-app CLAUDE.md §20.13,2026-07 迁出)。
> **占 factory 分区**。多 App 单刷纪律的速查见根 `CLAUDE.md` §9;分区偏移权威表见 `tools/flash_map.md`。

## 仓库形态:游戏卡带机

factory 分区常驻 **launcher 选择页**(本工程),6 个 ota 槽各放一个独立游戏 bin。**改一个游戏只重编+单刷它自己(~600KB)**,launcher 与其它游戏零风险。

- **机制** = IDF otadata 启动选择(`esp_ota_set_boot_partition`,**无网络成分**),组件化为 `components/app_slot`。
- **分区表全表重写**(`partitions.csv`:factory 1.5M + ota_0~5 各 2M + spiffs ~2.4M),**定案冻结,改表=全量重刷**,烧录对照 `tools/flash_map.md`。

## 各游戏接入 launcher 的三处改动

每个 app 的 `app_main.c` 需要:
1. 第一行 `app_slot_return_to_factory()`(此后任何复位/崩溃都回 launcher,**crash-safe**);
2. `app_slot_enable_button_exit()`(**电源键按一下 = 回 launcher**,AXP192 PEK REG 0x46,`core2_power_pek_pressed()`);
3. 家长菜单增 Home 按钮。

🔴 **电源键短按/长按标志都要算**:BSP 写 REG 0x36=0x4C → 按住 ≥1s 只报"长按"bit0、≥4s AXP 硬断电,首版只认短按 bit1 时灵时不灵——2026-07-03 实机踩坑修正,轮询 150ms+命中先轻震确认,详见 `components/app_slot/README.md`。

各工程共享根部 `components/`、`partitions.csv`、`sdkconfig.platform`(经 `SDKCONFIG_DEFAULTS`/`EXTRA_COMPONENT_DIRS` 跨目录引用,已验证可行)。

## 🔴 构建 / 烧录纪律(多工程后变化)

- esp-idf MCP 的 build 固定指向仓库根、不支持选工程目录,多工程后编译改走命令行 `idf.py -C launcher|apps/<app> build`。
- 🔴 **游戏工程严禁 `idf.py flash`**(会烧 0x10000 覆盖 launcher);单刷用 `esptool write-flash <槽偏移> <bin>`(见 `tools/flash_map.md` / `flash_one.sh`)。
- 新游戏脚手架:`tools/new_app.sh <名>`。

## launcher 形态

暖色卡带架(3×2 大图标,空槽灰显但点了也有回应)+ 吉祥物浮动;点图标 → 校验镜像 → 重启进游戏(~2-3s 黑屏,物理卡带机固有体验);久置走 core2_sleep 两级省电。

每加一个新游戏,launcher 里加它的专属图标分支(**要重刷 launcher 才显示**;不刷也能玩、显示通用笑脸)。

**二期预留**:PORT.A 探测 I2C 单元(DLight 0x23/8Encoder 0x41/超声波 0x57/手势 0x73)→"插卡带即启动"(`launcher/main/app_main.c` 头注释有预留位)。

## 槽位分配现状

| 槽 | 偏移 | 游戏 | 外设 |
|---|---|---|---|
| factory | — | launcher | — |
| ota_0 | 0x210000 | tilt_maze | IMU MPU6886 |
| ota_1 | 0x390000 | busy_knobs | 8Encoder |
| ota_2 | 0x590000 | peekaboo | DLight |
| ota_3 | 0x790000 | feed_monster | 超声波 |
| ota_4 | 0x990000 | chain_lab | Chain Encoder/Joystick |
| ota_5 | — | (空) | — |

> 偏移以 `tools/flash_map.md` 为准。

## 状态

✅ launcher(608KB)build 通过(各槽余 ~60%);
⏳ 待实机:全量刷 launcher → 单刷各槽 → 验收"点图标进游戏/电源键短按回 launcher/Home 回 launcher/断电重启回 launcher/单刷迭代不动 launcher"。
