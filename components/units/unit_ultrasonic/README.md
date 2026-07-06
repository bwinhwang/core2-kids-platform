# unit_ultrasonic —— Unit Ultrasonic-I2C(U098-B1)最小驱动

I2C 超声波测距,芯片 **RCWL-9620**,I2C 地址默认 **0x57**,量程 2cm~450cm。硬件事实唯一
出处:`docs/units/Unit_Ultrasonic_I2C.md`(接线、供电、测量周期)。

## 用法

```c
#include "core2_board.h"
#include "unit_ultrasonic.h"

// 挂 PORT.A 外接总线(G32/G33,I2C_NUM_0);addr 传 0 = 默认 0x57
esp_err_t err = unit_ultrasonic_init(core2_board_port_a(), 0);

// 非阻塞用法:触发 → 等 ≥ 一个测量周期(~50ms,官方库用 120ms 保守)→ 读回
unit_ultrasonic_trigger();
vTaskDelay(pdMS_TO_TICKS(100));
float mm;
if (unit_ultrasonic_read_mm(&mm) == ESP_OK) { /* 有目标,mm 为距离 */ }
// 返回 ESP_ERR_NOT_FOUND = 越界/无回波 = "没有目标"(手离开量程,单元仍在);
// 其余错误(TIMEOUT/INVALID_RESPONSE)= I2C 读失败 = 拔线/断电 → 判单元喪失
```

建议放**独立采样任务**(trigger→delay→read 循环缓存最新值),游戏主循环读缓存即可,
不阻塞 60/30Hz 的 game_task。`init` 可重复调用:没插/没电时返回错误,应用低频重试实现
"插上即生效"。

## 坑 / 注意

- 🔴 **读写是两笔独立事务**:触发 `i2c_master_transmit({0x01},1)`,读 `i2c_master_receive(buf,3)`;
  **不用 `i2c_master_transmit_receive`(repeated-start)**——与官方 Arduino/UIFlow 库一致,
  也避开 §20.14 那类 MCU 从机组合读钳死总线的历史坑。
- **触发后必须等够测量周期(~50ms)再读**,否则读到的是上一次结果(§docs 明确)。
- **数据格式**:读回 3 字节,24-bit **大端**(b[0]=高字节),单位**微米**,mm = raw/1000。
  越界([20mm,4500mm] 外)= 无有效回波,驱动返回 `ESP_ERR_NOT_FOUND`,应用当"没目标"
  (与真正的通信错误 TIMEOUT/INVALID_RESPONSE 区分开,后者才判拔线)。
- 🔴 **插口位置:红色 PORT.A 在 Core2 机身侧面**,不在底座上(Bottom2 黑口 PORT.B/蓝口
  PORT.C 不是 I2C)。插错口 = NACK(`ESP_ERR_INVALID_RESPONSE`);先 `core2_board_port_a_scan()` 一扫便知。
- 🔴 **单元吃 PORT.A 5V = M-Bus 5V(EXTEN)**:电池供电时 EXTEN 没开单元没电(插 USB 会掩盖);
  `core2_board_init(enable_leds=true)` 已代开。**深度省电切 5V 后单元断电复位**,恢复供电重调 `init`。
- 近距 2cm 内盲区、指向角 60° 较宽、软/吸声目标(布/海绵)回波弱读数跳变——见 docs §5。
- 是 **I2C 从机**,不是 HC-SR04 那种 GPIO trig/echo;不占两个 GPIO,只挂 I2C 总线。
