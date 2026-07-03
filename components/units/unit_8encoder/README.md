# unit_8encoder —— Unit 8Encoder(U153)最小驱动

8 路旋转编码器 + 8 径向按键 + 1 拨动开关 + 9 颗 RGB(LED0~7=旋钮,LED8=开关)。
从机 MCU = STM32F030C8T6,I2C 地址默认 **0x41**。硬件事实唯一出处:
`docs/units/Unit_8Encoder.md`(寄存器映射、接线、供电)。

## 用法

```c
#include "core2_board.h"
#include "unit_8encoder.h"

// 挂 PORT.A 外接总线(G32/G33,I2C_NUM_0);addr 传 0 = 默认 0x41
esp_err_t err = unit_8encoder_init(core2_board_port_a(), 0);

int32_t inc[8];
unit_8encoder_read_increments(inc);      // 各路"距上次读转了多少格"(读后硬件自清零)

bool btn[8], sw;
unit_8encoder_read_buttons(btn);         // true = 按下(极性宏见下)
unit_8encoder_read_switch(&sw);

unit_8encoder_set_led(3, 255, 120, 0);   // 旋钮 3 就地亮橙(满亮度刺眼,应用层压亮度)
```

`init` 可重复调用:没插单元时返回错误,应用可低频重试实现"插上即生效"。

## 坑 / 注意

- 🔴 **插口位置:红色 PORT.A 在 Core2 机身侧面**,不在底座上——Bottom2 的黑口(PORT.B)
  /蓝口(PORT.C)不是 I2C,插错口 = `ESP_ERR_INVALID_RESPONSE`(NACK,0x41 无应答)。
  排查顺序:先 `core2_board_port_a_scan()` 扫总线(一颗没有=插错口/没电/线断;
  扫到别的地址=单元地址被 0xF0 改过)。**日志解读**:`I2C software timeout` +
  `GPIO 32/33 is not usable` 连环刷屏 ≠ 引脚冲突——经典 ESP32 无硬件 FSM 复位,
  每次事务失败后的总线恢复会重配引脚、对"自己已占用的引脚"重复告警,是症状不是根因。
- 🔴 **单元吃 PORT.A 的 5V = M-Bus 5V(EXTEN)**:电池供电时 EXTEN 没开单元就没电;
  插 USB 时 VBUS 直通会掩盖问题(症状:插电脑能玩、拔线失灵)。`core2_board_init`
  (enable_leds=true)已代开 EXTEN。**深度省电切 5V 后单元复位**:LED 全灭、计数清零,
  恢复供电后应用要自己重写 LED、重建开关基线(防幻影翻转事件)。
- **Increment 读后自清零**:一帧只读一次,读到的就是本帧转动量;init 时已把存量清掉。
- **按键极性:按下=0,已核实**(官方例程 `if (!getButtonStatus(i))`;内部固件原样回传
  电平、无取反)。驱动按 `UNIT_8ENCODER_BTN_ACTIVE_LOW=1` 换算成 `true=按下`。
- 🔴 **bootloader 陷阱(0x54)**:单元上电瞬间 I2C 两线双低 → 困在引导态 0x54 不进应用;
  被 clear-bus 脉冲打搅还可能从机卡死把总线拽死(双低)。自愈 =
  `core2_board_port_a_recover()` 断电重启单元;详见 `docs/units/Unit_8Encoder.md` §5.1。
  注意 EXTEN 掉电不清零:主机重启**不会**给单元断电,卡死跨重启存活。
- **不做跨值批量拼读**:固件对"一次连读 32 字节跨 8 个值"未验证,驱动与官方 Arduino 库
  同粒度(一个值一次事务)。一次全量轮询 ≈17 个小事务 @100kHz ≈ 12ms,30Hz 轮询足够。
- 计数/增量是**有符号 int32**(会回绕),按无符号解析必错。
- 编码器输出是**相对量**,没有绝对角度;转动方向对应的符号以实机为准(应用层给方向系数)。
