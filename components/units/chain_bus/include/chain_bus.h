// chain_bus —— M5Stack Chain 系列 UART 级联总线传输层(Core2 PORT.C 作 host)
//
// Chain ≠ Grove I2C:同是 HY2.0-4P,但走 **UART 115200 8N1 菊花链**。主控(host)对
// 链上第 id 个节点(1 起)发一帧 [AA 55 len id cmd data.. crc 55 AA],节点逐跳转发、
// 命中 id 的那个处理并原路回一帧。本组件把这套帧协议(与官方 m5stack/M5Chain 库逐字节
// 对齐)封装成"请求/应答"一次事务,并自动跳过节点主动上报的心跳/枚举包。
//
// 接线(docs/platform/M5GO_Bottom2.md §3 / docs/units/Chain_*.md):
//   Core2 **PORT.C = UART2**,Yellow=G13(RXD2)、White=G14(TXD2)。节点 IN 口朝主控
//   (三角箭头从主控指向外侧),用随附 Chain Bridge 桥接;接反整链不通。
//
// 🔴 供电坑(与 PORT.A 单元同源):Chain 节点吃 PORT.C 的 5V = M-Bus 5V(SY7088 升压,
//   AXP192 EXTEN)。电池供电时 EXTEN 没开 = 节点没电(插 USB 时 VBUS 直通会掩盖)。
//   core2_board_init(enable_leds=true) 已代开 EXTEN;不用灯带的应用需 core2_power_bus_5v(true)。
//   深度省电切 5V → 节点断电复位,唤醒后需重新 probe。
//
// ⚠️ Core2 直接当 Chain host(不经独立 Chain 主控如 DualKey)未经官方背书,本组件 +
//   chain_lab 应用即为**上板验证**手段;协议实现照 M5Chain 库,理应可行。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

// Core2 PORT.C 默认接线(UART2 / G13 RX / G14 TX)
#define CHAIN_BUS_PORTC_UART    UART_NUM_2
#define CHAIN_BUS_PORTC_RX_PIN  13
#define CHAIN_BUS_PORTC_TX_PIN  14

// 节点设备类型码(M5Chain ChainCommon.hpp:chain_device_type_t)
typedef enum {
    CHAIN_DEV_UNKNOWN  = 0x0000,
    CHAIN_DEV_ENCODER  = 0x0001,
    CHAIN_DEV_ANGLE    = 0x0002,
    CHAIN_DEV_KEY      = 0x0003,
    CHAIN_DEV_JOYSTICK = 0x0004,
    CHAIN_DEV_TOF      = 0x0005,
} chain_dev_type_t;

/**
 * @brief 安装 UART 并作 Chain host。可重复调用(已装则跳过)。
 * @param uart   UART 端口(PORT.C 用 CHAIN_BUS_PORTC_UART=UART_NUM_2)。
 * @param tx_pin 主控 TX 引脚(PORT.C = 14)。
 * @param rx_pin 主控 RX 引脚(PORT.C = 13)。
 */
esp_err_t chain_bus_init(uart_port_t uart, int tx_pin, int rx_pin);

/** @brief 便捷初始化:PORT.C(UART2,G14 TX / G13 RX)。 */
esp_err_t chain_bus_init_port_c(void);

/**
 * @brief 一次请求/应答事务。发帧给节点 id(1 起)+ cmd + 可选 data,等匹配 (id,cmd) 的
 *        应答帧,把其**载荷**(id/cmd 之后、crc 之前的数据字节)拷进 rx_payload。
 *        期间收到的心跳/枚举/别的节点包会被丢弃、继续等,直到命中或超时。
 * @param tx_data   请求载荷(可 NULL,tx_len=0)。
 * @param rx_payload 载荷输出缓冲(可 NULL,只在乎成败/仅需操作状态时)。
 * @param rx_cap    rx_payload 容量;实际载荷更长会被截断到 rx_cap。
 * @param rx_len    实际拷入字节数(可 NULL)。
 * @return ESP_OK 命中;ESP_ERR_TIMEOUT 超时无应答;ESP_ERR_INVALID_STATE 未 init;
 *         ESP_ERR_INVALID_ARG 参数越界。
 */
esp_err_t chain_bus_request(uint8_t id, uint8_t cmd,
                            const uint8_t *tx_data, uint8_t tx_len,
                            uint8_t *rx_payload, uint8_t rx_cap, uint8_t *rx_len,
                            int timeout_ms);

// ── 全系列通用命令(ChainCommon)──────────────────────────────────────
/** @brief 读节点设备类型(0xFB)。可当"在位 + 是哪种节点"检查。 */
esp_err_t chain_bus_get_device_type(uint8_t id, chain_dev_type_t *type, int timeout_ms);

/** @brief 读节点固件版本(0xFA)。 */
esp_err_t chain_bus_get_fw_version(uint8_t id, uint8_t *ver, int timeout_ms);

/** @brief 设节点第 index 颗板载 RGB(0x20)。encoder/joystick 各只 1 颗,index=0。
 *  颜色受节点 RGB 亮度档缩放,先调 chain_bus_set_rgb_brightness 保证可见。 */
esp_err_t chain_bus_set_rgb(uint8_t id, uint8_t index, uint8_t r, uint8_t g, uint8_t b, int timeout_ms);

/** @brief 设节点板载 RGB 亮度档(0x22),pct 0~100。 */
esp_err_t chain_bus_set_rgb_brightness(uint8_t id, uint8_t pct, int timeout_ms);

/** @brief 诊断:抓 ms 毫秒内 PORT.C 收到的原始字节并 hexdump(节点心跳约 1/s)。
 *  用来判断"Core2 到底有没有收到节点发的东西"——验证直连 host 是否成立的第一手段。
 *  @return 收到的字节数。 */
int chain_bus_sniff(int ms);

#ifdef __cplusplus
}
#endif
