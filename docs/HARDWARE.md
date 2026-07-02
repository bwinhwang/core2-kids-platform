# HARDWARE.md —— 本应用装配清单(BOM + 接线)

**平台(固定)**:M5Stack Core2 v1.0 + M5GO Bottom2(**硬依赖**:IMU 与电池来自底座)
→ 板级真值见 `docs/platform/Core2_v1_0.md` 与 `docs/platform/M5GO_Bottom2.md`(平台常量,勿改)。

## 本应用用到的硬件通道(全部板载/底座,无外接 UNIT)

| 通道 | 器件 | 接口 / 引脚 | 备注 |
|---|---|---|---|
| 倾斜输入 | MPU6886(来自 Bottom2) | 内部 I2C G21/G22 @ 0x68 | WHO_AM_I=0x19;**不是 BMI270** |
| 屏 | ILI9342C 320×240 | SPI(BSP 管) | 官方 BSP ili9341 兼容驱动 + LVGL |
| 触摸 | FT6336U | 内部 I2C @ 0x38,INT=39 | 仅家长菜单长按用 |
| 音频 | NS4168 | I2S BCLK=12/LRCK=0/DATA=2 | SPK_EN=AXP192 IO2,BSP 管 |
| 震动 | 马达 | AXP192 LDO3 | `haptics` 组件 |
| 灯带 | 10×SK6812(Bottom2) | 数据 G25,RMT | **供电吃 M-Bus 5V,须开 AXP192 EXTEN**(`main/power.c`) |
| 电源 | AXP192 @ 0x34 | 内部 I2C | 背光=DCDC3;深度省电断 DCDC3 使能 + 切 EXTEN |

> 内部 I2C 地址占用:0x34(AXP192)/ 0x38(FT6336U)/ 0x51(BM8563)/ 0x68(MPU6886)。
> G25 被灯带占用,不能再当 DAC;G0/G2/G12 是 strapping(音频占用),勿动。

## 外接 UNIT 外设

无(PORT.A/B/C 空闲;若将来扩展,参考 `docs/units/`,I2C UNIT 走 PORT.A G32/G33,地址勿撞上表)。

## 实机验证状态

- [x] I2C 扫到 0x68,WHO_AM_I=0x19;屏/声/灯/震动全通(2026-07-01,原机)
- [x] 轴映射定案:`TILT_INVERT_X=1, TILT_INVERT_Y=0, TILT_SWAP_XY=0`(`main/tuning.h`)
- [x] 灯带 EXTEN、背光 DCDC3 两坑已解(详见 `CLAUDE.md` §17/§20.6)
- [x] 迁移版固件烧入本机板子,无异常、按预期工作(2026-07-02,用户手动烧录——WSL 无烧录条件)
