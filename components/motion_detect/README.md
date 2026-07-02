# motion_detect —— "有没有人在玩"检测(纯逻辑,零硬件依赖)

帧间加速度变化量 + 双向计数(静止累计 / 唤醒去抖)。任何带 IMU 的应用做
**打盹/深度省电/唤醒**都直接复用。判据与阈值为 Core2+Bottom2 实机定案。

## 固化的两个踩坑(为什么这么设计)

1. **判"没人玩"只看机身动作量,不要看应用量(如球速/游标速度)**:
   机身平放但相对校准零点有残余倾斜时,应用量可能永不归零 → 永不打盹(实测)。
   机身动作量 = `|Δax|+|Δay|+|Δaz|`;平放噪声 ≈0.005~0.03g(尖峰偶达 0.08),拿起 >0.12g。
2. **判"真的动了"必须连续多帧去抖**:单帧噪声尖峰会误唤醒,把深度省电计时整段作废
   (实测 60s 几乎永远熬不满)。定案:连续 3 帧 >0.12g。

## 用法(60Hz 帧循环)

```c
static motion_detect_t md;
motion_detect_init(&md, 0, 0);                        // 0 = 用实测默认(0.12g / 3帧)

// 每帧:
motion_detect_feed(&md, have ? (float[]){ax,ay,az} : NULL);   // 读不到样本传 NULL

// 活跃状态(要计静止进打盹):
if (motion_detect_tick_still(&md) > IDLE_TIMEOUT_FRAMES) enter_idle();

// 打盹/深度省电状态(等唤醒):
if (motion_detect_tick_wake(&md)) wake_up();

// 状态切换时:
motion_detect_reset(&md);
```

配套的省电执行动作(关背光/切 5V)见 `core2_power`;完整休眠状态机示例见
`main/game_state.c` 与 `CLAUDE.md` §20.6。
