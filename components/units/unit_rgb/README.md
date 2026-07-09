# unit_rgb —— Unit RGB(U003)最小驱动

PORT.B 上的 3× SK6812 贴身道具灯("魔法棒")。硬件事实唯一出处:`docs/units/Unit_RGB.md`
(信号在 Yellow 线 → Core2 G26,每颗 Unit RGB = 3 像素)。

## 用法

```c
#include "unit_rgb.h"

esp_err_t err = unit_rgb_init();          // G26,3 像素,起内部动画任务(25ms/帧)
unit_rgb_set_max_brightness(60);          // 贴身道具,亮度上限可比底座(48)略高但仍克制

unit_rgb_trigger(WAND_FX_FLASH_WHITE);    // 非阻塞,触发一次法术特效
```

## 坑 / 注意

- **与底座灯带(`components/ledstrip_fx`,G25/10 颗)代码不共享**:拓扑(3px @G26)和语义
  (贴身道具 vs 机身氛围)都不同,详见 `apps/magic_wand/SPEC.md` §2 的职责边界说明。
  RMT 通道由 `led_strip` 组件按需动态分配,与 `ledstrip_fx` 占用的通道不冲突。
- **供电与 PORT.A/C 同路**:M-Bus 5V 由 AXP192 EXTEN 使能的 SY7088 升压供电,单路分裂给
  灯带/PORT.A/PORT.B/PORT.C 共用(`core2_power` 头注释已载明"及一切吃 M-Bus 5V 的外设"),
  `core2_board_init(enable_leds=true)` 已一次性使能覆盖,PORT.B 不需要额外开电。
- **深度省电切 5V 后本单元断电复位**:唤醒后需重新 `unit_rgb_init()`(可重复调用,内部会
  重新创建 RMT 设备)。
- **效果都是短促离散序列**(几十~几百 ms,几步状态),不做连续渐变/旋转——呼应
  `apps/magic_wand/SPEC.md` 的"全部视觉不透明离散状态,不做真正的渐变/旋转"红线。
- **纯输出单元**,无 I2C、无回读能力;`unit_rgb_init()` 失败(如 RMT 通道耗尽)不阻塞整卡
  启动,只损失魔法棒这一条反馈通道。
