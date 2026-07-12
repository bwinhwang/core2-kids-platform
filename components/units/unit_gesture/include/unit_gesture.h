// Unit Gesture(U127)最小驱动 —— I2C 3D 手势识别传感器,芯片 PAJ7620U2,地址 0x73
//
// ⚠️ **光标模式对手掌跟踪实测不可用**(apps/magic_wand v2 P1 实机验证,2026-07-11):
//   157s 游玩实测手在视野内占空比仅 ~50%,>0.5s 跟踪中断 31 次(平均 2.5s、最长
//   15.7s),偶发单步满量程跳变——PAJ7620U2 光标模式是为指尖近距设计(60° FOV 在
//   10cm 处仅 ~11cm 见方隐形窗口),手掌极易滑出/充满视野,连续位置信号对幼儿场景
//   不可用。本文件下方"光标模式"寄存器事实(叠加写表/坐标寄存器/`in_view` 判据)
//   全部继续留档(已核实、非编造),API 也保留不删,只是 apps/magic_wand v2.1 起
//   改回默认手势模式 + 新增的"在场信号"API(见下),不再调用 `unit_gesture_set_
//   cursor_mode()`。详见 apps/magic_wand/SPEC.md §0、README.md。
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
#include <stdbool.h>
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

// ─────────────────────────────────────────────────────────────────────────
// 光标模式(apps/magic_wand v2,SPEC.md §9)—— 逐字节核实自参考驱动源码原文
// (AGENTS.md §1 铁律,拿不到源码不编造):
//
//   Confirmed via acrandal/RevEng_PAJ7620 (github.com/acrandal/RevEng_PAJ7620,
//   raw.githubusercontent.com/acrandal/RevEng_PAJ7620/master/src/
//   RevEng_PAJ7620.h + .cpp,库版本 1.5.0,2026-07-11 抓取):
//
//   1) 进光标模式是"叠加写",不是重新灌整套初始化表——`setCursorMode()`
//      只调用一次 `writeRegisterArray(setCursorModeRegisterArray, ...)`,
//      而 `writeRegisterArray()`(.cpp L247-263)逐项 `writeRegister(addr,val)`,
//      不touch 数组之外的任何寄存器;数组本身也只有 17 组 {addr,val}(含 3 次
//      bank 切换)。即:在本驱动 `unit_gesture_init()` 已灌好的 219 组出厂表
//      之上,再叠加写这一小撮寄存器即可进光标模式,原表其余寄存器不受影响。
//      对称地,`setGestureMode()`(.cpp L292-295)也只是另一小撮叠加写,用来
//      从光标模式切回手势模式(不需要重新灌整张 219 组表)。
//
//   2) `setCursorModeRegisterArray`(.h L461-497)完整序列(十六进制
//      "高字节=寄存器地址,低字节=值"):
//        Bank0: EF=00(选 bank0)
//               32=29  (光标行为位域:Use-Top/BG-Model/Invert-Y/Invert-X/TopRatio)
//               33=01,34=00 (R_PositionFilterStartSizeTh,9 位数值拆两个寄存器)
//               35=01,36=00 (R_ProcessFilterStartSizeTh,同上拆法)
//               37=03 (R_CursorClampLeft[4:0],datasheet 出厂默认 0x09→本表改 0x03)
//               38=1B (R_CursorClampRight[4:0],出厂默认 0x15→0x1B)
//               39=03 (R_CursorClampUp[4:0],出厂默认 0x0A→0x03)
//               3A=1B (R_CursorClampDown[4:0],出厂默认 0x12→0x1B)
//               41=00 (关掉全部 8 个手势中断使能位——光标模式不用手势中断)
//               42=84 (中断使能掩码 bit2=Has Object / bit7=No Object,
//                      光标模式专属语义,同一寄存器在手势模式下另作 Wave 中断用)
//               8B=01 (R_Cursor_ObjectSizeTh[7:0],出厂默认 0x10→0x01,放宽最小物体阈值)
//               8C=07 (R_PositionResolution[2:0],出厂默认已是 0x07,重申不变)
//        Bank1: EF=01(选 bank1)
//               04=03 (Lens Orientation:同时置位 invert-X + invert-Y,
//                      驱动注释"GUI coordinates:原点左上、+X 向右、+Y 向下")
//               74=03 (模式选择寄存器:0=手势/3=光标/5=接近——这一条才是真正
//                      切换工作模式的开关;手势模式下同寄存器写 0x00)
//        Bank0: EF=00(切回 bank0 停泊,供后续寄存器读写)
//      交叉核对 PixArt《PAJ7620U2 General Datasheet》v1.0(2016-03-29,
//      files.seeedstudio.com/wiki/Grove_Gesture_V_1.0/res/
//      PAJ7620U2_Datasheet_V0.8_20140611.pdf)§5.8 Cursor Mode Controls:
//      寄存器地址/名称/出厂默认值与上表完全吻合(R_CursorClampLeft/Right/Up/Down
//      @0x37-0x3A、R_Cursor_ObjectSizeTh@0x8B、R_PositionResolution@0x8C)。
//
//   3) 光标 X/Y 坐标寄存器(bank0,只在模式寄存器 0x74=0x03 时有意义):
//        X: LOW=0x3B(全 8 位)/ HIGH=0x3C(`getCursorX()`,.cpp L320-334,
//           读回后 `data1 &= 0x0F` 只取低 4 位当 [11:8]);
//        Y: LOW=0x3D/ HIGH=0x3E(`getCursorY()`,.cpp L344-358,同一掩码手法)。
//        合成:`result = (data1<<8) | data0`,即 12 位值,寄存器位宽上限
//        0..4095(掩码逻辑是源码原文,非猜测)。PixArt datasheet §5.8 里
//        0x3B/0x3C 标注为 CursorClampCenterX[7:0]/[11:8]、0x3D/0x3E 为
//        CursorClampCenterY[7:0]/[11:8](均只读),寄存器地址与位宽切分与
//        RevEng 源码一致,交叉确认无误。
//        ⚠️ **两份来源都只给到寄存器位宽(12 位),没有给出光标实际输出的
//        数值区间**——有效范围受 FOV / R_CursorClampLeft-Right-Up-Down /
//        R_PositionResolution 共同决定,两份文档都未钉死具体数字;官方
//        `examples/paj7620_cursor_mode/paj7620_cursor_mode.ino` 示例代码
//        也只是原样打印 raw X/Y,没有给校准常量。因此 apps/magic_wand 把
//        "光标原始坐标 → 屏幕坐标"的映射区间(`CUR_RAW_X/Y_MIN/MAX`)当作
//        **待实机标定的占位值**处理(与 tuning.h 里其余"默认值待实机标定"
//        常量同等地位),不是编造的寄存器事实——寄存器事实到位宽为止。
//
//   4) `in_view` 判据寄存器:bank0 0x44(`PAJ7620_ADDR_CURSOR_INT`,与手势
//      模式下的 GES_RESULT_1/Wave 标志复用同一物理地址,寄存器语义随
//      0x74 模式切换而不同,两模式互斥不冲突)。`isCursorInView()`
//      (.cpp L368-380)按**精确值**判断:读回值 == 0x80(`CUR_NO_OBJECT`,
//      bit7)→ false;== 0x04(`CUR_HAS_OBJECT`,bit2)→ true;其余(含 0x00
//      过渡态)→ false。本驱动照抄这个精确匹配,不改成"按位或更宽容"的
//      写法,避免过渡态被误判为有物体。
//
//   5) 读事务形状不变:沿用 `read_regs()` 现有的"写寄存器号(STOP)+ 单独
//      发起读"两笔事务(见 read_regs 实现注释),光标寄存器读不特殊化。
//
//   模式切换 API 均可重复调用(纯寄存器叠加写,无内部状态依赖),供 DEEP
//   省电唤醒后 `unit_gesture_init()` 全量重灌 + 再调 `set_cursor_mode()`
//   重新进光标模式。
// ─────────────────────────────────────────────────────────────────────────

