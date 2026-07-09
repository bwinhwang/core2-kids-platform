// Unit Gesture(U127)最小驱动 —— I2C 3D 手势识别传感器,芯片 PAJ7620U2,地址 0x73
//
// 硬件事实见 docs/units/Unit_Gesture.md(有效识别距离 5~15cm,9 种内置手势,寄存器分
// bank0/bank1)。接入:unit_gesture_init(core2_board_port_a(), 0) —— PORT.A 外接总线。
//
// ⚠️ 供电坑(同 DLight/超声波/8Encoder):单元吃 PORT.A 的 5V = M-Bus 5V(EXTEN)。
//   深度省电切 5V 后单元断电复位(bank/寄存器配置丢失),恢复供电后重调 init 即可
//   (init 会重发完整初始化序列)。详见 core2_board_port_a() 注释。
//
// 寄存器级事实来源(AGENTS.md §1 铁律:先查参考实现,不编造寄存器值)——
//   **Confirmed via 厂商参考驱动源码**:
//   - Seeed_Studio/Grove_Gesture (github.com/Seeed-Studio/Grove_Gesture,
//     src/paj7620.h + Gesture.cpp):确认 I2C 地址 0x73、BANK_SEL=0xEF(写 0x00/0x01
//     切 bank0/bank1)、手势结果寄存器 RESULT_L=0x43/RESULT_H=0x44、读事务形状
//     (Wire.beginTransmission+write+endTransmission 单独一笔,再 Wire.requestFrom
//     单独一笔 —— 不是 repeated-start 组合读,天然满足平台"写寄存器号+STOP,再单独
//     发起读"的两笔事务规则)。
//   - DFRobot/DFRobot_PAJ7620U2(github.com/DFRobot/DFRobot_PAJ7620U2):同一颗芯片
//     的更完整参考驱动,给出 219 组 {reg,val} 出厂初始化表(bank0+bank1)、PART_ID
//     校验寄存器(0x00 低字节=0x20 / 0x01 高字节=0x76,合并 0x7620)、以及**本驱动
//     实际采用**的手势→比特位映射:RIGHT=bit0/LEFT=bit1/UP=bit2/DOWN=bit3/
//     FORWARD=bit4/BACKWARD=bit5/CLOCKWISE=bit6/ANTI_CLOCKWISE=bit7(均在 0x43),
//     WAVE=bit0(在 0x44)。
//   - **比特位映射定案依据**:Seeed 与 DFRobot 两份参考驱动对"哪个比特对应哪个手势"
//     的枚举顺序彼此不一致(不同物理封装/丝印方向导致的已知分歧)。M5Stack 官方
//     Unit Gesture(本平台购买的确切型号,SKU U127)示例代码
//     (github.com/m5stack/M5Stack, examples/Unit/GESTURE_PAJ7620U2/
//     GESTURE_PAJ7620U2.ino)在依赖清单里明确指向 DFRobot_PAJ7620U2 库,故本驱动
//     采用 DFRobot 的比特位映射作为定案(与实际购入硬件的官方例程一致)。若实机方向
//     感觉"反了",大概率是这份分歧的残留,回来改这里的 switch 分支,不要改初始化表。
//   - **无独立"物体存在但未分类"信号**:两份参考驱动的手势读取路径都只读 0x43/0x44
//     这两个"已分类手势"结果寄存器;DFRobot 头文件另列了 PS_APPROACH_STATE(bank0
//     0x6B)/PS_RAW_DATA(0x6C)等接近感应寄存器名,但两份参考驱动的应用层代码均未
//     启用/读取它们(那是芯片的另一套"接近感应"模式,需要额外校准,default 手势模式
//     初始化表不保证其语义有效)。因此 apps/magic_wand 按 SPEC §4 点 2 的退化方案
//     实现 SHIMMER(固定低频 idle ping),不依赖本驱动读出"未分类但有动静"。
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UNIT_GESTURE_ADDR_DEFAULT  0x73

typedef enum {
    GESTURE_NONE = 0,        // 本帧无新手势(不是错误,是"还没挥出一个来")
    GESTURE_UP,
    GESTURE_DOWN,
    GESTURE_LEFT,
    GESTURE_RIGHT,
    GESTURE_FORWARD,
    GESTURE_BACKWARD,
    GESTURE_CLOCKWISE,
    GESTURE_COUNTER_CLOCKWISE,
    GESTURE_WAVE,
} gesture_event_t;

/**
 * @brief 挂载并初始化 Gesture 单元(校 PART ID + 灌 219 组出厂寄存器表,选回 bank0)。
 *
 * 可重复调用:设备句柄只挂一次,但每次都会重新校验 ID + 重灌整套初始化表 —— 因此
 * 深度省电切 5V 令单元断电复位后,直接再调本函数即可重新接管(bank/寄存器配置
 * 会被重新写好)。
 *
 * @param bus  外接 I2C 总线(core2_board_port_a())。
 * @param addr 7 位地址;传 0 = 默认 0x73。
 * @return ESP_OK 就绪;ESP_ERR_INVALID_ARG 总线为 NULL;
 *         ESP_ERR_NOT_FOUND = PART ID 校验不符(在位但不是这颗芯片,基本不会发生);
 *         其余 = I2C 通信失败(没插/没电/插错口),同 unit_dlight 的错误语义。
 */
esp_err_t unit_gesture_init(i2c_master_bus_handle_t bus, uint8_t addr);

/**
 * @brief 读一次手势分类结果(非阻塞,读当前寄存器快照)。
 *
 * @param out 输出手势;无手势时写 GESTURE_NONE(不是错误)。
 * @return ESP_OK 读成功(不论是否有手势);ESP_ERR_INVALID_STATE 未 init;
 *         ESP_ERR_INVALID_ARG out 为 NULL;其余(TIMEOUT/INVALID_RESPONSE)=
 *         I2C 读失败(拔线/断电),调用方据此判丢失(同其它 unit 的 ERR_STREAK 惯例)。
 */
esp_err_t unit_gesture_read(gesture_event_t *out);

#ifdef __cplusplus
}
#endif
