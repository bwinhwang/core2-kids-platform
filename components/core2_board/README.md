# core2_board —— 平台一键 bring-up(新应用从这里开始)

Core2 v1.0 + M5GO Bottom2 全部板载/底座外设的**总初始化入口**。价值 = 把实机调通的
**初始化顺序知识**固化成代码,新应用一行起平台,不必重踩坑。

## 用法(新应用 app_main 骨架)

```c
#include "core2_board.h"

void app_main(void)
{
    core2_board_cfg_t cfg = CORE2_BOARD_CFG_KIDS_DEFAULT;  // 全开、低亮(60%/灯带≤48)
    ESP_ERROR_CHECK(core2_board_init(&cfg));
    // 之后:屏(LVGL)/灯带/喇叭/震动/IMU 全部就绪,直接写你的应用
}
```

不用的外设置 `enable_* = false` 即可(运行时裁剪;硬件固定,无需 Kconfig)。

## 固化的初始化顺序(为什么不能乱)

| 步 | 动作 | 顺序原因 |
|---|---|---|
| 1 | `bsp_i2c_init` | AXP192/触摸/RTC/IMU 全挂内部 I2C(G21/G22) |
| 2 | `bsp_display_start` + 背光 | BSP 做 AXP192 基础配置,**会重写 REG 0x12** |
| 3 | I2C 自检扫描 | 顺带确认底座 IMU(0x68)在位 |
| 4 | `core2_power_init` | 必须在步 2 之后,否则 EXTEN/DCDC3 位被 BSP 清掉 |
| 5 | 灯带:先 `core2_power_bus_5v(true)` 再 `ledstrip_fx_init` | 先供电后驱动;反了=数据在跑灯全黑 |
| 6-8 | 喇叭 / 震动 / IMU | IMU 复用内部 I2C 句柄,勿自建总线(会抢总线) |

## PORT.A 外接 I2C(UNIT 单元)

`core2_board_port_a()` 返回 PORT.A(G32=SDA / G33=SCL)的 `i2c_master_bus_handle_t`,
懒加载,首次调用创建。外接 UNIT(8Encoder 0x41 / DLight 0x23 / 超声波 0x57 / 手势 0x73)
全挂这条,与内部总线物理隔离。**端口分配:内部=I2C_NUM_1(CONFIG_BSP_I2C_NUM),PORT.A=I2C_NUM_0。**

🔴 **坑:PORT.A 的 5V 来自 M-Bus 5V(EXTEN)** ——插 USB 时 VBUS 直通掩盖问题,
拔线用电池时 EXTEN 没开单元就断电("插电脑能玩、拔线失灵")。`core2_board_init`
(enable_leds=true)已代开;深度省电切 5V 时 PORT.A 单元同样断电、恢复后单元内部
状态(如 8Encoder 的 LED/计数)已复位,应用要自己重建。

## 失败语义

- 返回 `ESP_ERR_NOT_FOUND` = 没扫到 0x68 → **Bottom2 底座没接**(IMU/电池都来自底座)。
- 其余错误码来自对应外设 init,均有显式 `ESP_LOGE`,不静默。

各外设组件可脱离本组件单独使用;板级事实见 `docs/platform/`,复用总览见
`docs/platform/BSP_GUIDE.md`。