/**
 * @brief 切入光标模式(叠加写,見上方核实记录)。可重复调用。
 * @return ESP_OK 成功;ESP_ERR_INVALID_STATE 未 init;其余 = I2C 写失败(拔线/断电)。
 */
esp_err_t unit_gesture_set_cursor_mode(void);

/**
 * @brief 切回手势模式(叠加写,供 Plan B / 诊断用;9 手势模式默认在 init 后已生效,
 *        通常不需要主动调用)。可重复调用。
 * @return ESP_OK 成功;ESP_ERR_INVALID_STATE 未 init;其余 = I2C 写失败(拔线/断电)。
 */
esp_err_t unit_gesture_set_gesture_mode(void);

/**
 * @brief 读一次光标坐标(仅在光标模式下有意义;手势模式下调用会读到无意义数值)。
 *
 * @param x       输出:光标 X(0..4095,寄存器位宽上限;实际有效区间待实机标定,
 *                见上方核实记录第 3 条)。
 * @param y       输出:光标 Y(同上)。
 * @param in_view 输出:是否有物体在视野内(0x44 精确匹配判据)。x/y 每次调用都
 *                会读寄存器原始值并写出,但无物体时该原始值可能无意义(陈旧/漂移
 *                坐标),调用方应仅在 *in_view == true 时采信 x/y。
 * @return ESP_OK 读成功;ESP_ERR_INVALID_STATE 未 init;ESP_ERR_INVALID_ARG
 *         任一输出指针为 NULL;其余(TIMEOUT/INVALID_RESPONSE)= I2C 读失败
 *         (拔线/断电),调用方据此判丢失(同 unit_gesture_read 的 ERR_STREAK 惯例)。
 */
