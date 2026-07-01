# CLAUDE.md —— __PROJECT__ 项目规范(应用特定)

> 通用 AI 协作规范见 `AGENTS.md`;平台硬件事实见 `docs/platform/`;UNIT 硬件事实见
> `docs/units/`。**本文件只放本应用特定内容**(产品目标、交互设计、待验证项)。

## 1. 产品目标
<面向几岁幼儿?玩法一句话?核心因果回路(输入→输出,即时/一致/封顶)?>

## 2. 硬件构成
- 平台:Core2 v1.0 + M5GO Bottom2(固定)
- UNIT 外设:见 `docs/HARDWARE.md`
- 启用的板载能力:灯 / 声 / 屏 / 触摸(在 menuconfig「Core2 BSP」裁剪)

## 3. 交互设计
- 输入:<IMU / 哪个 UNIT> → engine → intensity(连续)+ events(离散)
- 输出映射:
  - 灯效:<强度→颜色/亮度怎么变>
  - 音效:<强度/事件→什么声>
  - 屏 / 震动:<可选>

## 4. 安全红线(见 `components/app_core/include/kids_safety.h`,不可放宽)
- 音量封顶 `KIDS_MAX_VOLUME`(上板声级计 @25cm 实测,≲75dBA)
- 亮度封顶 `KIDS_MAX_BRIGHTNESS`
- 低电仍可玩,仅叠加提示

## 5. 待上板验证清单(无串口只能 build 时,逐条上板做)
- [ ] `i2c_scan` 确认板载 + 各 UNIT 地址
- [ ] LED 物理顺序填 `BSP_LED_LAYOUT`
- [ ] 音量声级计实测定 `KIDS_MAX_VOLUME`
- [ ] 触摸方向(若用 FT6336U,驱动未验证)
- [ ] 真人幼儿试玩,微调 `app_tuning.h` 的阈值/曲线
