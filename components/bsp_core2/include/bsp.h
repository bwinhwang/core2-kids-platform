// bsp.h —— Core2 + Bottom2 平台板级支持包(唯一对外头)
//
// 应用只需 #include "bsp.h" 并调 bsp_init():完成 AXP192 上电 → 内部 I2C →
// IMU → (可选)灯/屏/触摸/音频 的正确初始化顺序(顺序错屏会黑,见 Core2 §2)。
// 板载能力可用 menuconfig「Core2 BSP」裁剪(CONFIG_BSP_ENABLE_*)。
#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>
#include <stdbool.h>
#include "platform_pins.h"
#include "bsp_port.h"

#ifdef __cplusplus
extern "C" {
#endif

// ═════════════════════════ 一键初始化 ═══════════════════════════════════════
// 按 Core2 上电时序初始化平台:AXP192(SPK 先关)→ 内部 I2C → IMU →
// 依 Kconfig 开关初始化灯/屏/触摸/音频。返回后各外设就绪,应用可起任务。
esp_err_t bsp_init(void);

// ═════════════════════════ I2C 总线句柄 ═════════════════════════════════════
i2c_master_bus_handle_t bsp_i2c_internal(void);   // 内部总线(板载外设,勿外接)
i2c_master_bus_handle_t bsp_i2c_port_a(void);      // PORT.A 外部总线(接 I2C UNIT)

// ═════════════════════════ 电源 / AXP192(bsp_power.c)═══════════════════════
esp_err_t bsp_power_set_speaker(bool en);     // NS4168 SPK_EN(IO2)
esp_err_t bsp_power_set_led(bool on);          // 绿色电源灯(IO1)
esp_err_t bsp_power_set_backlight(bool on);    // LCD 背光(DCDC3)
esp_err_t bsp_power_set_vibration(bool on);    // 震动马达(LDO3)
float     bsp_power_batt_voltage(void);        // 电池电压(V);失败返回 0

// ═════════════════════════ IMU / MPU6886(bsp_imu.c)════════════════════════
esp_err_t bsp_imu_read_accel(float out_g[3]);  // 三轴加速度(g,±8g 量程)
uint8_t   bsp_imu_who_am_i(void);              // 应为 0x19(区分 BMI270)

#if CONFIG_BSP_ENABLE_LEDS
// ═════════════════════════ 灯条 / SK6812(bsp_leds.c)══════════════════════
// 输出为【裸】0~255,不做亮度封顶——应用请经 kids_safety.h 的 KIDS_MAX_BRIGHTNESS 限幅。
esp_err_t bsp_leds_set(int logical_idx, uint8_t r, uint8_t g, uint8_t b); // 经 layout 映射
esp_err_t bsp_leds_clear(void);
esp_err_t bsp_leds_refresh(void);              // 提交到灯条
#endif

#if CONFIG_BSP_ENABLE_AUDIO
// ═════════════════════════ 音频 / NS4168(bsp_audio.c)═════════════════════
// NS4168 监听右声道:内部把 mono 复制成 L=R 交织再写(见 Core2 §3 踩坑)。
esp_err_t bsp_audio_write_mono(const int16_t *pcm_mono, size_t frames); // 阻塞→实时节拍
esp_err_t bsp_audio_play_tone(int freq_hz, int ms);                     // 自检测试音
#endif

#if CONFIG_BSP_ENABLE_DISPLAY
// ═════════════════════════ 屏 / ILI9342C(bsp_display.c)═══════════════════
// 把 RGB565(小端主机序)区块推到屏;方向/反色/BGR 已在 BSP 内按 Core2 面板固定处理。
esp_err_t bsp_display_draw(int x, int y, int w, int h, const uint16_t *pixels);
esp_err_t bsp_display_fill(uint16_t color);    // 整屏填充(分条,便利)
uint16_t  bsp_rgb565(int r, int g, int b);     // RGB565(已处理 BGR + SPI 字节序)
#endif

#if CONFIG_BSP_ENABLE_TOUCH
// ═════════════════════════ 触摸 / FT6336U(bsp_touch.c)════════════════════
// 读一个触点;有触摸返回 true 并填坐标(屏坐标,已随显示方向)。⚠️ 未上板验证。
bool bsp_touch_read(uint16_t *x, uint16_t *y);
#endif

#ifdef __cplusplus
}
#endif
