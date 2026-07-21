# data_log —— 串口 CSV 导出(v1)+ SPIFFS 离线录制(v2)

评估台"数据可导出"原则(`CLAUDE.md` §2 原则 3)的落地:把一段数值流打成 CSV,经 UART0
串口导出,主机侧 `tools/serial_capture.py --csv` 按哨兵提取成 `.csv` 文件。

## 用法

```c
data_log_begin("dlight", "lux");   // 打 "#CSV-BEGIN dlight\nts_ms,lux\n"
for (...) {
    data_log_row("%.1f", lux);     // 打 "<ts_ms>,<lux>\n"
}
data_log_end();                    // 打 "#CSV-END\n"
```

主机侧:
```bash
python3 tools/serial_capture.py /dev/ttyUSB0 30 --csv dlight.csv
# 抓 30s;串口原样打印到终端(带 [相对秒数] 前缀调试用),同时把 BEGIN/END 之间
# 的行原样落盘到 dlight.csv(不带前缀,干净 CSV,可直接 Excel/pandas 打开)
```

## 设计取舍

- **走原始 `printf`,不经 `ESP_LOG`**:`ESP_LOGx` 会带时间戳/TAG 前缀,污染 CSV 格式;
  `data_log` 直接 `printf`/`vprintf` 到 stdout(UART0),保持每行纯 `ts_ms,col1,col2,…`。
- **第一列固定时间戳**(`esp_timer_get_time()/1000`,毫秒,自 boot 起算):调用方
  `cols_csv`/`data_log_row` 只需给其余列,不用自己管时间戳。
- **哨兵行**(`#CSV-BEGIN <name>` / `#CSV-END`)让主机侧脚本能从一堆普通日志里精确切出
  数据段,支持一次串口会话里多段导出(如逐个单元轮流 Log)。
- **不做后台采样任务**:`data_log_row` 由调用方在自己的循环里显式调用,推点频率、
  是否节流全归 app 决定(如 `unit_bench` 详情页 2~5Hz 推 chart 时顺带 Log,或独立更高频
  率单独录)。
- **v1 只做串口 CSV**;SPIFFS 离线录制是下面的 v2(`power_lab` P4 拔 USB 录放电曲线场景
  落地,2026-07-17 实现)。

## 已知限制(v1)

- 与 `components/screenshot` 共享同一条 UART0:截屏取景窗口内 `esp_log_set_vprintf`
  只静默 `ESP_LOGx`,**不拦截**本组件的裸 `printf`,若截屏时同时高频打点,两路输出可能
  交织。v1 不处理这个边界情况——典型用法是分开操作:先截图确认布局,再录数据。
- 高频打点(如 chart 10Hz 推点顺带每帧 Log)会占用不少 UART0 带宽(115200bps),评估台
  单元读数量级(几十 Hz 内)通常没问题,若感觉丢行/卡顿先降低 Log 频率。

## v2:SPIFFS 离线录制(`data_log_rec_start/row/stop/dump`)

给"拔 USB 就没串口"的场景用(典型如 `power_lab` 的真实续航测试:必须拔 USB 才能测到
真实放电电流,见 `CLAUDE.md` §11.2)。API:

```c
data_log_rec_start("power_lab", "batt_mv,batt_discharge_ma,vbus_present");
// … 循环里 …
data_log_rec_row("%d,%d,%d", batt_mv, batt_discharge_ma, vbus_present ? 1 : 0);
// … 结束录制(可选,rec_dump 也会自动先收尾)…
data_log_rec_stop();

// 插回 USB 后,用户手动触发一次:
data_log_rec_dump();   // 把整份文件逐行 printf 回 UART0,复用 v1 同一套哨兵格式
```

### 设计取舍

