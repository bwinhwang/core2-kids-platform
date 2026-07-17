# data_log —— 串口 CSV 导出(v1)

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
- **v1 只做串口 CSV**,SPIFFS 离线录制(`rec_start/stop/dump`,给 `power_lab` P4 拔 USB
  录放电曲线用)规划中,尚未实现。

## 已知限制

- 与 `components/screenshot` 共享同一条 UART0:截屏取景窗口内 `esp_log_set_vprintf`
  只静默 `ESP_LOGx`,**不拦截**本组件的裸 `printf`,若截屏时同时高频打点,两路输出可能
  交织。v1 不处理这个边界情况——典型用法是分开操作:先截图确认布局,再录数据。
- 高频打点(如 chart 10Hz 推点顺带每帧 Log)会占用不少 UART0 带宽(115200bps),评估台
  单元读数量级(几十 Hz 内)通常没问题,若感觉丢行/卡顿先降低 Log 频率。
