# core2_sleep —— 两级省电编排器(打盹 → 深度省电 → 去抖唤醒)

电池只有底座 500mAh,幼儿应用**必做** idle 省电。本组件把实机调通的整套编排固化成
一个可复用状态机,应用主循环每帧喂一次加速度即可。

```
AWAKE ──(可打盹状态静止 12s)──► NAP 打盹 ──(再静止 60s)──► DEEP 深度省电
  ▲                                │                            │
  └────────(连续 3 帧明显动作,去抖唤醒 + 恢复供电)◄─────────────┘
```

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
    vTaskDelayUntil(&last, pdMS_TO_TICKS(delay_ms));   // DEEP 时自动降频
}
```

配套 API:`core2_sleep_wake()`(触摸等非 IMU 活动立即唤醒)、`core2_sleep_kick()`
(切关/交互事件清静止计时)、`core2_sleep_set_awake_brightness()`(家长菜单改亮度,
休眠中改也会在唤醒时生效)、`core2_sleep_motion()`(读最近一帧机身动作量,应用可
复用做自己的稳定判据,如 IMU 校准前等静止)、`on_stage_change` 回调(应用换
"睡着/伸懒腰"画面用)。

## 配置要点

- `manage_bus_5v=false`:若 M-Bus 5V 还带着灯带之外的外设,深度省电就别切 5V;
- `manage_leds=false`:不用灯带的应用;
- 前置条件:`core2_power_init` 已完成(`core2_board_init` 已代管)。

判据细节见 `motion_detect/README.md`,电源位细节见 `core2_power/README.md`,
完整应用示例见 `main/game_state.c`,机制溯源见 `CLAUDE.md` §20.6。
