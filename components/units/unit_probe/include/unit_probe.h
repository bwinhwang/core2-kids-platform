// unit_probe —— PORT.A I2C 全总线扫描 + 已知单元地址表
//
// 与 `core2_board_port_a_scan()` 的区别:后者只打日志(M0 自检用),本组件返回**结构化
// 结果**供 `unit_bench` 的扫描/列表页直接渲染(数值卡/列表行)。已知地址表与
// `core2_board.c` 的日志分支同源(CLAUDE.md §10、`docs/units/`),两处保持一致。
//
// ⚠️ 本组件只做纯扫描,不做"总线被拉死"的线电平预检(那需要知道具体 GPIO 号,是
// core2_board 的职责):调用前建议先 `core2_board_port_a_stuck()` 自查,拉死时跳过
// 扫描(全地址会 timeout 而不是快速 NACK,体验上是"很慢地全部失败")。
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t     addr;         // 7 位地址
    const char *known_name;   // 已知单元名;NULL = 未知地址(仍探到应答,不是没插东西)
} unit_probe_result_t;

/**
 * @brief 扫描 0x08~0x77 全地址范围,把探到应答的地址填进 out。
 * @param bus 要扫描的 I2C 总线(PORT.A 用 core2_board_port_a())。
 * @param out 输出数组。
 * @param cap out 容量;命中数超过 cap 时多出的部分被截断(不计入返回值)。
 * @return 实际探到应答的器件数(可能大于 cap,此时 out 只填了前 cap 个)。
 */
int unit_probe_scan(i2c_master_bus_handle_t bus, unit_probe_result_t *out, int cap);

#ifdef __cplusplus
}
#endif