esp_err_t unit_gesture_read_cursor(uint16_t *x, uint16_t *y, bool *in_view);

// ─────────────────────────────────────────────────────────────────────────
// 在场信号(apps/magic_wand v2.1,SPEC.md §9)—— 寄存器地址/位宽逐字节核实自
// 参考驱动源码原文(AGENTS.md §1 铁律,拿不到源码不编造):
//
//   Confirmed via acrandal/RevEng_PAJ7620 (github.com/acrandal/RevEng_PAJ7620,
//   raw.githubusercontent.com/acrandal/RevEng_PAJ7620/master/src/
//   RevEng_PAJ7620.h + .cpp,库版本 1.5.0,2026-07-11 抓取):
//
//   1) 三个只读寄存器均在 **bank0**(`/** @name REGISTER BANK 0 */` 分组下),
//      与手势结果寄存器 0x43/0x44 同一 bank——本驱动 `init()` 结尾已 `select_
//      bank(0)` 停泊,手势模式下常态轮询无需再切 bank(与 §9 设计一致)。
//        `PAJ7620_ADDR_OBJECT_BRIGHTNESS`  = 0xB0,只读 [7:0](`getObjectBrightness()`
//        单笔读,无遮罩,值域 0..255,"255 is max" —— 头文件原注释)。
//        `PAJ7620_ADDR_OBJECT_SIZE_LSB`    = 0xB1,只读 [7:0]。
//        `PAJ7620_ADDR_OBJECT_SIZE_MSB`    = 0xB2,只读 **[3:0]**(注意不是像
//        光标 X/Y 那样的 [11:8]——头文件原文写的是 [3:0]);`getObjectSize()`
//        合成方式为 `(data1<<8)|data0`,头文件原注释 "900 is max (30x30 pixel
//        array)"。参考实现本身未对 MSB 字节做 `&0x0F` 遮罩(硬件保证高位为 0),
//        本驱动仍按与光标 X/Y 一致的防御性写法遮罩 `&0x0F`,不改变数值,只是
//        更保守。
//
//   2) **寄存器存在性 + 位宽 + 值域上限已核实,但读数语义(无手/远/近手实际落在
//      什么区间)两份来源都未给标定曲线**——PixArt datasheet 与 RevEng 源码都
//      只给出寄存器名/地址/位宽,没有给"多远的手对应多少亮度/尺寸"的对照表。
//      因此 apps/magic_wand 的 `PRES_ON_TH`/`PRES_OFF_TH`/`PRES_LVL2_TH`/
//      `PRES_LVL3_TH`(tuning.h)是**待实机标定的占位值**,不是编造的寄存器
//      事实——寄存器事实到"数值范围 0..255 / 0..4095"为止,同 §9 光标模式
//      `CUR_RAW_X/Y_MIN/MAX` 的处理原则一致。
//
//   3) 读事务形状不变:两个只读区各自走"写寄存器号(STOP)+ 单独发起读"两笔
//      独立事务(0xB0 单字节一笔;0xB1/0xB2 自增地址合并一笔读 2 字节),不特殊化。
// ─────────────────────────────────────────────────────────────────────────

/**
 * @brief 读一次"在场"信号(手势模式下有效;与光标模式互斥,本 API 不切模式)。
 *
 * @param brightness 输出:物体亮度(bank0 0xB0,0..255,"255 is max"——datasheet/
 *                   参考驱动均未标定"多远=多少",见上方核实记录第 2 条)。
 * @param size       输出:物体尺寸(bank0 0xB1/0xB2 合成,12 位,"900 is max
 *                   (30x30 像素阵列)"——同样未标定"多远=多大")。
 * @return ESP_OK 读成功;ESP_ERR_INVALID_STATE 未 init;ESP_ERR_INVALID_ARG
 *         任一输出指针为 NULL;其余(TIMEOUT/INVALID_RESPONSE)= I2C 读失败
 *         (拔线/断电),调用方据此判丢失(同 unit_gesture_read 的 ERR_STREAK 惯例)。
 */
esp_err_t unit_gesture_read_presence(uint8_t *brightness, uint16_t *size);

#ifdef __cplusplus
}
#endif