- **后端 `esp_vfs_spiffs`,挂载点固定 `/spiffs`,分区标签固定 `"storage"`**(即
  `partitions.csv` 里 `storage, data, spiffs, 0xD90000, 0x270000` 那一条,~2.4MB,
  唯一真源不在本组件重复定义尺寸/偏移)。
  Confirmed via esp-idf v6.0 本地源码 `$IDF_PATH/components/spiffs/include/esp_spiffs.h`
  (与 espressif-docs MCP 检索结果交叉核对一致):`esp_vfs_spiffs_register()` 接受
  `esp_vfs_spiffs_conf_t{ base_path, partition_label, max_files, format_if_mount_failed }`;
  `format_if_mount_failed=true` 时首次挂载或分区损坏会自动格式化。
- **懒挂载**:第一次调 `rec_start` 才 `esp_vfs_spiffs_register`,之后全局只挂一次
  (`s_spiffs_mounted` 静态标志位)。**首次格式化耗时可达数秒**——这是阻塞调用,调用方
  **不要**在持有 `bsp_display_lock` 时调,建议用"LVGL 回调只改提示文字 + 下一轮主循环
  tick 再真正调用"的两段式,避免顶住 LVGL 渲染(`apps/power_lab` 的录制启停按钮就是
  这么做的,可参考其 `power_lab_ctl.c`)。
- **单文件、截断写、顺序读回**:每次 `rec_start(name, cols)` 都用 `"w"` 模式打开
  `/spiffs/<name>.csv`(截断重开一段新录制,不追加旧内容);`rec_dump()` 把这同一份
  文件整个读回并逐行 `printf`。不做多文件管理/目录遍历/历史归档——评估台的典型用法是
  "录一段、导出一段",复杂用法留给以后按需扩展。
- **哨兵格式与 v1 保持一致**:`rec_start` 往文件里写的第一两行就是
  `"#CSV-BEGIN <name>\nts_ms,<cols_csv>\n"`(与 `data_log_begin` 字节级相同);
  `rec_dump()` 只需在流完文件内容后追加一行 `"#CSV-END\n"` 收尾,`tools/serial_capture.py`
  不需要区分数据是 v1 直播段还是 v2 回放段。
- **`rec_row` 未在录制时返回 `ESP_ERR_INVALID_STATE` 而不是崩溃**:调用方可以无条件地
  在主循环里调用它,不必自己维护"是否正在录制"的额外判断(当然仍提供 `rec_active()`
  给 UI 显示状态用)。
- **不影响 v1**:`data_log_begin/row/end` 三个函数签名/实现完全未改动,新增的
  `rec_*` 系列是独立的静态状态(`s_rec_fp`/`s_spiffs_mounted`),两套 API 可以在同一个
  app 里同时使用而互不干扰(未验证场景,但设计上无共享可变状态)。

### 已知限制(v2)

- **`REC_NAME_MAX=24`**:`rec_start` 的 `name` 超过 24 字符会被截断,留够
  `"/spiffs/" + name + ".csv"` 不撞 SPIFFS 默认对象名长度上限(`CONFIG_SPIFFS_OBJ_NAME_LEN`
  默认 32)。
  未逐字节核实到确切上限触发点(未在实机测过超长文件名的失败行为),按经验值留了余量。
- **`max_files=2`**:同一时刻最多 2 个打开的文件句柄(本组件自身逻辑上任意时刻只会用到
  1 个,留 1 个余量);若调用方自己另外也在用 SPIFFS 开文件,可能撞句柄上限,本组件不
  感知调用方的其它文件操作。
- **无磨损均衡以外的写放大防护**:SPIFFS 本身有磨损均衡,但本组件不做"写满自动清理旧
  数据"这类策略——2.4MB 分区、1Hz 采样几十字节一行,按 `power_lab` 的典型录制时长(几
  分钟到十几分钟)算远不会写满,长时间(数小时以上)连续录制需要调用方自行注意剩余空间
  (`esp_spiffs_info` 已在 `ensure_mounted` 里打过一次日志,可作为排查起点)。
- **未做完整性校验**:意外断电/复位可能导致文件写到一半(最后一行不完整),`rec_dump()`
  不做逐行格式校验,原样吐出——主机侧脚本处理不完整的最后一行需要自己容错。
