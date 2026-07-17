# 烧录对照表(多 App 分区,partitions.csv 唯一真源)

> 🔴 **红线:评估 app 工程严禁 `idf.py flash`** ——它把 app 烧到 0x10000(factory),
> **会覆盖 launcher**!评估 app 只能按下表用 esptool 单刷自己的槽位。
> (误刷了也能救:回 launcher 工程全量刷一次即可,见下。)

## 槽位 → 偏移 → 命令

| 分区 | 偏移 | 内容 | 单刷命令(在对应工程 build/ 出 bin 后) |
|---|---|---|---|
| factory | `0x10000` | launcher 选择页 | 用下方"全量刷"(还包含 bootloader/分区表/otadata) |
| ota_0 | `0x190000` | unit_bench 外设/单元评估台(PORT.A I2C + Chain) | `python -m esptool --chip esp32 -p <PORT> write-flash 0x190000 apps/unit_bench/build/unit_bench.bin` |
| ota_1 | `0x390000` | power_lab 功耗/系统评估台(AXP192 遥测) | `python -m esptool --chip esp32 -p <PORT> write-flash 0x390000 apps/power_lab/build/power_lab.bin` |
| ota_2 | `0x590000` | 预留(2026-07-17 平台转向,原 chick_pour 槽) | — |
| ota_3 | `0x790000` | 预留 | — |
| ota_4 | `0x990000` | 预留(2026-07-17 平台转向,原 chain_lab 槽) | — |
| ota_5 | `0xB90000` | 预留(2026-07-17 平台转向,原 fish_pond 槽) | — |
| storage | `0xD90000` | 共享数据区(spiffs,~2.4M) | power_lab P4 离线录制(拔 USB 放电曲线)等落这 |

`tools/flash_one.sh <app名> [PORT]` 可直接打印/执行对应命令(WSL 内只打印,拿到 WSL 外执行)。

## 全量刷(首次上机 / 改了 partitions.csv / 误刷救砖)

在 `launcher/` 工程 build 后:

```bash
idf.py -C launcher -p <PORT> flash        # bootloader + 分区表 + otadata 初始 + launcher
```

之后再按上表单刷各评估槽。**改 partitions.csv = 所有分区错位,必须全量刷 + 所有评估槽重刷。**

## 日常迭代(改一个评估 app)

```bash
idf.py -C apps/unit_bench build                                       # WSL 内编译
python -m esptool --chip esp32 -p COM3 write-flash 0x190000 \
       apps/unit_bench/build/unit_bench.bin                            # WSL 外单刷(~5s)
```

launcher 与其它评估槽分毫不动。烧完上电即回 launcher(评估 app 启动时已擦 otadata)。

> 2026-07-17 平台转向 IoT 评估台:tilt_maze/busy_knobs/chick_pour/chain_lab/fish_pond/
> pipe_garden 六个游戏 app 已删除(git 历史可查),槽位表按上方重排。
