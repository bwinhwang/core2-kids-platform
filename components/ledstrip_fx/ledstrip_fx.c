#include "ledstrip_fx.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "ledstrip_fx";

#define FRAME_MS        25      // 40fps 动画
#define BUMP_FRAMES     6       // ~150ms
#define COLLECT_FRAMES  10      // ~250ms
#define WIN_FRAMES      64      // ~1.6s
#define SWEEP_FRAMES    18      // ~450ms(busy_knobs 图案彩蛋)
#define GATHER_FRAMES   20      // ~500ms
#define SPREAD_FRAMES   20      // ~500ms
#define FLASH_FRAMES    10      // ~250ms 柔亮起落,非频闪

static led_strip_handle_t s_strip;
static uint8_t s_max_bright = 48;

static volatile led_base_t s_base = LED_BASE_AMBIENT;
static volatile int s_fx = -1;       // 当前瞬态(-1=无)
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
    for (int i = 0; i < LEDSTRIP_COUNT; i++) put(i, r, g, b);
}

// h:0~359 s=v=1 → rgb 0~255
static void hsv(int h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int region = h / 60, rem = h % 60;
    uint8_t p = 0, q = 255 - 255 * rem / 60, t = 255 * rem / 60;
    switch (region) {
        case 0: *r = 255; *g = t;   *b = p;   break;
        case 1: *r = q;   *g = 255; *b = p;   break;
        case 2: *r = p;   *g = 255; *b = t;   break;
        case 3: *r = p;   *g = q;   *b = 255; break;
        case 4: *r = t;   *g = p;   *b = 255; break;
        default:*r = 255; *g = p;   *b = q;   break;
    }
}

static void render_base(int tick)
{
    switch (s_base) {
        case LED_BASE_OFF:
            fill(0, 0, 0);            // 深度省电:全熄
            break;
        case LED_BASE_NEAR:
            fill(0x9F, 0xD0, 0x6A);   // 偏亮的暖绿,向目标加亮
            break;
        case LED_BASE_IDLE: {
            float br = 0.15f + 0.10f * (0.5f + 0.5f * sinf(tick * 0.06f));  // 极慢极低呼吸
            fill((uint8_t)(0xFF * br), (uint8_t)(0x9E * br), (uint8_t)(0x3F * br));
            break;
        }
        case LED_BASE_AMBIENT:
        default: {
            float br = 0.55f + 0.10f * (0.5f + 0.5f * sinf(tick * 0.12f));  // 暖色轻呼吸
            fill((uint8_t)(0xFF * br), (uint8_t)(0x9E * br), (uint8_t)(0x3F * br));
            break;
        }
    }
}

