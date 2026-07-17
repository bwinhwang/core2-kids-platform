# power_monitor —— AXP192 遥测(电池/VBUS 电压电流、充电状态、库仑计)

给 `power_lab`(功耗/系统评估台)、`unit_bench` 等评估 app 提供"这台机器自己的功耗状态"。
只做**一次性读全量**,不做后台采样任务/历史环形缓冲(那是 app 层的事)。

## 用法

```c
core2_power_init(bsp_i2c_get_handle());  // 必须先于本组件(复用同一 AXP192 句柄)
power_monitor_init();                    // 开 ADC 通道 + 使能库仑计

power_telemetry_t t;
if (power_monitor_read(&t) == ESP_OK) {
    // t.batt_mv / t.batt_charge_ma / t.batt_discharge_ma / t.vbus_present /
    // t.vbus_mv / t.vbus_ma / t.charging / t.coulomb_mah
}

power_monitor_coulomb_reset();  // 需要重新起算续航估算时调
```

## 寄存器核实记录(AGENTS.md §1 铁律:本会话 Espressif MCP 不可用,退路是查厂商源码)

**Confirmed via `github.com/m5stack/M5Core2/blob/master/src/AXP192.cpp`**(2026-07-17 WebFetch 抓取):

| 遥测 | 寄存器 | 位宽 | ADCLSB | 换算 |
|---|---|---|---|---|
| 电池电压 | 0x78 | 12-bit | 1.1 mV | `raw * 1.1` mV |
| 电池充电电流 | 0x7A | 13-bit | 0.5 mA | `raw * 0.5` mA |
| 电池放电电流 | 0x7C | 13-bit | 0.5 mA | `raw * 0.5` mA |
| VBUS 电压 | 0x5A | 12-bit | 1.7 mV | `raw * 1.7` mV |
| VBUS 电流 | 0x5C | 12-bit | 0.375 mA | `raw * 0.375` mA |
| 电源状态 | 0x00 | 8-bit | — | bit5(0x20)=VBUS 在位,bit2(0x04)=正在充电 |
| 库仑计充电累加 | 0xB0 | 32-bit | — | `GetCoulombchargeData()` |
| 库仑计放电累加 | 0xB4 | 32-bit | — | `GetCoulombdischargeData()` |
| 库仑计控制 | 0xB8 | 8-bit | — | 0x80=使能 / 0xA0=清零 / 0xC0=停止 |
| ADC 使能 | 0x82 | 8-bit | — | 写 0xFF 开全部通道(`SetAdcState(true)`) |

库仑计 mAh 换算(同源码 `GetCoulombData()`):
```
mAh = 65536 * 0.5 * (charge_raw - discharge_raw) / 3600.0 / 25.0
```

**Confirmed via `github.com/m5stack/M5StickC/blob/master/src/AXP192.cpp`**(交叉核对字节序):
充电电流读法 `icharge = (buf[0]<<5) + buf[1]`——高字节在基址寄存器、低字节在 base+1,
13-bit = `(hi<<5)|lo`。12-bit 同理按 M5Unified(`src/utility/AXP192_Class.hpp`)确认的
电池电压读法(高字节 0x78、低 4 位 0x79&0x0F)类推为 `(hi<<4)|lo`——X-Powers AXP192
全系列 ADC 寄存器统一采用"高字节 + 低字节低若干位"布局,两份官方驱动源码交叉印证。

## 待查证 / 已知限制

- ⚠️ **库仑计 32-bit 寄存器字节序按大端序假设**,未逐字节核实到 `Read32bit()` 的函数体
  (M5Core2 源码只给出调用点 `Read32bit(0xB0)`)。若实机 `coulomb_mah` 读数明显不合理
  (如插着 USB 慢慢变成天文数字),优先怀疑这里,换成小端序或改用 `power_monitor_coulomb_reset()`
  勤复位规避。
- 内部温度 ADC(reg 0x5E,已核实存在但本组件不暴露)——评估台优先级更高的是电压/电流,
  需要时按同一套路补,不是查不到。
- 只测电池路和 VBUS 路,**无 per-rail 测量**(不能单独看背光/灯带/喇叭各自耗多少电,
  只能看总电流台阶,见 `apps/power_lab` 负载矩阵设计)。
- USB 插着时电池电流通常 ≈0,要看 VBUS 电流才有意义;真实续航测试必须拔 USB(见
  `CLAUDE.md` §11.2)。

## 依赖顺序

必须在 `core2_power_init()` 成功之后调用 `power_monitor_init()`(复用其内部绑定的
AXP192 I2C 设备句柄,通过 `core2_power_read_regs`/`core2_power_write_reg` 两个原语访问,
不二次 `i2c_master_bus_add_device`)。
