# haptics —— 震动马达模式库(AXP192 LDO3,经 BSP feature)

事件驱动后台任务:`haptics_play()` 只投队列立即返回,**绝不阻塞调用方**。
供电(LDO3)由 BSP `bsp_feature_enable(BSP_FEATURE_VIBRATION, …)` 管,无需碰寄存器。

## 模式词汇表(短而不腻,与 audio_fx/ledstrip_fx 对应)

| 模式 | 节奏 | 语义 |
|---|---|---|
| `HAPTIC_HELLO` / `HAPTIC_WAKE` | 一下轻震 | 开机 / 唤醒 |
| `HAPTIC_BUMP_LIGHT/MED/HARD` | ~30/60/100ms 单次 | 碰撞力度三档 |
| `HAPTIC_COLLECT` | ~25ms 极短 | 收集 |
| `HAPTIC_WIN` | 80ms ×3 连震 | 达成/过关 |

## 用法

```c
haptics_init();                    // core2_board_init 已代管
haptics_play(HAPTIC_BUMP_MED);     // 非阻塞
haptics_set_enabled(false);        // 家长总开关(夜间关震动)
```

设计约束:幼儿应用震动**短促、分级、不连续轰炸**;新模式在 `haptics.c` 的模式表加行即可。
