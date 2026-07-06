#include "unit_chain_joystick.h"

#include "chain_bus.h"

// Chain Joystick 命令码(M5Chain ChainJoystick.hpp / ChainCommon.hpp)
#define JOY_GET_16ADC     0x30   // 应答载荷 = x(uint16 小端) + y(uint16 小端)
#define BTN_GET_STATUS    0xE1   // 应答载荷[0] = 1 按下 / 0 松开

#define REQ_TIMEOUT_MS    40

esp_err_t unit_chain_joystick_probe(uint8_t id)
{
    chain_dev_type_t type;
    esp_err_t err = chain_bus_get_device_type(id, &type, 60);
    if (err != ESP_OK) return err;
    return (type == CHAIN_DEV_JOYSTICK) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t unit_chain_joystick_read_adc(uint8_t id, uint16_t *x, uint16_t *y)
{
    if (!x || !y) return ESP_ERR_INVALID_ARG;
    uint8_t p[4]; uint8_t n = 0;
    esp_err_t err = chain_bus_request(id, JOY_GET_16ADC, NULL, 0, p, sizeof(p), &n, REQ_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    if (n < 4) return ESP_ERR_INVALID_RESPONSE;
    *x = (uint16_t)(p[0] | (p[1] << 8));
    *y = (uint16_t)(p[2] | (p[3] << 8));
    return ESP_OK;
}

esp_err_t unit_chain_joystick_read_button(uint8_t id, bool *pressed)
{
    if (!pressed) return ESP_ERR_INVALID_ARG;
    uint8_t p[1]; uint8_t n = 0;
    esp_err_t err = chain_bus_request(id, BTN_GET_STATUS, NULL, 0, p, sizeof(p), &n, REQ_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    if (n < 1) return ESP_ERR_INVALID_RESPONSE;
    *pressed = (p[0] != 0);
    return ESP_OK;
}
