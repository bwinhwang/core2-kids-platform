#include "shared_state.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static shared_state_t s_state;

void shared_state_init(void)
{
    portENTER_CRITICAL(&s_lock);
    s_state.intensity    = 0.0f;
    s_state.last_peak_ts = 0;
    s_state.peak_mag     = 0.0f;
    s_state.state        = APP_INIT;
    s_state.batt_voltage = 0.0f;
    portEXIT_CRITICAL(&s_lock);
}

void shared_state_set_shake(float intensity, uint32_t last_peak_ts, float peak_mag)
{
    portENTER_CRITICAL(&s_lock);
    s_state.intensity    = intensity;
    s_state.last_peak_ts = last_peak_ts;
    s_state.peak_mag     = peak_mag;
    portEXIT_CRITICAL(&s_lock);
}

void shared_state_set_app_state(app_state_t s)
{
    portENTER_CRITICAL(&s_lock);
    s_state.state = s;
    portEXIT_CRITICAL(&s_lock);
}

void shared_state_set_batt(float v)
{
    portENTER_CRITICAL(&s_lock);
    s_state.batt_voltage = v;
    portEXIT_CRITICAL(&s_lock);
}

void shared_state_get(shared_state_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_lock);
}
