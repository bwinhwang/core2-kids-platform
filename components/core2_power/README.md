# core2_power —— AXP192 直控(BSP 不管的两路电源)

固化 Core2 v1.0 两个**症状酷似硬件损坏**的实机踩坑(2026-07-01 排查定案):

## 坑 1:底座灯带全黑 ≠ 灯带坏

Bottom2 灯带吃 M-Bus 5V(Core2 的 SY7088 升压),使能挂 **AXP192 EXTEN(REG 0x12 bit6)**。
官方 BSP 从不开 EXTEN → 灯带没供电全黑;此时数据线 G25 照常翻转、`led_strip_refresh`
照样返回 `ESP_OK`,极易误判驱动/硬件问题。→ `core2_power_bus_5v(true)`。

## 坑 2:`bsp_display_brightness_set(0)` 不熄屏

BSP 亮度只调 DCDC3 **电压**(0% 仍 ~2.95V,屏有微光)。真黑屏必须断 **DCDC3 使能
(REG 0x12 bit1)** → `core2_power_backlight(false)`。恢复时先开使能再设亮度。

## 用法

```c
core2_power_init(bsp_i2c_get_handle());  // ⚠️ 必须在 bsp_display_start() 之后
core2_power_bus_5v(true);                // 用灯带前开;深度省电时关(省 SY7088 静态电流)
core2_power_backlight(false);            // 深度省电真黑屏;唤醒时 true + 恢复亮度
```

## 实现纪律

- AXP192 各使能位挤在同一寄存器(REG 0x12 同管 DCDC1/3、LDO2/3、EXTEN),
  **必须读-改-写**,整字节覆盖会误关屏电/喇叭电。
- 初始化只做绑定 + GPIO0 LDOio 3.3V 一次性准备,不隐式改电源状态。

寄存器出处:`docs/platform/Core2_v1_0.md` §2;休眠联动示例:`main/game_state.c`。
