# unit_probe —— PORT.A I2C 全总线扫描 + 已知单元地址表

给 `unit_bench` 的扫描/列表页用:扫一遍 PORT.A(或任何传入的 I2C 总线)全地址范围,
返回结构化结果(地址 + 是否已知单元),供 UI 直接渲染成列表(已知单元显示名字,未知
地址显示 hex)。

## 用法

```c
i2c_master_bus_handle_t bus = core2_board_port_a();
if (core2_board_port_a_stuck()) {
    // 总线被拉死(SDA/SCL 任一为低),跳过扫描,提示"检查供电/线缆"
} else {
    unit_probe_result_t results[16];
    int n = unit_probe_scan(bus, results, 16);
    for (int i = 0; i < n && i < 16; i++) {
        // results[i].addr、results[i].known_name(NULL=未知地址)
    }
}
```

## 已知地址表(与 `core2_board.c` 日志分支同源)

| 地址 | 单元 |
|---|---|
| 0x23 | DLight(BH1750 光照) |
| 0x41 | 8Encoder |
| 0x54 | 8Encoder 困在 bootloader(`core2_board_port_a_recover()` 可救回 0x41) |
| 0x57 | Ultrasonic(RCWL 超声波) |
| 0x73 | Gesture(PAJ7620U2 手势) |

未知地址仍会被探到(`known_name=NULL`),不等于"没插东西"——PORT.A 上任何能应答 I2C
探测的器件都会出现在结果里,只是本组件不认识它。

## 坑位

- **本组件不做总线拉死预检**(那需要具体 GPIO 号,是 `core2_board` 的职责):总线被
  拉死时,`unit_probe_scan` 会对全部 112 个地址逐一 timeout(~50ms×112 ≈ 5.6s),体验
  上是"很慢地全部失败"。调用前先 `core2_board_port_a_stuck()` 自查,拉死就跳过扫描、
  直接提示检查供电/线缆(症状排查见 `core2_board_port_a_scan()` 头注释)。
- 扫描本身用 `i2c_master_probe`(50ms 超时/地址),112 个地址全扫最坏情况耗时数秒,
  `unit_bench` 的 Rescan 按钮应异步跑(不阻塞 UI)。
