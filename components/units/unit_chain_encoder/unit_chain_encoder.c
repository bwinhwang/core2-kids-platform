#include "unit_chain_encoder.h"

#include "chain_bus.h"

// Chain Encoder 命令码(M5Chain ChainEncoder.hpp / ChainCommon.hpp)
#define ENC_GET_VALUE     0x10   // 应答载荷 = int16 小端(绝对计数)
#define ENC_GET_INC       0x11   // 应答载荷 = int16 小端(增量)
#define ENC_RESET_VALUE   0x13
#define BTN_GET_STATUS    0xE1   // 应答载荷[0] = 1 按下 / 0 松开

#define REQ_TIMEOUT_MS    40

esp_err_t unit_chain_encoder_probe(uint8_t id)
{
    chain_dev_type_t type;
    esp_err_t err = chain_bus_get_device_type(id, &type, 60);
    if (err != ESP_OK) return err;
    return (type == CHAIN_DEV_ENCODER) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t unit_chain_encoder_read_value(uint8_t id, int16_t *value)
{
    if (!value) return ESP_ERR_INVALID_ARG;
    uint8_t p[2]; uint8_t n = 0;
    esp_err_t err = chain_bus_request(id, ENC_GET_VALUE, NULL, 0, p, sizeof(p), &n, REQ_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    if (n < 2) return ESP_ERR_INVALID_RESPONSE;
    *value = (int16_t)(p[0] | (p[1] << 8));
    return ESP_OK;
}

esp_err_t unit_chain_encoder_read_button(uint8_t id, bool *pressed)
{
    if (!pressed) return ESP_ERR_INVALID_ARG;
    uint8_t p[1]; uint8_t n = 0;
    esp_err_t err = chain_bus_request(id, BTN_GET_STATUS, NULL, 0, p, sizeof(p), &n, REQ_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    if (n < 1) return ESP_ERR_INVALID_RESPONSE;
    *pressed = (p[0] != 0);
    return ESP_OK;
}

esp_err_t unit_chain_encoder_reset_value(uint8_t id)
{
    return chain_bus_request(id, ENC_RESET_VALUE, NULL, 0, NULL, 0, NULL, REQ_TIMEOUT_MS);
}
