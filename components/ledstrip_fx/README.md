# ledstrip_fx —— 底座灯带效果引擎(10×SK6812 @ G25)

M5GO Bottom2 灯带的常驻动画任务(40fps,低优先级,不阻塞应用):
**基础模式**(常态氛围)+ **瞬态特效**(播完自动回基础模式)。
词汇表与 `audio_fx`/`haptics` 对应,幼儿应用直接复用:

- 基础:`LED_BASE_AMBIENT`(暖色轻呼吸)/ `LED_BASE_NEAR`(接近目标加亮)/
  `LED_BASE_IDLE`(极低极慢呼吸)/ `LED_BASE_OFF`(深度省电全熄)
- 瞬态:`LED_FX_BUMP`(一下微闪)/ `LED_FX_COLLECT`(金色扫圈)/ `LED_FX_WIN`(彩虹转圈)

## ⚠️ 前置条件(经典坑)

**必须先开 M-Bus 5V:`core2_power_bus_5v(true)`,再 `ledstrip_fx_init()`。**
不开 EXTEN 灯带没供电全黑,而数据线照常翻转、refresh 返回 OK——别误判硬件坏。
(`core2_board_init` 已按此顺序代管。)

## 用法

```c
core2_power_bus_5v(true);
ledstrip_fx_init();
ledstrip_fx_set_max_brightness(48);   // 全局亮度上限;满亮刺眼且耗流,幼儿应用 ≤48
ledstrip_fx_set_base(LED_BASE_AMBIENT);
ledstrip_fx_trigger(LED_FX_WIN);      // 非阻塞,播完自动回基础模式
```

## 板级事实

- G25 被灯带占用,**不能再当 DAC**;GRB 顺序,RMT 后端 10MHz,经典 ESP32 无 RMT DMA(10 颗够用)。
- 深度省电:`LED_BASE_OFF` + `core2_power_bus_5v(false)`(顺带省 SY7088 静态电流)。
- 出处:`docs/platform/M5GO_Bottom2.md`。
