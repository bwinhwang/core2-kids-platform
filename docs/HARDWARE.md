# HARDWARE.md —— 本应用装配清单(BOM + 接线)

**平台(固定)**:M5Stack Core2 v1.0 + M5GO Bottom2
→ 板级真值见 `docs/platform/Core2_v1_0.md` 与 `M5GO_Bottom2.md`(平台常量,勿改)。
→ 引脚/地址常量在 `components/bsp_core2/include/platform_pins.h` + `bsp_port.h`。

## 接入的 UNIT 外设
> 外接 I2C UNIT 走 **PORT.A(G32/G33)**,勿接内部总线;地址勿撞 0x34/0x38/0x51/0x68。
> 接上后先 `i2c_scan(bsp_i2c_port_a())` 自检地址。

| UNIT | 接口 | 端口 | I2C 地址 | 组件 | 文档 |
|---|---|---|---|---|---|
| _(示例)超声波_ | I2C | PORT.A | 0x57 | `unit_ultrasonic` | `docs/units/ultrasonic.md` |
| | | | | | |

## 板载能力启用情况(`idf.py menuconfig` →「Core2 BSP」)
- [x] SK6812 灯条
- [x] NS4168 音频
- [x] ILI9342C 屏
- [x] FT6336U 触摸 ⚠️ 未上板验证

## 待上板核对
- [ ] `i2c_scan(bsp_i2c_internal())` 应见 0x34 / 0x38 / 0x51 / 0x68
- [ ] LED 物理顺序 → 填 `platform_pins.h` 的 `BSP_LED_LAYOUT`
- [ ] 各 UNIT 地址不冲突
