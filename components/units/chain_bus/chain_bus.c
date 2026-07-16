#include "chain_bus.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "chain_bus";

// 🔴 总线事务互斥:一条 UART 上的一问一答必须整体串行。多个任务合法并发使用同一节点
// (如 game_task 每帧 poll 读 ADC、feedback_task 事件写节点 RGB),若不加锁,一方的
// uart_flush_input 会清掉另一方在途应答 → 双方都匹配不到自己的帧、各自跑满超时(40ms/笔),
// poll 每帧两笔 ~80ms 远超帧周期 → 轮询任务空转饿死 IDLE → task_wdt。全事务函数共用此锁。
static SemaphoreHandle_t s_lock;

// ── 帧常量(M5Chain ChainCommon.hpp,逐字节对齐)──────────────────────
#define PK_HEAD_HI   0xAA
#define PK_HEAD_LO   0x55
#define PK_END_HI    0x55
#define PK_END_LO    0xAA
#define PK_MIN       9        // head(2)+len(2)+id(1)+cmd(1)+crc(1)+end(2)

// 通用命令码
#define CMD_SET_RGB_VALUE   0x20
#define CMD_SET_RGB_LIGHT   0x22
#define CMD_GET_VERSION     0xFA
#define CMD_GET_DEV_TYPE    0xFB

#define SCAN_BUF        128   // 应答帧都很短(≤~16B),128 足够容一整帧 + 残尾
#define RX_RING         1024

static uart_port_t s_uart = -1;

// data 之外的开销 = 9 字节;请求载荷上限留足(RGB 一次也就几字节)
#define TX_DATA_MAX     32

esp_err_t chain_bus_init(uart_port_t uart, int tx_pin, int rx_pin)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) { ESP_LOGE(TAG, "总线锁创建失败"); return ESP_ERR_NO_MEM; }
    }
    if (uart_is_driver_installed(uart)) {
        s_uart = uart;
        return ESP_OK;
    }
    const uart_config_t cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(uart, RX_RING, 0, 0, NULL, 0);
    if (err != ESP_OK) { ESP_LOGE(TAG, "uart_driver_install 失败: %s", esp_err_to_name(err)); return err; }
    err = uart_param_config(uart, &cfg);
    if (err != ESP_OK) { ESP_LOGE(TAG, "uart_param_config 失败: %s", esp_err_to_name(err)); return err; }
    err = uart_set_pin(uart, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) { ESP_LOGE(TAG, "uart_set_pin 失败: %s", esp_err_to_name(err)); return err; }

    s_uart = uart;
    ESP_LOGI(TAG, "Chain host 就绪:UART%d 115200 8N1 (TX=G%d RX=G%d)", uart, tx_pin, rx_pin);
    return ESP_OK;
}

esp_err_t chain_bus_init_port_c(void)
{
    return chain_bus_init(CHAIN_BUS_PORTC_UART, CHAIN_BUS_PORTC_TX_PIN, CHAIN_BUS_PORTC_RX_PIN);
}

// crc8 = (id + cmd + data...) & 0xFF,即帧中 [4 .. total-4] 的字节和(ChainCommon::calculateCRC)
static uint8_t crc_sum(const uint8_t *p, int from, int to_excl)
{
    uint8_t c = 0;
    for (int i = from; i < to_excl; i++) c += p[i];
    return c;
}

// 在 buf[0..*len) 里找匹配 (id,cmd) 的完整帧。
//   命中:拷载荷到 out(截断到 cap),把该帧及其之前的字节从 buf 移除,返回 1。
//   未命中:丢弃前导垃圾/别的完整帧(心跳等),保留可能的半包尾巴,返回 0。
static int scan_for(uint8_t *buf, int *len, uint8_t id, uint8_t cmd,
                    uint8_t *out, uint8_t cap, uint8_t *outlen)
{
    int i = 0;
    while (*len - i >= PK_MIN) {
        if (buf[i] != PK_HEAD_HI || buf[i + 1] != PK_HEAD_LO) { i++; continue; }

        int plen  = buf[i + 2] | (buf[i + 3] << 8);
        int total = 4 + plen + 2;                 // head+len + payload + end
        if (total < PK_MIN || total > SCAN_BUF) { i++; continue; }   // 长度离谱 = 伪头
        if (*len - i < total) break;              // 半包,等更多字节

        // 帧尾 + CRC 校验
        if (buf[i + total - 2] != PK_END_HI || buf[i + total - 1] != PK_END_LO ||
            crc_sum(buf, i + 4, i + total - 3) != buf[i + total - 3]) {
            i++; continue;                        // 校验不过,当伪头跳一字节
        }

        if (buf[i + 4] == id && buf[i + 5] == cmd) {
            int n = total - 9;                    // 载荷长 = len - 3
            if (n < 0) n = 0;
            if (n > cap) n = cap;
            if (out && n > 0) memcpy(out, &buf[i + 6], n);
            if (outlen) *outlen = (uint8_t)n;
            int consumed = i + total;
            memmove(buf, buf + consumed, *len - consumed);
            *len -= consumed;
            return 1;
        }
        i += total;                               // 合法但不是目标(心跳/枚举/别的节点)→ 整帧跳过
    }
    if (i > 0) { memmove(buf, buf + i, *len - i); *len -= i; }   // 压掉前导垃圾
    return 0;
}

