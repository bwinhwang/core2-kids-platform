# feed_monster — 喂怪兽 / 空气琴(as-built)

> 本文件是 **feed_monster 应用的竣工记录**(原单-app CLAUDE.md §20.16,2026-07 迁出)。
> 跨应用平台事实(单元接入/桌面玩法省电坑)见根平台手册 `CLAUDE.md`(§10 做新 App 指南、§7 电源·休眠、§11 坑位)。源自 `idea_1.txt` 创意 #6。
> **占 ota_3(0x790000)**,外设 = Unit Ultrasonic-I2C(PORT.A @0x57)。
> 烧录:`tools/flash_one.sh feed_monster`(= esptool write-flash 0x790000)。

## 玩法 = 连续层(空气琴)+ 喂食钩子

超声波测手离探头远近 `d`,归一化 `t`(近=1)。守零失败/即时因果/多通道冗余/渲染红线。

- **连续层始终在、零技巧**:嘴张开大小 = t(唯一每帧动的主对象)、音高 = t 量化 8 档 C 大调五声音阶(近=高,仅档变时弹一声防机枪音)、底座灯带 t 过阈切 NEAR/AMBIENT。
- **喂食循环**:怪兽头顶浮饼干,手进吃区(`d≤EAT_MM=90mm`,**边沿触发**、须先退过 `EAT_EXIT_MM=140mm` 再武装)→ 饼干飞入嘴 CHOMP(嘴瞬闭再张)+ 收集音 + 轻震 + 灯扫圈 + 肚子弹一下;喂满 `WIN_FEED_COUNT=5` → 全屏迸发庆祝(限量 8 星)→ 清零重开。永不失败、无计时。

## 平台新增(可复用)

`components/units/unit_ultrasonic` —— RCWL-9620 最小驱动(触发写 `0x01` + 等 ≥50ms + 读 3 字节 24-bit **大端 µm**)。🔴 **触发/读是两笔独立事务、不用 repeated-start**(与官方库一致,也避开 8Encoder 那类 MCU 从机组合读钳死总线的坑,虽 RCWL 是纯回波芯片不吃这套)。越界返回 `ESP_ERR_NOT_FOUND`="没目标"(单元仍在),与 TIMEOUT/INVALID_RESPONSE="拔线"分开。挂 `core2_board_port_a()`,init 可重复调用支持热插拔。

**超声波采样 = 单任务流水线**(不阻塞 30Hz game_task):每 `SONIC_READ_TICKS=3` 帧读一次上次触发的结果、立刻重触发(3×33≈99ms ≥ 测量周期);从不 delay 等测量。

## 🔴 桌面玩法省电坑(单元玩法通用变体)

机身不动 ≠ 没人玩——超声波距离变化 > `SONIC_MOVE_MM=12mm` = "手在动" → `core2_sleep_kick`(否则挥手玩着也会打盹)。NAP 时 5V 还在、继续轮询、挥手可唤醒;DEEP 切 5V → 单元断电复位,唤醒后 `unit_attach` 重接管(拿起机身才醒)。容错:没插单元 = 无字提示卡(超声波方块+插头图)+ 2s 重试,插上即"你好"接管;连续 `ERR_STREAK_LOST=20` 次 I2C 读失败判拔线回提示卡。

## 待实机标定(tuning.h,主要是幼儿手臂距离)

`NEAR_MM=60`/`FAR_MM=450`(嘴/音高映射区间)、`EAT_MM=90`/`EAT_EXIT_MM=140`(吃区边沿,须 EXIT>EAT)、`DIST_ALPHA=0.35`(去抖)、`SONIC_MOVE_MM=12`(唤醒灵敏度)、`PITCH_STEPS=8`/`TONE_MS=150`/`TONE_AMP=55`(空气琴)。

## launcher 图标

launcher 加 feed_monster 专属图标分支(薄荷圆脸+大张嘴+头顶饼干,**要重刷 launcher 才显示**;不刷也能玩、显示通用笑脸)。

## 状态

✅ **build 通过**(app 0x99360 ≈ 628KB,槽内余 60%,esp-idf v6.0 命令行);
⏳ **待烧录实机**:插超声波"手远近→嘴/音高/灯即时变"手感、吃区距离标定、打盹-挥手唤醒、深度省电-拿起唤醒-单元重接管、拔线提示卡、电源键回 launcher。
