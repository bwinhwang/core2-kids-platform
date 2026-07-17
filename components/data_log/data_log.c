#include "data_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

static const char *TAG = "data_log";

void data_log_begin(const char *name, const char *cols_csv)
{
    printf("#CSV-BEGIN %s\n", name ? name : "");
    printf("ts_ms,%s\n", cols_csv ? cols_csv : "");
}

void data_log_row(const char *fmt, ...)
{
    printf("%lld,", (long long)(esp_timer_get_time() / 1000));
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

void data_log_end(void)
{
    printf("#CSV-END\n");
}

// ── SPIFFS 离线录制(v2)──────────────────────────────────────────────────────
//
// 分区标签固定为 "storage"(partitions.csv 唯一真源已定义该 spiffs 分区),挂载点
// 固定 "/spiffs"。只服务"单文件顺序写、之后整体读回"这一种用法(power_lab 的续航
// 录制场景),不做多文件管理/目录遍历——需要更复杂用法时再扩展,不在本轮范围内。

#define REC_BASE_PATH  "/spiffs"
#define REC_PARTITION  "storage"
#define REC_MAX_FILES  2          // 同时只会有一个录制文件在写或在读,留 1 个余量
#define REC_NAME_MAX   24         // 留够 "/spiffs/" + name + ".csv" 不越过 SPIFFS 对象名上限

static bool  s_spiffs_mounted = false;
static FILE *s_rec_fp = NULL;
static char  s_rec_path[REC_NAME_MAX + 16];   // "/spiffs/<name>.csv"

static esp_err_t ensure_mounted(void)
{
    if (s_spiffs_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path              = REC_BASE_PATH,
        .partition_label        = REC_PARTITION,
        .max_files              = REC_MAX_FILES,
        .format_if_mount_failed = true,   // 首次挂载 / 分区损坏时自动格式化,耗时可达数秒
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS 挂载失败(%s),分区=%s", esp_err_to_name(err), REC_PARTITION);
        return err;
    }
    s_spiffs_mounted = true;

    size_t total = 0, used = 0;
    if (esp_spiffs_info(REC_PARTITION, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS 就绪:分区 %s 已用 %u/%u bytes", REC_PARTITION,
                 (unsigned)used, (unsigned)total);
    }
    return ESP_OK;
}

esp_err_t data_log_rec_start(const char *name, const char *cols_csv)
{
    esp_err_t err = ensure_mounted();
    if (err != ESP_OK) return err;

    if (s_rec_fp) {   // 上一段录制忘了 rec_stop:先自动收尾,避免文件句柄泄漏
        ESP_LOGW(TAG, "rec_start 时仍有未关闭的录制,自动 rec_stop 后开新段");
        data_log_rec_stop();
    }

    const char *n = (name && name[0]) ? name : "rec";
    snprintf(s_rec_path, sizeof(s_rec_path), "%s/%.*s.csv", REC_BASE_PATH, REC_NAME_MAX, n);

    s_rec_fp = fopen(s_rec_path, "w");   // "w" = 每次 rec_start 截断重开一段新录制,同 v1 语义
    if (!s_rec_fp) {
        ESP_LOGE(TAG, "打开 %s 写入失败", s_rec_path);
        return ESP_FAIL;
    }

    fprintf(s_rec_fp, "#CSV-BEGIN %s\n", n);
    fprintf(s_rec_fp, "ts_ms,%s\n", cols_csv ? cols_csv : "");
    fflush(s_rec_fp);
    return ESP_OK;
}

esp_err_t data_log_rec_row(const char *fmt, ...)
{
    if (!s_rec_fp) return ESP_ERR_INVALID_STATE;

    fprintf(s_rec_fp, "%lld,", (long long)(esp_timer_get_time() / 1000));
    va_list args;
    va_start(args, fmt);
    vfprintf(s_rec_fp, fmt, args);
    va_end(args);
    fprintf(s_rec_fp, "\n");
    return ESP_OK;
}

esp_err_t data_log_rec_stop(void)
{
    if (!s_rec_fp) return ESP_OK;   // 未在录制:空操作,不算错误
    fflush(s_rec_fp);
    fclose(s_rec_fp);
    s_rec_fp = NULL;
    return ESP_OK;
}

bool data_log_rec_active(void)
{
    return s_rec_fp != NULL;
}

esp_err_t data_log_rec_dump(void)
{
    if (s_rec_fp) data_log_rec_stop();   // 边录边 dump 没意义,先收尾确保内容已落盘

    if (s_rec_path[0] == '\0') {
        ESP_LOGW(TAG, "rec_dump:从未 rec_start 过,无文件可导出");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ensure_mounted();
    if (err != ESP_OK) return err;

    FILE *f = fopen(s_rec_path, "r");
    if (!f) {
        ESP_LOGW(TAG, "rec_dump:%s 不存在", s_rec_path);
        return ESP_ERR_NOT_FOUND;
    }

    // 文件本身已含 rec_start 写入的 "#CSV-BEGIN"/表头,原样逐行吐出;只在结尾补
    // "#CSV-END" 收尾——与 v1 的哨兵格式完全一致,主机侧提取脚本不需要区分两个来源。
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        fputs(line, stdout);
    }
    fclose(f);
    printf("#CSV-END\n");
    return ESP_OK;
}
