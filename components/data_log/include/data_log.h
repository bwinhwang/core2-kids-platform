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

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 打 CSV 开始哨兵 + 表头(自动加 "ts_ms" 首列)。 */
void data_log_begin(const char *name, const char *cols_csv);

/** @brief 打一行数据:自动打时间戳(ms)+ 逗号,再原样格式化 fmt/变参,末尾加换行。 */
void data_log_row(const char *fmt, ...);

/** @brief 打 CSV 结束哨兵。 */
void data_log_end(void);

// ── SPIFFS 离线录制(v2,拔 USB 场景专用)───────────────────────────────────────
//
// 与上面的串口 CSV(v1 begin/row/end)完全独立、互不影响:v1 直接 printf 到 UART0,
// v2 写文件到 SPIFFS(分区标签 "storage",partitions.csv 定义 0xD90000~0x270000,
// ~2.4MB),给 power_lab 这类"拔 USB 就没串口"的续航实测场景用——录完插回 USB,
// 调 data_log_rec_dump() 把整份文件吐回串口,复用同一套哨兵行给
// tools/serial_capture.py 提取。
//
// Confirmed via esp-idf v6.0 本地源码 $IDF_PATH/components/spiffs/include/esp_spiffs.h
// (与 espressif-docs MCP 检索结果一致):esp_vfs_spiffs_register(esp_vfs_spiffs_conf_t*)
// 结构体字段为 { base_path, partition_label, max_files, format_if_mount_failed }。

/**
 * @brief 挂载 SPIFFS(懒加载,只在第一次调用时挂;若分区从未格式化过或已损坏,
 *        `format_if_mount_failed=true` 会自动格式化,**耗时可达数秒**)+ 以截断
 *        写模式打开 `/spiffs/<name>.csv`(每次 rec_start 都是全新一段录制,不追加
 *        旧内容)+ 写入 `"#CSV-BEGIN <name>\nts_ms,<cols_csv>\n"`(与 v1
 *        `data_log_begin` 同一哨兵/表头约定)。
 *
 * ⚠️ 本函数会阻塞(挂载/格式化 + 文件 IO),**不要在持有 `bsp_display_lock` 时调用**
 * (格式化那几秒会顶住 LVGL 渲染);建议在主循环里异步触发,先在 LVGL 回调里改一个
 * "初始化存储中…" 的提示文字并返回,下一轮主循环 tick 再真正调用本函数(用法参考
 * `apps/power_lab` 的录制启停实现)。
 *
 * @return ESP_OK 成功;其余 = 挂载失败(分区不存在/格式化失败)或建文件失败。
 */
esp_err_t data_log_rec_start(const char *name, const char *cols_csv);

/** @brief 录制中格式化写一行(同 v1 `data_log_row` 语义:自动加 `ts_ms` 前缀)。
 *  未在录制状态(没调过 `rec_start`,或已 `rec_stop`)时返回 `ESP_ERR_INVALID_STATE`,
 *  不崩溃、调用方可放心在"可能没开始录制"的循环里无条件调用。 */
esp_err_t data_log_rec_row(const char *fmt, ...);

/** @brief flush + 关闭文件。未在录制状态时是安全的空操作,返回 `ESP_OK`。 */
esp_err_t data_log_rec_stop(void);

/** @brief 当前是否正在录制(供 UI 显示"录制中/未录制"状态用)。 */
bool data_log_rec_active(void);

/**
 * @brief 把最近一次录制的文件整个逐行 `printf` 回 UART0(裸 printf,同 v1 风格,
 *        不经 ESP_LOG)。文件本身已含 `rec_start` 写入的 `#CSV-BEGIN`/表头,本函数
 *        只需在流完文件内容后补一行 `#CSV-END` 收尾——与 v1 的哨兵格式完全一致,
 *        `tools/serial_capture.py` 不需要区分是 v1 直播段还是 v2 回放段。
 *        若调用时仍在录制中,会先自动 `rec_stop()` 确保文件内容落盘完整。
 *
 * @return ESP_OK 成功;ESP_ERR_NOT_FOUND 从未 `rec_start` 过 / 文件不存在。
 */
esp_err_t data_log_rec_dump(void);

#ifdef __cplusplus
}
#endif
