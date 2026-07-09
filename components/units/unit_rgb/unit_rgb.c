#include "unit_rgb.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "unit_rgb";

#define FRAME_MS         25

// 索引约定:0=根(靠近握把)… (UNIT_RGB_COUNT-1)=尖(灯珠串接末端)
#define IDX_ROOT   0
#define IDX_MID    1
#define IDX_TIP    (UNIT_RGB_COUNT - 1)

#define GROW_FRAMES      12   // 3 级 × 4 帧(~120ms/级)
#define STARRAIN_FRAMES   6   // 3 步 × 2 帧
#define SWEEP_FRAMES      4
#define FLASH_FRAMES      5
#define DIMPOP_FRAMES     6
#define WHIRL_FRAMES      6   // 1.5 圈(3 像素 ×2)
#define RAINBOW_FRAMES    8
#define SHIMMER_FRAMES    2

static led_strip_handle_t s_strip;
static uint8_t s_max_bright = 60;

static volatile int s_fx = -1;   // 当前瞬态(-1=无,回 OFF)
static volatile int s_fx_frame = 0;

static inline uint8_t scale(uint8_t c)
{
    return (uint8_t)((uint16_t)c * s_max_bright / 255);
}

static void put(int i, uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, i, scale(r), scale(g), scale(b));
}

static void fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < UNIT_RGB_COUNT; i++) put(i, r, g, b);
}

static bool render_fx(int frame)
{
    switch (s_fx) {
        case WAND_FX_GROW: {
            int level = frame / 4;   // 0,1,2
            fill(0, 0, 0);
            for (int i = 0; i <= level && i < UNIT_RGB_COUNT; i++) put(i, 0x20, 0xE0, 0x40);
            return frame < GROW_FRAMES;
        }
        case WAND_FX_STARRAIN: {
            int step = frame / 2;   // 0,1,2 = tip→root
            fill(0, 0, 0);
            if ((frame % 2) == 0 && step < UNIT_RGB_COUNT) {
                put(IDX_TIP - step, 0xC0, 0xD8, 0xFF);
            }
            return frame < STARRAIN_FRAMES;
        }
        case WAND_FX_SWEEP_ROOT2TIP: {
            fill(0, 0, 0);
            if (frame < UNIT_RGB_COUNT) put(frame, 0xFF, 0xD2, 0x3F);
            if (frame - 1 >= 0 && frame - 1 < UNIT_RGB_COUNT) put(frame - 1, 0x50, 0x40, 0x08);
            return frame < SWEEP_FRAMES;
        }
        case WAND_FX_SWEEP_TIP2ROOT: {
            fill(0, 0, 0);
            int head = IDX_TIP - frame;
            if (head >= 0) put(head, 0xFF, 0xD2, 0x3F);
            if (head + 1 <= IDX_TIP) put(head + 1, 0x50, 0x40, 0x08);
            return frame < SWEEP_FRAMES;
        }
        case WAND_FX_FLASH_WHITE: {
            float k = 1.0f - fabsf((float)frame / FLASH_FRAMES * 2.0f - 1.0f);
            fill((uint8_t)(0xFF * k), (uint8_t)(0xFF * k), (uint8_t)(0xFF * k));
            return frame < FLASH_FRAMES;
        }
        case WAND_FX_DIM_POP: {
            if (frame < DIMPOP_FRAMES / 2) fill(0x18, 0x10, 0x04);
            else fill(0xFF, 0xE0, 0x60);
            return frame < DIMPOP_FRAMES;
        }
        case WAND_FX_WHIRL_CW: {
            fill(0, 0, 0);
            put(frame % UNIT_RGB_COUNT, 0xB0, 0x60, 0xFF);
            return frame < WHIRL_FRAMES;
        }
        case WAND_FX_WHIRL_CCW: {
            fill(0, 0, 0);
            put(IDX_TIP - (frame % UNIT_RGB_COUNT), 0xB0, 0x60, 0xFF);
            return frame < WHIRL_FRAMES;
        }
        case WAND_FX_RAINBOW: {
            static const uint8_t hues[3][3] = { {0xFF,0x40,0x30}, {0x40,0xFF,0x50}, {0x40,0x80,0xFF} };
            fill(0, 0, 0);
            int lit = frame / 2 + 1;
            if (lit > UNIT_RGB_COUNT) lit = UNIT_RGB_COUNT;
            for (int i = 0; i < lit; i++) put(i, hues[i][0], hues[i][1], hues[i][2]);
            return frame < RAINBOW_FRAMES;
        }
        case WAND_FX_SHIMMER: {
            fill(0, 0, 0);
            if (frame == 0) put(IDX_MID, 0xE0, 0xE8, 0xFF);
            return frame < SHIMMER_FRAMES;
        }
        default:
            return false;
    }
}

static void rgb_task(void *arg)
{
    for (;;) {
        if (s_fx >= 0) {
            bool cont = render_fx(s_fx_frame);
            s_fx_frame++;
            if (!cont) { s_fx = -1; fill(0, 0, 0); }
        }
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

esp_err_t unit_rgb_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = UNIT_RGB_GPIO,
        .max_leds = UNIT_RGB_COUNT,
        .led_model = LED_MODEL_SK6812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device 失败: %s", esp_err_to_name(err));
        return err;
    }
    led_strip_clear(s_strip);

    if (xTaskCreate(rgb_task, "wandrgb", 3072, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "魔法棒灯就绪：%d× SK6812 @ G%d", UNIT_RGB_COUNT, UNIT_RGB_GPIO);
    return ESP_OK;
}

void unit_rgb_set_max_brightness(uint8_t max) { s_max_bright = max; }

void unit_rgb_trigger(wand_fx_t fx)
{
    s_fx_frame = 0;
    s_fx = fx;
}
