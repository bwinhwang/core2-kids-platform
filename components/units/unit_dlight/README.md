# unit_dlight —— Unit DLight(U136)最小驱动

I2C 环境光/照度传感器,芯片 **BH1750FVI**,I2C 地址固定 **0x23**,量程 1~65535 lx。硬件事实
唯一出处:`docs/units/Unit_DLight.md`(接线、供电、测量模式)。

## 用法

```c
#include "core2_board.h"
#include "unit_dlight.h"

// 挂 PORT.A 外接总线(G32/G33,I2C_NUM_0);addr 传 0 = 默认 0x23
// init 会发上电(0x01)+ 选连续高分辨率模式(0x10)并等首次测量(~180ms)
esp_err_t err = unit_dlight_init(core2_board_port_a(), 0);

// 连续模式:此后随时读回最近一次测量值(纯 receive,不必再发命令)
float lux;
if (unit_dlight_read_lux(&lux) == ESP_OK) { /* lux 为照度 */ }
// 返回 TIMEOUT/INVALID_RESPONSE = I2C 读失败 = 拔线/断电 → 判单元丢失
```

放**独立采样任务**每 ~130ms 读一次即可(BH1750 高分辨率测量周期 ~120ms,读更快只会拿到
同一个值)。`init` 可重复调用:没插/没电时返回错误,应用低频重试实现"插上即生效"。

## 坑 / 注意

- 🔴 **读写是两笔独立事务**:init 写命令 `i2c_master_transmit`,读 `i2c_master_receive(buf,2)`;
  **连续模式下读不必先写命令**,天然就不用 `i2c_master_transmit_receive`(repeated-start)。
- **上电后需一次"上电 + 选模式"命令才出数据**;直接裸读可能拿到旧值/0(§docs §5)。init 已代发。
- **数据格式**:读回 2 字节,16-bit **大端**(b[0]=高字节),照度 **lx = raw / 1.2**(高分辨率模式系数)。
- **强光/量程顶会饱和**在 ~54612 lx(0xFFFF/1.2)附近;设计阈值别指望绝对值,**用相对/自适应基准**更稳
  (peekaboo 就是自适应房间亮度做"捂住/松开"判定,见 apps/peekaboo)。
- 🔴 **插口位置:红色 PORT.A 在 Core2 机身侧面**(Bottom2 黑口 PORT.B/蓝口 PORT.C 不是 I2C)。
  插错口 = NACK(`ESP_ERR_INVALID_RESPONSE`);先 `core2_board_port_a_scan()` 一扫便知。
- 🔴 **单元吃 PORT.A 5V = M-Bus 5V(EXTEN)**:电池供电时 EXTEN 没开单元没电(插 USB 会掩盖);
  `core2_board_init(enable_leds=true)` 已代开。**深度省电切 5V 后单元断电复位(模式丢失)**,恢复供电重调 `init`
  会重发上电+选模式。
- **只测照度**,不是颜色/色温/UV;只有 I2C,无模拟输出、无中断脚。
