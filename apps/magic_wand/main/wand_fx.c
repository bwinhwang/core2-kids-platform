#include "wand_fx.h"

#include "esp_log.h"

#include "tuning.h"

static const char *TAG = "wand_fx";
static bool s_ready;

void wand_fx_start(void)
{
    esp_err_t err = unit_rgb_init();
    s_ready = (err == ESP_OK);
    if (s_ready) {
        unit_rgb_set_max_brightness(WAND_MAX_BRIGHTNESS);
    } else {
        ESP_LOGW(TAG, "unit_rgb 未就位(%s):魔法棒灯这一条反馈通道缺席,不影响其余三通道",
                 esp_err_to_name(err));
    }
}

void wand_fx_trigger(wand_fx_t fx)
{
    if (s_ready) unit_rgb_trigger(fx);
}
