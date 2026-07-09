// Unit RGB(U003)最小驱动 —— PORT.B 3× SK6812,贴身道具灯("魔法棒")
//
// 硬件事实见 docs/units/Unit_RGB.md:信号在 PORT.B Yellow 线 → 落 Core2 G26
// (M5-Bus DAC 引脚,可作 GPIO 输出);每颗 Unit RGB = 3 像素,GRB,时序同 WS2812。
//
// ⚠️ 与底座灯带(components/ledstrip_fx,G25/10 颗)**职责不同、代码不共享**:
//   底座灯带是常驻机身氛围通道;本组件是"孩子手上举着的贴身道具",拓扑(3px @G26)
//   与语义都不同(见 apps/magic_wand/SPEC.md §2)。RMT 通道由 led_strip 组件按需
//   动态分配,与 ledstrip_fx 占用的 RMT 通道不冲突(经典 ESP32 共 8 条 RMT 通道)。
//
// ⚠️ 供电:PORT.B 与 PORT.A/C 同为 M-Bus 5V(AXP192 EXTEN 使能的 SY7088 升压)——
//   **Confirmed via 平台已核实事实**(components/core2_power/include/core2_power.h
//   注释:"M-Bus 5V / EXTEN:M5GO Bottom2 灯带(及一切吃 M-Bus 5V 的外设)…"):单路
//   升压分裂给灯带/PORT.A/PORT.B/PORT.C 共用,`core2_board_init(enable_leds=true)`
//   已一次性使能覆盖,PORT.B 不需要额外开电。深度省电切 5V 后本单元断电复位,唤醒后
//   需要重新 unit_rgb_init()(可重复调用)。
//
// 效果都是**短促离散序列**(几十~几百 ms,几步状态,不做连续渐变/旋转),配合内部
// 小动画任务按帧步进,调用方只管 trigger,不阻塞。
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_RGB_GPIO   26   // PORT.B Yellow(信号)= Core2 G26
#define UNIT_RGB_COUNT  3    // 每颗 Unit RGB = 3 像素

// 小词汇表:对应 apps/magic_wand/SPEC.md §5 表的"魔法棒 RGB"列,一个法术一个值。
typedef enum {
    WAND_FX_OFF = 0,       // 熄灭(默认待机态,施法之间不常亮)
    WAND_FX_GROW,          // 长高咒:绿色根→尖 3 级离散显现
    WAND_FX_STARRAIN,      // 星雨咒:蓝白由尖到根依次单闪(3 步)
    WAND_FX_SWEEP_ROOT2TIP,// 左/右旋咒之一方向:根→尖单向扫过一次
    WAND_FX_SWEEP_TIP2ROOT,// 另一方向:尖→根单向扫过一次
    WAND_FX_FLASH_WHITE,   // 冲天咒:三像素同时炸亮白一下
    WAND_FX_DIM_POP,       // 躲猫咒:先暗后瞬间回亮(两态,非渐变)
    WAND_FX_WHIRL_CW,      // 旋风咒(顺):3 像素依次点亮,顺方向跑一圈半
    WAND_FX_WHIRL_CCW,     // 旋风咒(逆):同上反方向
    WAND_FX_RAINBOW,       // 你好咒:3 段离散色轮流点亮
    WAND_FX_SHIMMER,       // 微光回应:单像素轻闪一下
} wand_fx_t;

/** @brief 初始化 G26 上的 3 像素 SK6812 + 起内部动画任务。 */
esp_err_t unit_rgb_init(void);

/** @brief 设全局亮度上限(0~255),所有颜色按此缩放。默认 60(贴身道具,略高于底座 48)。 */
void unit_rgb_set_max_brightness(uint8_t max);

/** @brief 触发一次法术特效(非阻塞,可打断上一个未播完的)。 */
void unit_rgb_trigger(wand_fx_t fx);

#ifdef __cplusplus
}
#endif
