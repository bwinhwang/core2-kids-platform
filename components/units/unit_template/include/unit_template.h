// unit_template.h —— UNIT 外设驱动【骨架】(以 I2C / PORT.A 为例)
//
// 加一个新 UNIT(超声波/手势/光线…):
//   1. 复制本目录为 components/units/unit_<name>/,全局改名 unit_template → unit_<name>
//   2. 按该 UNIT 的硬件文档 docs/units/<name>.md 填地址/寄存器/端口
//   3. 若有官方/M5 托管驱动,在本目录新建 idf_component.yml 声明依赖
//   4. main 里 REQUIRES unit_<name>,用 bsp_i2c_port_a() 取总线传入 init
// (tools/add_unit.sh 会帮你做 1、并生成 docs 雏形)
#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t unit_template_init(i2c_master_bus_handle_t bus, uint8_t addr);
esp_err_t unit_template_read(uint16_t *value);
