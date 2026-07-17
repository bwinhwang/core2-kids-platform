// data_log —— 串口 CSV 导出(v1,自动时间戳打点)
//
// 走 UART0(与日志/串口截图同一条物理线),用哨兵行包住一段 CSV,主机侧
// `tools/serial_capture.py` 按哨兵提取成 .csv 文件。第一列固定是毫秒时间戳
// (`esp_timer_get_time()/1000`),调用方 `cols_csv`/`data_log_row` 只需给其余列。
//
// 用法:
//   data_log_begin("dlight", "lux");      // 表头会打成 "#CSV-BEGIN dlight\nts_ms,lux\n"
//   data_log_row("%.1f", lux);            // 一行:"<ts_ms>,<lux>\n"
//   data_log_end();                       // "#CSV-END\n"
//
// ⚠️ 走原始 printf(不经 ESP_LOG,不带时间戳/TAG 前缀,保证 CSV 干净),与
// `components/screenshot` 共享同一条 UART0——截屏取景窗口内 `esp_log_set_vprintf`
// 只静默 ESP_LOGx,不拦截本组件的 printf,若截屏时同时高频打点,两路输出可能交织;
// v1 不处理这个边界情况(评估台典型用法是分开操作:先截图确认布局,再录数据)。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 打 CSV 开始哨兵 + 表头(自动加 "ts_ms" 首列)。 */
void data_log_begin(const char *name, const char *cols_csv);

/** @brief 打一行数据:自动打时间戳(ms)+ 逗号,再原样格式化 fmt/变参,末尾加换行。 */
void data_log_row(const char *fmt, ...);

/** @brief 打 CSV 结束哨兵。 */
void data_log_end(void);

#ifdef __cplusplus
}
#endif
