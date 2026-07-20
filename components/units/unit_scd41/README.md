# unit_scd41 —— Unit CO2L(SCD41)CO₂/温/湿传感器驱动

PORT.A I2C 单元,芯片 Sensirion **SCD41**(M5Stack Unit CO2L,SKU U104),地址 **0x62**。
输出 CO₂ 浓度(ppm)+ 环境温度(°C)+ 相对湿度(%RH)。硬件事实见
`docs/units/Unit_SCD41.md`。

## API

```c
esp_err_t unit_scd41_init(i2c_master_bus_handle_t bus, uint8_t addr);   // addr=0 → 0x62
esp_err_t unit_scd41_data_ready(bool *ready);                           // 每 ~5s 才 true
esp_err_t unit_scd41_read(uint16_t *co2_ppm, float *temp_c, float *rh_pct);
```

典型用法(周期测量模型):

```c
unit_scd41_init(core2_board_port_a(), 0);      // stop→start,首数据 ~5s 后
// 主循环里:
bool ready;
if (unit_scd41_data_ready(&ready) == ESP_OK && ready) {
    uint16_t co2; float t, rh;
    if (unit_scd41_read(&co2, &t, &rh) == ESP_OK) { /* 上屏 */ }
}
```

## 关键坑

- 🔴 **5 秒测量周期**:周期模式下 SCD41 每 5s 才产出一次新数据。**必须先 `data_ready` 探到
  就绪再 `read`**,别把"数据未就绪"(ready=false)当成读失败去判拔线——那样会误判在线单元
  掉线。两次新数之间沿用上一次读数即可。
- 🔴 **init 阻塞 ~500ms**:为把单元拉回确定态,init 先发 stop_periodic(执行 500ms)再
  start_periodic。这是每次挂载(插入/断电恢复)的一次性代价;由后台扫描调用、不持 LVGL
  锁,只延后一次 app tick,不冻结渲染。
- **供电 = PORT.A 5V(EXTEN)**:同其它 PORT.A 单元,电池供电时须 `core2_board_init` 已开
  EXTEN;深度省电切 5V 会令单元断电复位(退出周期测量),恢复供电后重调 init 即可。
- **命令/读是两笔独立事务**:发 16-bit 命令(大端两字节)+STOP,等执行时间,再单独 receive;
  不用 repeated-start 组合读。

## 协议核实

命令码、CRC 参数、换算公式、就绪判据均 **Confirmed via esp-idf-lib/scd4x.c 源码 + Sensirion
SCD4x datasheet**(见 `unit_scd41.h` 顶部注释,逐项列明):
- start=0x21B1 / read=0xEC05 / data_ready=0xE4B8 / stop=0x3F86(500ms);
- CRC-8 poly 0x31 init 0xFF 不反射;data_ready = 状态字 & 0x07FF != 0;
- read 回 9 字节 = 3×(2 数据 + 1 CRC);CO2=raw ppm、T=-45+175·raw/65536、RH=100·raw/65536。

## 待实机点检

- [ ] 接上 Unit CO2L,init 后 ~5s 内首个 CO₂/温/湿读数出现;数值合理(室内 CO₂ 400~1500ppm、
      温度接近室温、湿度合理)。
- [ ] 对着单元呼气,CO₂ 在几个 5s 周期内明显上升,离开后缓慢回落(验证读数真实、CRC 未误伤)。
- [ ] 运行中拔线,调用方在连续读失败后判"断线";插回后 init 重新接管、数据恢复。
- [ ] 温度换算校核:与参考温度计对比(SCD41 自身发热会使读数略高于环境,属正常)。
