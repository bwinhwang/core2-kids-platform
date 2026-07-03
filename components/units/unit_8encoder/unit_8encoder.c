#include "unit_8encoder.h"

#include "esp_log.h"

static const char *TAG = "unit_8encoder";

// ── 寄存器映射(docs/units/Unit_8Encoder.md §3,V1 FW)─────────────────
#define REG_COUNTER_BASE    0x00   // Cnt0..7,各 4 字节小端 int32,R/W
#define REG_INCREMENT_BASE  0x20   // Inc0..7,各 4 字节小端 int32,读后自动清零
#define REG_CNT_RESET_BASE  0x40   // Cnt0..7,各 1 字节,写 1 复位
#define REG_BUTTON_BASE     0x50   // BNT0..7,各 1 字节,0~1
#define REG_SWITCH          0x60   // 1 字节,0~1
#define REG_RGB_BASE        0x70   // LED0..8,各 3 字节 R,G,B
#define REG_FW_VERSION      0xFE   // 1 字节

// STM32 从机,100kHz 保守稳妥(数据量小:一次轮询也就 ~17 个小事务)
#define I2C_SPEED_HZ        100000
#define I2C_TIMEOUT_MS      50

static i2c_master_dev_handle_t s_dev;

// ── 寄存器读写小工具(与官方库同粒度:一个值一次事务)──────────────────
static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

static esp_err_t reg_write(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[8] = { reg };
    if (len > sizeof(buf) - 1) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < len; i++) buf[1 + i] = data[i];
    return i2c_master_transmit(s_dev, buf, len + 1, I2C_TIMEOUT_MS);
}

static esp_err_t read_i32(uint8_t reg, int32_t *out)
{
    uint8_t b[4];
    esp_err_t err = reg_read(reg, b, 4);
    if (err != ESP_OK) return err;
    // 小端拼装(文档 §3:byte0 为低字节),按有符号 32 位解析(会回绕,勿按无符号)
    *out = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                     ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
    return ESP_OK;
}

esp_err_t unit_8encoder_init(i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (addr == 0) addr = UNIT_8ENCODER_ADDR_DEFAULT;

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

    // 在位检查:读固件版本(单元没插/没电时这里失败——注意 PORT.A 5V 靠 EXTEN)
    uint8_t fw = 0;
    esp_err_t err = reg_read(REG_FW_VERSION, &fw, 1);
    if (err != ESP_OK) {
        // 错误码解读(IDF v6 i2c_master):INVALID_RESPONSE=NACK(总线通但 0x41 没人应答
        // → 多半插错口:红色 I2C 口在 Core2 机身侧面,底座黑/蓝口不是 I2C;或地址被改);
        // TIMEOUT=总线拉死(线缆/供电/上拉问题)。用 core2_board_port_a_scan() 一扫便知。
        ESP_LOGW(TAG, "8Encoder 无响应(%s):%s", esp_err_to_name(err),
                 err == ESP_ERR_INVALID_RESPONSE
                     ? "0x41 无应答——插错口(要插红色 PORT.A)?地址被改?"
                     : "总线异常——线缆/供电(M-Bus 5V/EXTEN)?");
        return err;
    }

    // 清掉上电以来的存量增量,避免第一帧"凭空转了一大截"
    int32_t junk;
    for (int i = 0; i < UNIT_8ENCODER_NUM_ENC; i++) {
        (void)read_i32(REG_INCREMENT_BASE + 4 * i, &junk);
    }

    ESP_LOGI(TAG, "8Encoder 就绪 @0x%02X(FW v%u)", addr, fw);
    return ESP_OK;
}

esp_err_t unit_8encoder_read_increment(int idx, int32_t *inc)
{
    if (idx < 0 || idx >= UNIT_8ENCODER_NUM_ENC || !inc) return ESP_ERR_INVALID_ARG;
    return read_i32(REG_INCREMENT_BASE + 4 * idx, inc);
}

esp_err_t unit_8encoder_read_increments(int32_t inc[UNIT_8ENCODER_NUM_ENC])
{
    if (!inc) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < UNIT_8ENCODER_NUM_ENC; i++) {
        esp_err_t err = read_i32(REG_INCREMENT_BASE + 4 * i, &inc[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t unit_8encoder_read_button(int idx, bool *pressed)
{
    if (idx < 0 || idx >= UNIT_8ENCODER_NUM_ENC || !pressed) return ESP_ERR_INVALID_ARG;
    uint8_t v = 0;
    esp_err_t err = reg_read(REG_BUTTON_BASE + idx, &v, 1);
    if (err != ESP_OK) return err;
#if UNIT_8ENCODER_BTN_ACTIVE_LOW
    *pressed = (v == 0);
#else
    *pressed = (v != 0);
#endif
    return ESP_OK;
}

esp_err_t unit_8encoder_read_buttons(bool pressed[UNIT_8ENCODER_NUM_ENC])
{
    if (!pressed) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < UNIT_8ENCODER_NUM_ENC; i++) {
        esp_err_t err = unit_8encoder_read_button(i, &pressed[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t unit_8encoder_read_switch(bool *on)
{
    if (!on) return ESP_ERR_INVALID_ARG;
    uint8_t v = 0;
    esp_err_t err = reg_read(REG_SWITCH, &v, 1);
    if (err != ESP_OK) return err;
    *on = (v != 0);
    return ESP_OK;
}

esp_err_t unit_8encoder_set_led(int idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx < 0 || idx >= UNIT_8ENCODER_NUM_LEDS) return ESP_ERR_INVALID_ARG;
    uint8_t rgb[3] = { r, g, b };
    return reg_write(REG_RGB_BASE + 3 * idx, rgb, 3);
}

esp_err_t unit_8encoder_read_counter(int idx, int32_t *val)
{
    if (idx < 0 || idx >= UNIT_8ENCODER_NUM_ENC || !val) return ESP_ERR_INVALID_ARG;
    return read_i32(REG_COUNTER_BASE + 4 * idx, val);
}

esp_err_t unit_8encoder_reset_counter(int idx)
{
    if (idx < 0 || idx >= UNIT_8ENCODER_NUM_ENC) return ESP_ERR_INVALID_ARG;
    uint8_t one = 1;
    return reg_write(REG_CNT_RESET_BASE + idx, &one, 1);
}

esp_err_t unit_8encoder_fw_version(uint8_t *ver)
{
    if (!ver) return ESP_ERR_INVALID_ARG;
    return reg_read(REG_FW_VERSION, ver, 1);
}