static bool render_fx(int frame)
{
    switch (s_fx) {
        case LED_FX_BUMP: {
            // 一下白→暖快速衰减
            float k = 1.0f - (float)frame / BUMP_FRAMES;
            fill((uint8_t)(255 * k), (uint8_t)(200 * k), (uint8_t)(120 * k));
            return frame < BUMP_FRAMES;
        }
        case LED_FX_COLLECT: {
            // 金色一圈扫过
            int head = frame % LEDSTRIP_COUNT;
            fill(0x20, 0x18, 0x00);
            put(head, 0xFF, 0xD2, 0x3F);
            put((head + 1) % LEDSTRIP_COUNT, 0x80, 0x68, 0x10);
            return frame < COLLECT_FRAMES;
        }
        case LED_FX_WIN: {
            // 彩虹转圈
            for (int i = 0; i < LEDSTRIP_COUNT; i++) {
                int h = (i * 36 + frame * 9) % 360;
                uint8_t r, g, b;
                hsv(h, &r, &g, &b);
                put(i, r, g, b);
            }
            return frame < WIN_FRAMES;
        }
        case LED_FX_SWEEP_L2R: {
            // COLLECT 同款遍历序(索引 0..9)正向单程扫过(不绕圈)
            int head = frame * LEDSTRIP_COUNT / SWEEP_FRAMES;
            fill(0x20, 0x18, 0x00);
            if (head < LEDSTRIP_COUNT) put(head, 0xFF, 0xD2, 0x3F);
            if (head - 1 >= 0 && head - 1 < LEDSTRIP_COUNT) put(head - 1, 0x80, 0x68, 0x10);
            return frame < SWEEP_FRAMES;
        }
        case LED_FX_SWEEP_R2L: {
            int head = LEDSTRIP_COUNT - 1 - frame * LEDSTRIP_COUNT / SWEEP_FRAMES;
            fill(0x20, 0x18, 0x00);
            if (head >= 0 && head < LEDSTRIP_COUNT) put(head, 0xFF, 0xD2, 0x3F);
            if (head + 1 >= 0 && head + 1 < LEDSTRIP_COUNT) put(head + 1, 0x80, 0x68, 0x10);
            return frame < SWEEP_FRAMES;
        }
        case LED_FX_GATHER: {
            // 两端(索引 0/9)→ 中间(4/5)对称聚拢
            int steps = LEDSTRIP_COUNT / 2;
            int step  = frame * steps / GATHER_FRAMES;
            fill(0x20, 0x18, 0x00);
            if (step < steps) {
                put(step, 0xFF, 0xD2, 0x3F);
                put(LEDSTRIP_COUNT - 1 - step, 0xFF, 0xD2, 0x3F);
            }
            if (step - 1 >= 0) {
                put(step - 1, 0x80, 0x68, 0x10);
                put(LEDSTRIP_COUNT - step, 0x80, 0x68, 0x10);
            }
            return frame < GATHER_FRAMES;
        }
        case LED_FX_SPREAD: {
            // 中间(4/5)→ 两端(0/9)对称散开
            int steps = LEDSTRIP_COUNT / 2;
            int step  = frame * steps / SPREAD_FRAMES;
            int a = steps - 1 - step;
            fill(0x20, 0x18, 0x00);
            if (a >= 0 && a < steps) {
                put(a, 0xFF, 0xD2, 0x3F);
                put(LEDSTRIP_COUNT - 1 - a, 0xFF, 0xD2, 0x3F);
            }
            int b = a + 1;
            if (b >= 0 && b < steps) {
                put(b, 0x80, 0x68, 0x10);
                put(LEDSTRIP_COUNT - 1 - b, 0x80, 0x68, 0x10);
            }
            return frame < SPREAD_FRAMES;
        }
        case LED_FX_FLASH: {
            // 整条暖白柔亮一下:三角包络起落,单次不闪烁(光敏安全)
            float k = 1.0f - fabsf((float)frame / FLASH_FRAMES * 2.0f - 1.0f);
            fill((uint8_t)(0xFF * k), (uint8_t)(0xE8 * k), (uint8_t)(0xC0 * k));
            return frame < FLASH_FRAMES;
        }
        default:
            return false;
    }
}

static void led_task(void *arg)
{
    int tick = 0;
    for (;;) {
        if (s_fx >= 0) {
            bool cont = render_fx(s_fx_frame);
            s_fx_frame++;
            if (!cont) s_fx = -1;     // 瞬态结束,回基础模式
        } else {
            render_base(tick);
        }
        led_strip_refresh(s_strip);
        tick++;
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}

esp_err_t ledstrip_fx_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LEDSTRIP_GPIO,
        .max_leds = LEDSTRIP_COUNT,
        .led_model = LED_MODEL_SK6812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },   // 经典 ESP32 RMT 无 DMA;10 颗够用
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device 失败: %s", esp_err_to_name(err));
        return err;
    }
    led_strip_clear(s_strip);

    if (xTaskCreate(led_task, "ledfx", 3072, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "灯带就绪：%d× SK6812 @ G%d(动画任务 40fps)", LEDSTRIP_COUNT, LEDSTRIP_GPIO);
    return ESP_OK;
}

void ledstrip_fx_set_max_brightness(uint8_t max) { s_max_bright = max; }

void ledstrip_fx_set_base(led_base_t base) { s_base = base; }

void ledstrip_fx_trigger(led_fx_t fx)
{
    s_fx_frame = 0;
    s_fx = fx;
}
