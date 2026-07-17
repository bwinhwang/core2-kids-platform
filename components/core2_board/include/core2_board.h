// core2_board —— Core2 v1.0 + M5GO Bottom2 一键 bring-up(平台外设层的总入口)
//
// 新应用只需:
//     core2_board_cfg_t cfg = CORE2_BOARD_CFG_DEFAULT;
//     ESP_ERROR_CHECK(core2_board_init(&cfg));
// 即完成:内部 I2C → 屏+LVGL+背光 → I2C 自检扫描 → AXP192 直控绑定 →
//        (按需)灯带供电+驱动 / 喇叭 / 震动 / IMU,全部按实机验证过的顺序。
//
// ⚠️ 初始化顺序是本组件固化的核心知识,别绕过它手工散装初始化:
//   1. bsp_i2c_init 必须最先(AXP192/触摸/IMU 都挂内部 I2C);
//   2. bsp_display_start 会做 AXP192 基础配置(屏电/背光),并把 REG 0x12 重写——
//      所以 core2_power_init / EXTEN(灯带 5V)必须在它**之后**;
//   3. 灯带先开 M-Bus 5V(EXTEN)再 init 驱动,否则数据在跑灯不亮(经典坑);
//   4. IMU 复用内部 I2C 总线句柄,不自己再 init 一条(会和 AXP192/触摸抢)。
//
// 各外设也可单独用(core2_power / ledstrip_fx / audio_fx / haptics / imu_mpu6886),
// 本组件只是把它们按正确顺序串起来。板级事实见 docs/platform/,坑位见各组件头注释。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int     brightness_pct;      // 初始背光 0~100(评估台默认 70,清晰优先于护眼低亮)
    bool    enable_leds;         // Bottom2 灯带(自动开 M-Bus 5V/EXTEN)
    uint8_t led_max_brightness;  // 灯带全局亮度上限 0~255
    bool    enable_audio;        // NS4168 喇叭(audio_fx,整局保持 open)
    bool    enable_haptics;      // 震动马达(AXP192 LDO3,经 BSP feature)
    bool    enable_imu;          // MPU6886(来自 Bottom2;没接底座会失败)
} core2_board_cfg_t;

// 评估台默认:全开、中亮(数值/图表可读优先;单元评估用不到灯带可自行 enable_leds=false)
#define CORE2_BOARD_CFG_DEFAULT (core2_board_cfg_t){ \
    .brightness_pct = 70, .enable_leds = true, .led_max_brightness = 80, \
    .enable_audio = true, .enable_haptics = true, .enable_imu = true }

/**
 * @brief 按正确顺序初始化平台外设。
 * @param cfg 传 NULL 等价于 CORE2_BOARD_CFG_DEFAULT。
 * @return ESP_OK 全部就绪;ESP_ERR_NOT_FOUND IMU 没找到(多半是没接 Bottom2 底座);
 *         其余错误码来自对应外设 init。失败的外设有显式 ESP_LOGE,不静默。
 */
esp_err_t core2_board_init(const core2_board_cfg_t *cfg);

/** @brief LVGL 显示句柄(core2_board_init 之后有效)。 */
lv_display_t *core2_board_display(void);

/** @brief 内部 I2C 总线句柄(挂 AXP192/触摸/RTC/IMU;外接 UNIT 别用这条)。 */
i2c_master_bus_handle_t core2_board_i2c(void);

/**
 * @brief PORT.A 外接 I2C 总线句柄(G32=SDA / G33=SCL,懒加载,首次调用时创建)。
 *
 * 外接 UNIT(8Encoder 0x41 / DLight 0x23 / 超声波 0x57 / 手势 0x73)全挂这条,
 * 地址互不冲突可共存;与内部总线(AXP192/触摸/IMU)物理隔离,不会互抢。
 * 占用 I2C_NUM_0(内部总线占 I2C_NUM_1,CONFIG_BSP_I2C_NUM=1)。
 *
 * ⚠️ PORT.A 的 5V 供电来自 M-Bus 5V(SY7088 升压,AXP192 EXTEN 使能):
 *   · 插着 USB 时 VBUS 直通,单元"看起来一直有电";
 *   · 拔线用电池时,EXTEN 没开 = 单元断电——症状是"插电脑能玩、拔线失灵"。
 *   core2_board_init(enable_leds=true)已代开 EXTEN;不用灯带的应用自己
 *   core2_power_bus_5v(true)。深度省电切 5V 时 PORT.A 单元同样断电(会复位)。
 *
 * @return 总线句柄;创建失败返回 NULL(有 ESP_LOGE,不静默)。
 */
i2c_master_bus_handle_t core2_board_port_a(void);

/** @brief 扫内部 I2C 并打印在位器件(M0 自检用;init 内已自动跑一次)。
 *  @return true = 扫到 0x68(IMU/底座在位)。 */
bool core2_board_i2c_scan(void);

/** @brief 扫 PORT.A 外接总线并打印在位 UNIT(单元"无应答"时的第一诊断手段)。
 *  先读线电平:被拉死(非双高)直接判因并跳过扫描;线平正常才逐地址 probe。
 *  一颗都没扫到 = 插错口(红口在 Core2 机身侧面;底座黑口 PORT.B/蓝口 PORT.C
 *  不是 I2C)/ 线缆问题;扫到 0x54 = 8Encoder 困在 bootloader(见 recover)。
 *  @return true = 至少扫到一颗器件。 */
bool core2_board_port_a_scan(void);

/** @brief PORT.A 总线是否被拉死(SDA/SCL 任一为低;空闲应双高)。
 *  须在 core2_board_port_a() 创建总线之后调用才有意义。 */
bool core2_board_port_a_stuck(void);

/** @brief 给 PORT.A 单元断电重启:切 M-Bus 5V 400ms 再恢复(底座灯带同路,会闪一下)。
 *  用途:单元卡死拽住总线(8Encoder 困在 bootloader/I2C 从机卡死)的软件自愈——
 *  恢复供电瞬间 G32/33 已有上拉,单元重启会正常跳进应用(0x41)。阻塞 ~550ms。 */
esp_err_t core2_board_port_a_recover(void);

#ifdef __cplusplus
}
#endif
