#include "unit_scd41.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "unit_scd41";

// 命令码(16-bit,大端发送)。Confirmed via esp-idf-lib/scd4x.c + Sensirion SCD4x datasheet。
#define CMD_START_PERIODIC  0x21B1   // 启动周期测量(执行 ~1ms;首个数据 ~5s 后就绪)
#define CMD_READ_MEAS       0xEC05   // 读测量值(~1ms,回 9 字节)
#define CMD_DATA_READY      0xE4B8   // 读数据就绪状态(~1ms,回 3 字节)
#define CMD_STOP_PERIODIC   0x3F86   // 停止周期测量(执行 500ms)

#define STOP_EXEC_MS    500          // stop 执行时间(datasheet:命令后需等 500ms)
#define CMD_EXEC_MS     1            // start/read/data_ready 执行时间
#define I2C_SPEED_HZ    100000       // 100kHz 保守稳妥(SCD4x 支持到 100kHz)
#define I2C_TIMEOUT_MS  50

static i2c_master_dev_handle_t s_dev;

// CRC-8:poly 0x31,init 0xFF,不反射,无最终异或(Sensirion 标准)。
static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t send_cmd(uint16_t cmd)
{
    uint8_t b[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_transmit(s_dev, b, 2, I2C_TIMEOUT_MS);
}

// 发命令 → 等执行时间 → 单独 receive 回 nwords 个字(每字 2 数据 + 1 CRC),逐字校验 CRC。
// 两笔独立事务(命令后 STOP,再单独读),不用 repeated-start。
static esp_err_t read_words(uint16_t cmd, uint16_t *words, int nwords, int exec_ms)
{
    esp_err_t err = send_cmd(cmd);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(exec_ms));

    uint8_t buf[9];   // 至多 3 个字 = 9 字节
    int nbytes = nwords * 3;
    err = i2c_master_receive(s_dev, buf, nbytes, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    for (int i = 0; i < nwords; i++) {
        const uint8_t *p = &buf[i * 3];
        if (crc8(p, 2) != p[2]) return ESP_ERR_INVALID_CRC;
        words[i] = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
    }
    return ESP_OK;
}

esp_err_t unit_scd41_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (addr == 0) addr = UNIT_SCD41_ADDR_DEFAULT;

    if (!s_dev) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = addr,
            .scl_speed_hz    = I2C_SPEED_HZ,
        };
        esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_bus_add_device 失败: %s", esp_err_to_name(err));
            return err;
        }
    }

    // 先 stop 把单元拉回确定态(容忍失败:本就 idle 时 stop 也被接受;残留测量态时必须先
    // stop 才能 start),等满 500ms 执行时间,再 start。start 的返回值兼作在位/健康探测。
    send_cmd(CMD_STOP_PERIODIC);
    vTaskDelay(pdMS_TO_TICKS(STOP_EXEC_MS));

    esp_err_t err = send_cmd(CMD_START_PERIODIC);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SCD41 无响应(%s):%s", esp_err_to_name(err),
                 err == ESP_ERR_INVALID_RESPONSE
                     ? "0x62 无应答——插错口(要插红色 PORT.A)?"
                     : "总线异常——线缆/供电(M-Bus 5V/EXTEN)?");
        return err;
    }

    ESP_LOGI(TAG, "SCD41 就绪 @0x%02X(周期测量已启动,首数据 ~5s 后)", addr);
    return ESP_OK;
}

esp_err_t unit_scd41_data_ready(bool *ready)
{
    if (!s_dev)  return ESP_ERR_INVALID_STATE;
    if (!ready)  return ESP_ERR_INVALID_ARG;

    uint16_t w;
    esp_err_t err = read_words(CMD_DATA_READY, &w, 1, CMD_EXEC_MS);
    if (err != ESP_OK) return err;

    *ready = (w & 0x07FF) != 0;   // 低 11 位非零 = 有新数据
    return ESP_OK;
}

esp_err_t unit_scd41_read(uint16_t *co2_ppm, float *temp_c, float *rh_pct)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint16_t w[3];
    esp_err_t err = read_words(CMD_READ_MEAS, w, 3, CMD_EXEC_MS);
    if (err != ESP_OK) return err;

    if (co2_ppm) *co2_ppm = w[0];                                  // CO2 直接是 ppm
    if (temp_c)  *temp_c  = -45.0f + 175.0f * (float)w[1] / 65536.0f;
    if (rh_pct)  *rh_pct  = 100.0f * (float)w[2] / 65536.0f;
    return ESP_OK;
}
