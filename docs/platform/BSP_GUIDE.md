# BSP_GUIDE.md —— 平台外设层复用指南(新应用必读)

> 本工程的核心价值:**Core2 v1.0 + M5GO Bottom2 的外设层已实机调通一次,沉淀为
> `components/` 下的可复用组件**。做新应用 = **在 `apps/` 下新建工程**(可用
> `tools/new_app.sh` 脚手架生成骨架,或参考已有评估 app `apps/unit_bench`/
> `apps/power_lab`),共享仓库根的 `components/`、`partitions.csv`、
> `sdkconfig.platform`;编好的 bin 单刷进自己的 ota 评估槽,由 factory 分区的
> **launcher** 选择启动(机制见 `components/app_slot/README.md`,偏移见
> `tools/flash_map.md`)。板级事实见 `Core2_v1_0.md` / `M5GO_Bottom2.md`(勿改);
> 本文讲"怎么复用、顺序为什么、坑在哪"。

## 1. 分层架构

```
launcher/                 factory 分区常驻的评估台选择页(上电入口,数据驱动渲染)
apps/<app>/main/          应用逻辑(每个 APP 独立工程:状态机/评估流程/渲染/反馈编排/调参)
────────────────────────────────────────────────────────────────────
components/app_slot       多 App 分区自举(launch/回 launcher/app_slot_info 数据驱动)
components/core2_board    ★ 平台一键 bring-up(固化初始化顺序,新应用从这里开始)
components/core2_power    AXP192 直控:M-Bus 5V(EXTEN)/ 背光真开关(DCDC3)/ 寄存器读原语
components/power_monitor  AXP192 遥测:电池/VBUS 电压电流、充电状态(+库仑计,视查证结果)
components/kv_store       NVS 封装(标定/设置持久化,每 app 一个 namespace)
components/ui_kit         评估台 UI 控件:状态栏/数值卡/chart/列表菜单(守渲染红线)
components/data_log       串口 CSV 导出(自动时间戳),SPIFFS 离线录制(规划中)
components/imu_mpu6886    MPU6886 三轴加速度(0x68,复用 BSP I2C)
components/ledstrip_fx    底座灯带效果引擎(基础模式 + 瞬态特效,40fps 后台任务)
components/audio_fx       音效引擎(程序化合成 + 自定义音序,防爆音纪律内置)
components/haptics        震动模式库(事件队列,非阻塞)
components/motion_detect  "机身有没有被动过"检测(纯逻辑:帧间差 + 唤醒去抖)
components/core2_sleep    两级省电编排器(打盹→深度省电→去抖唤醒,可 force_stage 手动驱动)
components/units          外接单元驱动(8Encoder/超声波/DLight/手势/RGB/Chain/unit_probe)
────────────────────────────────────────────────────────────────────
managed_components/       espressif/m5stack_core_2(AXP192/LCD/触摸/LVGL/喇叭)+ led_strip 等
```

- 反馈类组件共用一套跨应用反馈词汇表:`hello / bump(轻中重) / near / collect / win / wake`
  ——三通道(声/震/灯)同名对应,评估 app 按需编排(不强制每个事件都四通道齐鸣,详见
  `CLAUDE.md` §5)。
- 所有组件**事件驱动、非阻塞**:play/trigger 只投队列,绝不拖慢应用主循环。

## 2. 新应用最小骨架

```c
#include "app_slot.h"
#include "core2_board.h"

void app_main(void)
{
    app_slot_return_to_factory();   // 第一行:之后任何复位/崩溃都回 launcher

    core2_board_cfg_t cfg = CORE2_BOARD_CFG_DEFAULT;  // 全开;不用的外设置 false
    esp_err_t err = core2_board_init(&cfg);
    if (err != ESP_OK) {
        // 可观测优先:init 失败一律屏上显式呈现(错误码+原因),不静默(CLAUDE.md §2 原则 2)
        return;
    }

    // 评估台目前没有回 launcher 的软件入口(电源键触发的软件退出 2026-07-09 已取消,
    // 电源键唯一剩下的行为是 AXP192 硬件本身按住 ≥4s 强制断电);需要页内"返回"按钮
    // 自行加一个调 app_slot_return_to_factory() + esp_restart() 的按钮即可。

    // ↓ 从这里写你的评估逻辑(屏用 LVGL,记得 bsp_display_lock/unlock;数值/图表控件见
    //   components/ui_kit/README.md)
    audio_fx_play(SND_HELLO);
    haptics_play(HAPTIC_HELLO);
    ledstrip_fx_set_base(LED_BASE_AMBIENT);
}
```

工程骨架(`tools/new_app.sh <名字>` 可自动生成,或照抄已有评估 app):
`apps/<名字>/CMakeLists.txt` 用 `SDKCONFIG_DEFAULTS` 引入共享 `../../sdkconfig.platform`
+ 本地 `sdkconfig.defaults`(内容只有分区表相对路径),`EXTRA_COMPONENT_DIRS` 指向
`../../components`;`main/CMakeLists.txt` 的 `REQUIRES` 加 `core2_board`、`app_slot`
(+ 直接用到的组件)(**PSRAM、`CONFIG_BSP_PMU_AXP192=y`、1kHz tick 是平台前提,别删**)。
🔴 烧录只许单刷自己的 ota 槽(`tools/flash_map.md`),**严禁 `idf.py flash`(覆盖 launcher)**。

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

## 5. 评估台 UI 可读性基线(应用层职责,组件不强制)

- 状态栏 14~16px、数值卡核心数字 24px,数值必须带单位(CLAUDE.md §8);
- 无全屏快速频闪;告警提示用告警色常亮/慢闪,不用快闪抓注意力;
- 默认仍做 idle 两级省电(电池只有底座 500mAh):**直接用 `core2_sleep` 组件**
  (主循环每帧喂加速度,打盹→深度省电→唤醒全托管;用法见其 README)。桌面评估场景(单元
  被操作但机身不动)记得 `core2_sleep_kick()` 防误打盹(CLAUDE.md §10);功耗评估类 app
  可用 `core2_sleep_force_stage()` 手动驱动休眠阶段做演练。

## 6. 每个组件的详细文档

各组件目录下 `README.md`:职责、坑、API 示例、前置条件。改组件前先读对应 README
和 `AGENTS.md`(查 MCP 再动手)。
