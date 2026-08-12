# power_monitor —— AXP192 电池 / USB 读数

平台原来**完全看不到电量**:既没法在低电时提示,也没法回答
"深度省电里到底还在耗多少电"。本组件把 AXP192 的电池 ADC 读出来。

## 用法

`core2_board_init` 已代调 `power_monitor_init()`(在 `core2_power_init` 之后),应用直接读:

```c
power_status_t st;
if (power_monitor_read(&st) == ESP_OK && st.bat_mv > 0) {
    // st.bat_mv / st.bat_ma(+充 -放)/ st.pct / st.usb / st.charging
}
```

## 寄存器出处

**Confirmed via github.com/m5stack/M5Core2 `src/AXP192.cpp` + `src/AXP.cpp`**:

| 寄存器 | 内容 | 换算 |
|---|---|---|
| `0x00` | 电源状态 | bit5 = VBUS 在位,bit2 = 充电中 |
| `0x82` | ADC 使能 1 | 写 `0xFF` 全开(整字节写安全:8 位全是 ADC 使能) |
| `0x78` | 电池电压 12bit | ×1.1 mV |
| `0x7A` | 充电电流 13bit | ×0.5 mA |
| `0x7C` | 放电电流 13bit | ×0.5 mA |
| `0x5A` | VBUS 电压 12bit | ×1.7 mV |

位拼法:12bit = `(hi<<4)|(lo&0x0F)`,13bit = `(hi<<5)|(lo&0x1F)`。
`0x78`~`0x7D` 连续,电压+双向电流一笔 I2C 读完。

## 坑

- 🔴 **插着 USB 时电流读数没有参考价值**:VBUS 在位 → 系统吃 USB 的电、电池转为充电,
  放电电流恒 ~0。**要量真实待机功耗必须拔 USB**,而拔了就没串口 —— 实用办法是读屏上
  的电压:静置前后各看一次,`ΔmV / 时长` 就是耗电速率。`core2_sleep` 的低电/久置关机
  也因此在 `usb == true` 时整个跳过(见其 README)。
- `pct` 是**开路电压查表**,不是库仑计;带载(尤其灯带亮着)时电压被拉低,读数偏保守,
  松手静置几秒会"涨回来"。别拿它做精确统计,它只回答"还剩大概几格"。
- `power_monitor_init` 失败(AXP192 没绑上)后,`power_monitor_read` 返回
  `ESP_ERR_INVALID_STATE` 且把结构体清零。**调用方应把 `bat_mv == 0` 当作"读不到"**,
  不要据此判低电关机。
