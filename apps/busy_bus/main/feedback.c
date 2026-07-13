// busy_bus —— 反馈编排器实现(仿 tilt_maze/feedback.c 的队列+后台任务形状)。
#include "feedback.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "audio_fx.h"
#include "haptics.h"
#include "ledstrip_fx.h"

#include "bus_link.h"

static const char *TAG = "feedback";

typedef enum { EV_HELLO, EV_BUMP, EV_PICKUP, EV_WRONG_DOOR, EV_DELIVER, EV_HONK, EV_PARTY } ev_t;

typedef struct {
    ev_t     ev;
    uint32_t color;
    bool     flag;
} msg_t;

static QueueHandle_t s_queue;

static void dispatch(const msg_t *m)
{
    switch (m->ev) {
    case EV_HELLO:
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
        ledstrip_fx_set_base(LED_BASE_AMBIENT);
        break;

    case EV_BUMP:
        audio_fx_play(SND_BUMP_LIGHT);
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;

    case EV_PICKUP:
        audio_fx_play_notes((audio_note_t[]){ { 659, 70, 50 }, { 880, 90, 50 } }, 2);
        haptics_play(HAPTIC_COLLECT);
        ledstrip_fx_trigger(LED_FX_COLLECT);
        bus_link_joy_rgb((m->color >> 16) & 0xFF, (m->color >> 8) & 0xFF, m->color & 0xFF);
        break;

    case EV_WRONG_DOOR:
        audio_fx_play_notes((audio_note_t[]){ { 440, 60, 42 }, { 554, 90, 42 } }, 2);
        break;

    case EV_DELIVER:
        audio_fx_play_notes((audio_note_t[]){ { 988, 60, 50 }, { 1319, 60, 50 }, { 1568, 90, 55 } }, 3);
        haptics_play(HAPTIC_COLLECT);
        ledstrip_fx_trigger(LED_FX_SWEEP_L2R);
        break;

    case EV_HONK:
        audio_fx_play_notes((audio_note_t[]){ { 880, 60, 50 }, { 0, 20, 0 }, { 880, 60, 50 } }, 3);
        haptics_play(HAPTIC_BUMP_MED);
        bus_link_joy_rgb(255, 255, 255);
        if (m->flag) {
            audio_fx_play_notes((audio_note_t[]){ { 1568, 60, 35 } }, 1);   // 附近有人:短应答音(不逐个乘客重复)
        }
        break;

    case EV_PARTY:
        audio_fx_play(SND_WIN);
        haptics_play(HAPTIC_WIN);
        ledstrip_fx_trigger(LED_FX_WIN);
        break;
    }
}

static void feedback_task(void *arg)
{
    msg_t m;
    for (;;) {
        if (xQueueReceive(s_queue, &m, portMAX_DELAY) != pdTRUE) continue;
        dispatch(&m);
    }
}

esp_err_t feedback_init(void)
{
    s_queue = xQueueCreate(16, sizeof(msg_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(feedback_task, "fb_bus", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "反馈编排器就绪(四通道)");
    return ESP_OK;
}

static void emit(const msg_t *m)
{
    if (s_queue) xQueueSend(s_queue, m, 0);   // 满了丢弃,绝不阻塞 game_task
}

void feedback_emit_hello(void)               { emit(&(msg_t){ .ev = EV_HELLO }); }
void feedback_emit_bump(void)                { emit(&(msg_t){ .ev = EV_BUMP }); }
void feedback_emit_pickup(uint32_t c)        { emit(&(msg_t){ .ev = EV_PICKUP, .color = c }); }
void feedback_emit_wrong_door(void)          { emit(&(msg_t){ .ev = EV_WRONG_DOOR }); }
void feedback_emit_deliver(void)             { emit(&(msg_t){ .ev = EV_DELIVER }); }
void feedback_emit_honk(bool near)           { emit(&(msg_t){ .ev = EV_HONK, .flag = near }); }
void feedback_emit_party(void)               { emit(&(msg_t){ .ev = EV_PARTY }); }
