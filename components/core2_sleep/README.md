# core2_sleep —— 两级省电编排器(打盹 → 深度省电 → 去抖唤醒)

电池只有底座 500mAh,评估 app **默认仍做** idle 省电(功耗类评估 app 例外,可接管)。
本组件把实机调通的整套编排固化成一个可复用状态机,应用主循环每帧喂一次加速度即可。

```
AWAKE ──(可打盹状态静止 12s)──► NAP 打盹 ──(再静止 60s)──► DEEP 深度省电
  ▲                                │                            │
  └────────(连续 3 帧明显动作,去抖唤醒 + 恢复供电)◄─────────────┘
                                                                │
                                          (再耗 10min)──► **自动关机**(AXP192 断电)
```

## 🔴 为什么 DEEP 之后必须真关机(2026-08-12 补)

DEEP 省的**全是外设电**:屏、灯带、M-Bus 5V。主控自始至终 160MHz 全速跑
(`CONFIG_PM_ENABLE` 没开,没有动态调频/tickless/light sleep),LDO2(屏逻辑电)、
PSRAM、NS4168 功放也都一直供着。所以 DEEP **不是省到底的状态,只是省得最多的状态**
——停在那儿,500mAh 电芯会一路耗到放空(还每次都是深放电)。主控侧那几条为什么还没做、
各自代价多大,见 `docs/ROADMAP.md` §8。

`deep_shutdown_ms`(默认 10min)是没上 light sleep 的前提下**唯一能把待机功耗归零**的
手段。关机走 `core2_power_shutdown()`(AXP192 `0x32` bit7),之后按电源键重新开机。
另有 `crit_bat_mv`(默认 3300mV)在任何阶段都生效,防电芯深放电。

两条策略都在 **`usb == true`(插着 USB)时整个跳过** —— 插电时既在充电又多半在开发,
关机没意义还碍事(`idf.py monitor` 挂着看日志时不会突然断电)。两个字段的约定:
**0 = 用默认值,负数 = 关闭该策略**(0 在这里是个危险的合法值,所以留了负数这条退出口)。

## 固化的时序知识(每步顺序都是实机结论,勿手工重排)

- **进深度省电**:亮度 0(先降 DCDC3 电压)→ `core2_power_backlight(false)`(断使能,
  **brightness 0% 不熄屏**)→ 灯带 OFF → 切 M-Bus 5V(断灯带 + SY7088 静态电流)
  → feed 返回值变 120ms(轮询降频,少唤醒 CPU)。
- **唤醒**:恢复 5V → 重启 DCDC3 → 恢复亮度 → 灯带回常态 →(可选)轻震。
- **判据**(来自 `motion_detect`):"没人玩"只看机身动作量,绝不用应用量(球速/游标)
  ——会永不打盹;"真的动了"必须连续 3 帧去抖——否则单帧噪声作废深度省电计时。

## 用法(60Hz 主循环)

```c
static core2_sleep_t sl;
core2_sleep_init(&sl, NULL);          // NULL = CORE2_SLEEP_CFG_DEFAULT(实机定案值)
                                      // 自定义:从 CORE2_SLEEP_CFG_DEFAULT 改起,别漏 bool 字段
for (;;) {
    bool have = (imu_mpu6886_read_accel(&a) == ESP_OK);
    int delay_ms = core2_sleep_feed(&sl,
                       have ? (float[]){a.x, a.y, a.z} : NULL,
                       in_gameplay && have);      // 只有"正玩着"的状态允许累计静止
    if (core2_sleep_stage(&sl) == CORE2_SLEEP_AWAKE) {
        // ...应用逻辑/渲染...
    }
    core2_sleep_pace(&last, delay_ms);    // 帧节拍(DEEP 时自动降频);🔴 别裸用 vTaskDelayUntil
}
```

### 🔴 帧节拍必须走 `core2_sleep_pace()`(2026-08-18,chick_pour 实证)

`xTaskDelayUntil` 只把 `last` 加一个周期,**截止时刻早已过去也照样立刻返回,迟到的
时间一分不减**(IDF v6.0 `tasks.c`:`*pxPreviousWakeTime = xTimeToWake` 无条件写)。
渲染重的一帧、唤醒时的电源切换、LVGL 锁竞争都会让单帧超过 `frame_ms`,裸用
`vTaskDelayUntil` 就把这些迟到攒成**没有上限的时间欠债**;之后画面一变便宜(对象
变少 / 场景切完 / 打盹醒来),循环便零延时连跑几百上千帧还债 —— 物理 dt 通常写死
1/60,墙钟上就是「**玩到一半全场对象突然集体加速**」,持续几秒后自己恢复。速度封顶
拦不住它(封的是仿真速度,不是每秒帧数)。

`core2_sleep_pace()` 逾期即重锚 `last`(迟到时间直接丢掉,宁可慢不许快进),并按 5 秒
窗口打一行 `W core2_sleep: 帧超时丢帧 N 次/5 秒` —— 丢帧画面上只是略顿,不打日志就
永远不知道帧预算到底超没超。**该日志偶发正常**(派对整屏重画、唤醒那一帧);长期
持续偏大 = 该 app 的帧预算(根 `CLAUDE.md` §6.2)真的超了,回去减脏矩形。

配套 API:`core2_sleep_wake()`(触摸等非 IMU 活动立即唤醒)、`core2_sleep_kick()`
(切关/交互事件清静止计时;桌面评估场景——单元被操作但机身不动——务必调用,否则
评估中打盹)、`core2_sleep_set_awake_brightness()`(运行时改亮度,休眠中改也会在唤醒
时生效)、`core2_sleep_motion()`(读最近一帧机身动作量,应用可复用做自己的稳定判据,
如标定前等静止)、`on_stage_change` 回调(应用换状态画面用)。

**`core2_sleep_force_stage(&sl, CORE2_SLEEP_NAP/DEEP/AWAKE)`**(2026-07-17 新增):手动
跳到目标阶段,复用组件内固化的同一套顺序(直接调用文件内 `enter_nap`/`enter_deep`/
`do_wake`),不必等真的静止 12s/60s。给 `power_lab` 做休眠演练用——**不许 app 自己
散装重拼这套亮度→DCDC3→灯带→5V 顺序**。

## 配置要点

- `manage_bus_5v=false`:若 M-Bus 5V 还带着灯带之外的外设,深度省电就别切 5V;
- `manage_leds=false`:不用灯带的应用;
- `deep_shutdown_ms` / `crit_bat_mv`:见上一节(0=默认,负数=关闭);
- 前置条件:`core2_power_init` + `power_monitor_init` 已完成(`core2_board_init` 已代管;
  电量读不到时两条关机策略自动不生效,只是回到"永不关机"的老行为)。

## DEEP 里到底还耗多少?怎么量

DEEP 期间每 10s 打一条 `DEEP <秒>s:电池 xxxxmV(~xx%)±xxmA` 日志。
🔴 **但插着 USB 量不到真数**:VBUS 在位时系统吃 USB 的电、电池转充电,放电电流恒 ~0。
真实测法是拔 USB 静置,靠**屏上电量指示**读静置前后的电压差(launcher 右上角有,
clock_turn 信息区也有),`ΔmV / 时长` 即耗电速率。

判据细节见 `motion_detect/README.md`,电源位细节见 `core2_power/README.md`,
完整应用示例见 `main/game_state.c`,机制溯源见 `CLAUDE.md` §20.6。
