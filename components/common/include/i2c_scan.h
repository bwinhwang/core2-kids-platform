// i2c_scan.h —— I2C 总线地址扫描自检(接新 UNIT 后先扫一下)
#pragma once
#include "driver/i2c_master.h"

// 扫描 0x08~0x77,把应答的地址打到日志。接新 I2C UNIT 到 PORT.A 后调用,
// 确认识别 + 不撞内部总线地址(0x34/0x38/0x51/0x68)。
void i2c_scan(i2c_master_bus_handle_t bus);
