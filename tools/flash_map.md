# 烧录对照表(多 App 分区,partitions.csv 唯一真源)

> 🔴 **红线:游戏工程严禁 `idf.py flash`** ——它把 app 烧到 0x10000(factory),
> **会覆盖 launcher**!游戏只能按下表用 esptool 单刷自己的槽位。
> (误刷了也能救:回 launcher 工程全量刷一次即可,见下。)

## 槽位 → 偏移 → 命令

| 分区 | 偏移 | 内容 | 单刷命令(在对应工程 build/ 出 bin 后) |
|---|---|---|---|
| factory | `0x10000` | launcher 选择页 | 用下方"全量刷"(还包含 bootloader/分区表/otadata) |
| ota_0 | `0x190000` | tilt_maze 倾斜迷宫 | `python -m esptool --chip esp32 -p <PORT> write-flash 0x190000 apps/tilt_maze/build/tilt_maze.bin` |
| ota_1 | `0x390000` | (预留:躲猫猫/DLight) | `python -m esptool --chip esp32 -p <PORT> write-flash 0x390000 <bin>` |
| ota_2 | `0x590000` | (预留:空气琴/超声波) | 同上,偏移 `0x590000` |
| ota_3 | `0x790000` | (预留:忙碌台/8Encoder) | 同上,偏移 `0x790000` |
| ota_4 | `0x990000` | (预留) | 同上,偏移 `0x990000` |
| ota_5 | `0xB90000` | (预留) | 同上,偏移 `0xB90000` |
| storage | `0xD90000` | 共享素材区(spiffs,~2.4M) | 将来放烘焙精灵图/音效 |

`tools/flash_one.sh <app名> [PORT]` 可直接打印/执行对应命令(WSL 内只打印,拿到 WSL 外执行)。

## 全量刷(首次上机 / 改了 partitions.csv / 误刷救砖)

在 `launcher/` 工程 build 后:

```bash
idf.py -C launcher -p <PORT> flash        # bootloader + 分区表 + otadata 初始 + launcher
```

之后再按上表单刷各游戏槽。**改 partitions.csv = 所有分区错位,必须全量刷 + 所有游戏重刷。**

## 日常迭代(改一个游戏)

```bash
idf.py -C apps/tilt_maze build                                        # WSL 内编译
python -m esptool --chip esp32 -p COM3 write-flash 0x190000 \
       apps/tilt_maze/build/tilt_maze.bin                              # WSL 外单刷(~5s)
```

launcher 与其它游戏槽分毫不动。烧完上电即回 launcher(游戏启动时已擦 otadata)。
