# BSP_GUIDE.md —— 平台外设层复用指南(新应用必读)

> 本工程的核心价值:**Core2 v1.0 + M5GO Bottom2 的外设层已实机调通一次,沉淀为
> `components/` 下的可复用组件**。做新应用 = 复制本工程 → 保留组件层 → 换掉 `main/`
> 里的应用逻辑。板级事实见 `Core2_v1_0.md` / `M5GO_Bottom2.md`(勿改);
> 本文讲"怎么复用、顺序为什么、坑在哪"。

## 1. 分层架构

```
main/                     应用逻辑(每个 APP 自己的:状态机/玩法/渲染/反馈编排/调参)
────────────────────────────────────────────────────────────────────
components/core2_board    ★ 平台一键 bring-up(固化初始化顺序,新应用从这里开始)
components/core2_power    AXP192 直控:M-Bus 5V(EXTEN)/ 背光真开关(DCDC3)
components/imu_mpu6886    MPU6886 三轴加速度(0x68,复用 BSP I2C)
components/ledstrip_fx    底座灯带效果引擎(基础模式 + 瞬态特效,40fps 后台任务)
components/audio_fx       音效引擎(程序化合成 + 自定义音序,防爆音纪律内置)
components/haptics        震动模式库(事件队列,非阻塞)
components/motion_detect  "有没有人在玩"检测(纯逻辑:帧间差 + 唤醒去抖)
components/core2_sleep    两级省电编排器(打盹→深度省电→去抖唤醒,时序知识固化)
────────────────────────────────────────────────────────────────────
managed_components/       espressif/m5stack_core_2(AXP192/LCD/触摸/LVGL/喇叭)+ led_strip 等
```

- 反馈类组件共用一套**幼儿反馈词汇表**:`hello / bump(轻中重) / near / collect / win`
  ——三通道(声/震/灯)同名对应,应用层编排时一一映射(参考 `main/feedback.c`)。
- 所有组件**事件驱动、非阻塞**:play/trigger 只投队列,绝不拖慢应用主循环。

## 2. 新应用最小骨架

```c
#include "core2_board.h"

void app_main(void)
{
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;  // 全开;不用的外设置 false
    ESP_ERROR_CHECK(core2_board_init(&cfg));

    // ↓ 从这里写你的应用(屏用 LVGL,记得 bsp_display_lock/unlock)
    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);
    ledstrip_fx_set_base(LED_BASE_AMBIENT);
}
```

`main/CMakeLists.txt` 的 `REQUIRES` 加 `core2_board`(+ 直接用到的组件);
`sdkconfig.defaults` / `partitions.csv` / `dependencies.lock` 原样保留
(**PSRAM、`CONFIG_BSP_PMU_AXP192=y`、1kHz tick 是平台前提,别删**)。

## 3. 初始化顺序(core2_board 已固化;绕过它手工初始化时必须遵守)

1. `bsp_i2c_init` —— AXP192/触摸/RTC/IMU 全挂内部 I2C(G21/G22)
2. `bsp_display_start` —— BSP 做 AXP192 基础配置,**会重写 REG 0x12**
3. `core2_power_init` —— 必须在 2 之后(否则 EXTEN/DCDC3 位被清)
4. 灯带:**先** `core2_power_bus_5v(true)` **再** `ledstrip_fx_init`
5. `audio_fx_init` / `haptics_init` —— 依赖 BSP 的 AXP 配置
6. `imu_mpu6886_init(bsp_i2c_get_handle())` —— 复用总线,勿自建

## 4. 坑位速查(全部实机踩过,症状 → 真因)

| 症状 | 真因(不是硬件坏) | 修法 |
|---|---|---|
| 灯带全黑,refresh 返回 OK | M-Bus 5V 没开(BSP 从不开 EXTEN) | `core2_power_bus_5v(true)` |
| 亮度 0 屏还有微光 | BSP 只降 DCDC3 电压不断电 | `core2_power_backlight(false)` |
| 屏黑/无声/无触摸 | AXP192 没初始化 / PMU Kconfig 选错 | `CONFIG_BSP_PMU_AXP192=y` |
| IMU 读不到 / WHO_AM_I≠0x19 | 没接 Bottom2(IMU 在底座);或当成 BMI270 写 | 接底座;按 MPU6886 手册 |
| 喇叭有底噪无音调 | I2S 声道/格式错(NS4168 监听右声道) | 走 BSP 音频;见 Core2_v1_0.md §3 |
| 音效带"咔哒" | 反复 toggle SPK_EN / 反复 open-close | 整局保持 open + 首尾淡入淡出 |
| 永不打盹 / 深度省电熬不满 | 拿应用量当活动信号 / 唤醒没去抖 | 用 `motion_detect`(见其 README) |
| 帧率崩 | 每帧整屏重绘(SPI 40MHz 整屏 ~31ms) | 静态层画一次 + 脏矩形(CLAUDE.md §9) |

## 5. 幼儿应用安全基线(应用层职责,组件不强制)

- 背光 ≤60%、灯带亮度上限 ≤48、音量留上限(声级计 ≲75dBA @25cm 实测);
- 无全屏快速频闪;撞击类反馈单次不连闪;
- 必做 idle 两级省电(电池只有底座 500mAh):**直接用 `core2_sleep` 组件**
  (主循环每帧喂加速度,打盹→深度省电→唤醒全托管;用法见其 README 与 `main/game_state.c`)。

## 6. 每个组件的详细文档

各组件目录下 `README.md`:职责、坑、API 示例、前置条件。改组件前先读对应 README
和 `AGENTS.md`(查 MCP 再动手)。
