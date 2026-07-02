#include "haptics.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "bsp/m5stack_core_2.h"

static const char *TAG = "haptics";

// 每个模式 = 一串 (震 on_ms, 停 off_ms) 段;off_ms=0 表示该段是收尾停顿。
typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
} haptic_step_t;

#define MAX_STEPS 6

static const struct {
    uint8_t n;
    haptic_step_t steps[MAX_STEPS];
} k_patterns[HAPTIC_PATTERN_MAX] = {
    [HAPTIC_HELLO]      = { 1, { {60, 0} } },
    [HAPTIC_WAKE]       = { 1, { {40, 0} } },
    [HAPTIC_BUMP_LIGHT] = { 1, { {30, 0} } },
    [HAPTIC_BUMP_MED]   = { 1, { {60, 0} } },
    [HAPTIC_BUMP_HARD]  = { 1, { {100, 0} } },
    [HAPTIC_COLLECT]    = { 1, { {25, 0} } },
    [HAPTIC_WIN]        = { 3, { {80, 80}, {80, 80}, {80, 0} } },  // 欢庆三连
};

static QueueHandle_t s_queue;
static volatile bool s_enabled = true;

static void motor(bool on)
{
    bsp_feature_enable(BSP_FEATURE_VIBRATION, on);
}

static void haptics_task(void *arg)
{
    haptic_pattern_t p;
    for (;;) {
        if (xQueueReceive(s_queue, &p, portMAX_DELAY) != pdTRUE) continue;
        if (!s_enabled || p >= HAPTIC_PATTERN_MAX) continue;

        const uint8_t n = k_patterns[p].n;
        for (uint8_t i = 0; i < n; i++) {
            haptic_step_t st = k_patterns[p].steps[i];
            if (st.on_ms) {
                motor(true);
                vTaskDelay(pdMS_TO_TICKS(st.on_ms));
                motor(false);
            }
            if (st.off_ms) {
                vTaskDelay(pdMS_TO_TICKS(st.off_ms));
            }
        }
        motor(false);  // 兜底,保证停
    }
}

esp_err_t haptics_init(void)
{
    motor(false);
    s_queue = xQueueCreate(8, sizeof(haptic_pattern_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(haptics_task, "haptics", 2048, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "震动后台任务就绪");
    return ESP_OK;
}

void haptics_play(haptic_pattern_t pattern)
{
    if (!s_queue || !s_enabled) return;
    xQueueSend(s_queue, &pattern, 0);   // 满了就丢,绝不阻塞调用方
}

void haptics_set_enabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled) motor(false);
}
