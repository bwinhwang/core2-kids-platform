# unit_gesture —— Unit Gesture(U127)最小驱动

I2C 3D 手势识别传感器,芯片 **PAJ7620U2**,I2C 地址固定 **0x73**,有效识别距离 5~15cm,9 种
内置手势。硬件事实唯一出处:`docs/units/Unit_Gesture.md`(接线、供电、识别距离)。

## 用法

```c
#include "core2_board.h"
#include "unit_gesture.h"

// 挂 PORT.A 外接总线(G32/G33,I2C_NUM_0);addr 传 0 = 默认 0x73
// init 会校 PART ID(0x00/0x01)+ 灌 219 组出厂寄存器表(bank0+bank1)+ 选回 bank0
esp_err_t err = unit_gesture_init(core2_board_port_a(), 0);

gesture_event_t g;
if (unit_gesture_read(&g) == ESP_OK && g != GESTURE_NONE) { /* 挥了一下 */ }
// 返回 TIMEOUT/INVALID_RESPONSE = I2C 读失败 = 拔线/断电 → 判单元丢失
```

按 `GESTURE_POLL_MS`(建议 30ms)周期轮询即可。`init` 可重复调用:没插/没电时返回错误,
应用低频重试实现"插上即生效"(深度省电断 5V 后单元复位,唤醒重调 `init` 即重新配好)。

## 坑 / 注意

- 🔴 **寄存器级事实全部来自厂商参考驱动源码**(Seeed_Studio/Grove_Gesture +
  DFRobot/DFRobot_PAJ7620U2),`unit_gesture.h` 头注释有完整出处与逐条核实记录,
  **不是编造**(AGENTS.md §1 铁律)。
- 🔴 **手势→比特位映射存在两份参考驱动互不一致的已知分歧**(不同物理封装/丝印方向导致):
  本驱动采用 DFRobot 的映射(RIGHT=bit0/LEFT=bit1/UP=bit2/DOWN=bit3/FORWARD=bit4/
  BACKWARD=bit5/CW=bit6/CCW=bit7,WAVE 在另一寄存器 bit0),依据是 **M5Stack 官方
  Unit Gesture(本平台购买的确切型号 U127)示例代码明确依赖 DFRobot_PAJ7620U2 库**。
  若实机测试发现方向感觉"反了"(如挥右识别成挥左),改 `unit_gesture.c` 的
  `unit_gesture_read()` switch 分支,不要碰初始化表。
- **无独立"物体存在但未分类"信号**:两份参考驱动的手势读取路径都只读 0x43/0x44 这两个
  "已分类手势"结果寄存器;芯片另有 PS_APPROACH_STATE(0x6B)等接近感应寄存器,但那是
  另一套需要额外校准的模式,默认手势模式初始化表不保证其语义有效,故未使用。
  `apps/magic_wand` 的 SHIMMER 微光回应因此走固定低频 idle ping 的退化方案,不依赖
  本驱动判断"有动静但没分类出手势"。
- **两笔独立事务**:读固定走"写寄存器号(STOP)+ 单独发起读",不用
  `i2c_master_transmit_receive`(repeated-start 组合读),与 unit_dlight/unit_ultrasonic
  同一惯例(即使 PAJ7620U2 是硬件寄存器 ASIC、本可安全组合读)。
- 🔴 **插口位置:红色 PORT.A 在 Core2 机身侧面**(Bottom2 黑口 PORT.B/蓝口 PORT.C 不是
  I2C)。插错口 = NACK/超时;先 `core2_board_port_a_scan()` 一扫便知。
- 🔴 **单元吃 PORT.A 5V = M-Bus 5V(EXTEN)**:电池供电时 EXTEN 没开单元没电(插 USB 会
  掩盖);`core2_board_init(enable_leds=true)` 已代开。**深度省电切 5V 后单元断电复位**
  (bank/寄存器配置丢失),恢复供电重调 `init` 会重灌整套初始化表。
- **只输出已分类的 9 种离散手势事件,不是连续测距**;不要当摇杆/触摸用,也别指望挖速度/时长。