esp_err_t chain_bus_request(uint8_t id, uint8_t cmd,
                            const uint8_t *tx_data, uint8_t tx_len,
                            uint8_t *rx_payload, uint8_t rx_cap, uint8_t *rx_len,
                            int timeout_ms)
{
    if (s_uart < 0) return ESP_ERR_INVALID_STATE;
    if (tx_len > TX_DATA_MAX || (tx_len && !tx_data)) return ESP_ERR_INVALID_ARG;
    if (rx_len) *rx_len = 0;

    // 组帧
    uint8_t frame[9 + TX_DATA_MAX];
    int cmdlen = 3 + tx_len;                      // = len 字段(id+cmd+data+crc)
    int total  = 9 + tx_len;
    frame[0] = PK_HEAD_HI;
    frame[1] = PK_HEAD_LO;
    frame[2] = cmdlen & 0xFF;
    frame[3] = (cmdlen >> 8) & 0xFF;
    frame[4] = id;
    frame[5] = cmd;
    if (tx_len) memcpy(&frame[6], tx_data, tx_len);
    frame[total - 3] = crc_sum(frame, 4, total - 3);
    frame[total - 2] = PK_END_HI;
    frame[total - 1] = PK_END_LO;

    // 🔴 从这里到函数返回是一整笔"清输入→发→等应答"事务,必须独占总线(见 s_lock 注释)。
    // 锁最多被持有 ~timeout_ms;等锁的另一任务会正常阻塞让出 CPU,不会饿死 IDLE。
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);

    // 发前清掉积压的主动上报包(心跳等),让本次应答干净
    uart_flush_input(s_uart);
    int w = uart_write_bytes(s_uart, frame, total);
    if (w != total) { if (s_lock) xSemaphoreGive(s_lock); return ESP_FAIL; }
    uart_wait_tx_done(s_uart, pdMS_TO_TICKS(timeout_ms > 0 ? timeout_ms : 20));

    // 收 + 逐帧匹配
    uint8_t buf[SCAN_BUF];
    int buflen = 0;
    esp_err_t result = ESP_ERR_TIMEOUT;
    int64_t t0 = esp_timer_get_time();
    while ((esp_timer_get_time() - t0) / 1000 < timeout_ms) {
        uint8_t chunk[64];
        int n = uart_read_bytes(s_uart, chunk, sizeof(chunk), pdMS_TO_TICKS(4));
        if (n <= 0) continue;
        // 拼进 buf;溢出(收到超长伪数据)则丢掉旧的一半,保住新尾
        if (buflen + n > SCAN_BUF) {
            int keep = SCAN_BUF / 2;
            if (buflen > keep) { memmove(buf, buf + buflen - keep, keep); buflen = keep; }
            else buflen = 0;
            if (n > SCAN_BUF) n = SCAN_BUF;        // 单块超缓冲极不可能,防御性截断
            if (buflen + n > SCAN_BUF) buflen = SCAN_BUF - n;
        }
        memcpy(buf + buflen, chunk, n);
        buflen += n;

        if (scan_for(buf, &buflen, id, cmd, rx_payload, rx_cap, rx_len)) { result = ESP_OK; break; }
    }
    if (s_lock) xSemaphoreGive(s_lock);
    return result;
}

// ── 通用命令封装 ──────────────────────────────────────────────────────
esp_err_t chain_bus_get_device_type(uint8_t id, chain_dev_type_t *type, int timeout_ms)
{
    if (!type) return ESP_ERR_INVALID_ARG;
    uint8_t p[4]; uint8_t n = 0;
    esp_err_t err = chain_bus_request(id, CMD_GET_DEV_TYPE, NULL, 0, p, sizeof(p), &n, timeout_ms);
    if (err != ESP_OK) return err;
    if (n < 2) return ESP_ERR_INVALID_RESPONSE;
    *type = (chain_dev_type_t)(p[0] | (p[1] << 8));   // 小端(payload[0..1])
    return ESP_OK;
}

esp_err_t chain_bus_get_fw_version(uint8_t id, uint8_t *ver, int timeout_ms)
{
    if (!ver) return ESP_ERR_INVALID_ARG;
    uint8_t p[2]; uint8_t n = 0;
    esp_err_t err = chain_bus_request(id, CMD_GET_VERSION, NULL, 0, p, sizeof(p), &n, timeout_ms);
    if (err != ESP_OK) return err;
    if (n < 1) return ESP_ERR_INVALID_RESPONSE;
    *ver = p[0];
    return ESP_OK;
}

esp_err_t chain_bus_set_rgb(uint8_t id, uint8_t index, uint8_t r, uint8_t g, uint8_t b, int timeout_ms)
{
    // 请求载荷 = [index, num=1, r, g, b](ChainCommon::setRGBValue)
    uint8_t d[5] = { index, 1, r, g, b };
    return chain_bus_request(id, CMD_SET_RGB_VALUE, d, sizeof(d), NULL, 0, NULL, timeout_ms);
}

esp_err_t chain_bus_set_rgb_brightness(uint8_t id, uint8_t pct, int timeout_ms)
{
    if (pct > 100) pct = 100;
    uint8_t d[2] = { pct, 0 /* saveToFlash=disable */ };
    return chain_bus_request(id, CMD_SET_RGB_LIGHT, d, sizeof(d), NULL, 0, NULL, timeout_ms);
}

int chain_bus_sniff(int ms)
{
    if (s_uart < 0) return 0;
    uint8_t buf[128];
    int count = 0;
    int64_t t0 = esp_timer_get_time();
    while ((esp_timer_get_time() - t0) / 1000 < ms) {
        int n = uart_read_bytes(s_uart, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (n > 0) {
            ESP_LOG_BUFFER_HEXDUMP(TAG, buf, n, ESP_LOG_INFO);
            count += n;
        }
    }
    if (count == 0) ESP_LOGW(TAG, "sniff:%dms 内 PORT.C 一个字节都没收到(节点没供电/没插/接反/直连 host 不成立?)", ms);
    else            ESP_LOGI(TAG, "sniff:共收到 %d 字节", count);
    return count;
}
