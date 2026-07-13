#include "screenshot.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "driver/uart.h"
#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

#ifndef CONFIG_LV_USE_SNAPSHOT
#error "需要 CONFIG_LV_USE_SNAPSHOT=y(sdkconfig.platform 已加)。本工程 sdkconfig 是旧的:rm -f sdkconfig && idf.py fullclean && build(AGENTS.md §3 仪式)"
#endif

static const char *TAG = "screenshot";

#define SHOT_UART        CONFIG_ESP_CONSOLE_UART_NUM
#define SHOT_EXT_MARGIN  16   /* 屏对象 ext_draw_size 外扩余量(像素,每边) */

static bool s_inited;

/* ── Base64(避免引入 mbedtls 依赖,26 行自足)───────────────────── */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t b64_line(const uint8_t *in, size_t n, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = in[i] << 16;
        if (i + 1 < n) v |= in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? B64[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

/* ── RLE(rle16:u8 游程 1..255 + u16le 像素;幼儿风大色块典型 5~15×)── */
static size_t rle16_encode(const lv_draw_buf_t *db, uint8_t *out)
{
    uint32_t w = db->header.w, h = db->header.h, stride = db->header.stride;
    size_t o = 0;
    for (uint32_t y = 0; y < h; y++) {
        const uint16_t *row = (const uint16_t *)(db->data + y * stride);
        uint32_t x = 0;
        while (x < w) {
            uint16_t px = row[x];
            uint32_t run = 1;
            while (x + run < w && row[x + run] == px && run < 255) run++;
            out[o++] = (uint8_t)run;
            out[o++] = px & 0xFF;
            out[o++] = px >> 8;
            x += run;
        }
    }
    return o;
}

/* 导出期间静默 ESP_LOG,防别的任务日志把 Base64 行拦腰截断 */
static int null_vprintf(const char *fmt, va_list ap)
{
    (void)fmt; (void)ap;
    return 0;
}

esp_err_t screenshot_dump_now(void)
{
    /* 1) 抓帧:PSRAM 备好带外扩余量的 draw buf,持 LVGL 锁渲染活动屏 */
    const uint32_t max_w = BSP_LCD_H_RES + 2 * SHOT_EXT_MARGIN;
    const uint32_t max_h = BSP_LCD_V_RES + 2 * SHOT_EXT_MARGIN;
    const size_t   pix_sz = max_w * max_h * 2;
    uint8_t *pix = heap_caps_malloc(pix_sz, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(pix, ESP_ERR_NO_MEM, TAG, "PSRAM 抓帧缓冲分配失败(%u B)", (unsigned)pix_sz);

    lv_draw_buf_t db;
    lv_result_t res = LV_RESULT_INVALID;
    if (lv_draw_buf_init(&db, max_w, max_h, LV_COLOR_FORMAT_RGB565, 0, pix, pix_sz) == LV_RESULT_OK) {
        bsp_display_lock(0);
        res = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &db);
        bsp_display_unlock();
    }
    if (res != LV_RESULT_OK) {
        free(pix);
        ESP_LOGE(TAG, "lv_snapshot 失败(CONFIG_LV_USE_SNAPSHOT?活动屏尺寸异常?)");
        return ESP_FAIL;
    }

    /* 2) RLE 压缩(最坏 3B/像素,PSRAM 放得下) */
    const uint32_t w = db.header.w, h = db.header.h;
    uint8_t *rle = heap_caps_malloc((size_t)w * h * 3 + 16, MALLOC_CAP_SPIRAM);
    if (!rle) {
        free(pix);
        ESP_LOGE(TAG, "PSRAM RLE 缓冲分配失败");
        return ESP_ERR_NO_MEM;
    }
    size_t rle_len = rle16_encode(&db, rle);
    uint32_t crc = esp_rom_crc32_le(0, rle, rle_len);
    free(pix);

    /* 3) 带标记吐串口:$ 前缀 Base64 行,首尾哨兵行,主机侧按行过滤解码 */
    vprintf_like_t old_vp = esp_log_set_vprintf(null_vprintf);
    const size_t CHUNK = 57;                    /* 57B → 76 字符/行 */
    char line[2 + (CHUNK + 2) / 3 * 4 + 1];
    printf("\n<<<SHOT w=%u h=%u fmt=rgb565le enc=rle16 raw=%u rle=%u crc32=%08x>>>\n",
           (unsigned)w, (unsigned)h, (unsigned)(w * h * 2), (unsigned)rle_len, (unsigned)crc);
    for (size_t i = 0; i < rle_len; i += CHUNK) {
        size_t n = rle_len - i < CHUNK ? rle_len - i : CHUNK;
        line[0] = '$';
        b64_line(rle + i, n, line + 1);
        puts(line);                             /* puts 自带 \n */
    }
    printf("<<<SHOT-END>>>\n");
    fflush(stdout);
    esp_log_set_vprintf(old_vp);
    free(rle);

    ESP_LOGI(TAG, "已导出 %ux%u,RLE %u B(%.1fx)", (unsigned)w, (unsigned)h,
             (unsigned)rle_len, (double)w * h * 2 / rle_len);
    return ESP_OK;
}

/* ── 串口监听任务:收到一行 "SHOT" 即导出 ───────────────────────── */
static void shot_task(void *arg)
{
    char cmd[16];
    size_t len = 0;
    for (;;) {
        uint8_t ch;
        if (uart_read_bytes(SHOT_UART, &ch, 1, portMAX_DELAY) != 1) continue;
        if (ch == '\n' || ch == '\r') {
            cmd[len] = '\0';
            if (strcmp(cmd, "SHOT") == 0) screenshot_dump_now();
            len = 0;
        } else if (len < sizeof(cmd) - 1) {
            cmd[len++] = (char)ch;
        } else {
            len = 0;   /* 行超长(日志回环等杂音),丢弃重来 */
        }
    }
}

esp_err_t screenshot_init(void)
{
    if (s_inited) return ESP_OK;

    /* 控制台 UART 默认无驱动(日志 TX 走轮询 vfs,不受影响);装驱动只为
     * 中断收 RX。不动 uart_param_config/uart_set_pin —— 波特率引脚沿用
     * 控制台既有配置。Confirmed via espressif-docs(ESP-FAQ:UART0 日志
     * 输出与主机输入可并存;console 例程同款 install)。 */
    if (!uart_is_driver_installed(SHOT_UART)) {
        ESP_RETURN_ON_ERROR(uart_driver_install(SHOT_UART, 256, 0, 0, NULL, 0),
                            TAG, "UART%d 驱动安装失败", SHOT_UART);
    }
    ESP_RETURN_ON_FALSE(
        xTaskCreate(shot_task, "screenshot", 10240, NULL, 2, NULL) == pdPASS,
        ESP_ERR_NO_MEM, TAG, "监听任务创建失败");
    s_inited = true;
    ESP_LOGI(TAG, "串口截图就绪(发 \"SHOT\\n\" 触发,主机用 tools/screenshot.py)");
    return ESP_OK;
}
