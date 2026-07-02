#include "feedback.h"
#include "render.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "maze_audio.h"
#include "haptics.h"
#include "ledstrip_fx.h"

static const char *TAG = "feedback";

typedef enum { EV_HELLO, EV_BUMP, EV_NEAR, EV_COLLECT, EV_WIN } ev_t;

typedef struct {
    ev_t  ev;
    float speed;
    float x, y;
    int   level;
} msg_t;

static QueueHandle_t s_queue;

static void dispatch_bump(float speed, float x, float y)
{
    // 撞击强度分级(§6.1),映射震动/音效力度
    if (speed < 90) {
        haptics_play(HAPTIC_BUMP_LIGHT);
        maze_audio_play(SND_BUMP_LIGHT);
    } else if (speed < 160) {
        haptics_play(HAPTIC_BUMP_MED);
        maze_audio_play(SND_BUMP_MED);
    } else {
        haptics_play(HAPTIC_BUMP_HARD);
        maze_audio_play(SND_BUMP_HARD);
    }
    ledstrip_fx_trigger(LED_FX_BUMP);
    render_ball_squash();
    render_wall_flash(x, y);
}

static void feedback_task(void *arg)
{
    msg_t m;
    for (;;) {
        if (xQueueReceive(s_queue, &m, portMAX_DELAY) != pdTRUE) continue;
        switch (m.ev) {
            case EV_HELLO:
                maze_audio_play(SND_HELLO);
                haptics_play(HAPTIC_HELLO);
                ledstrip_fx_set_base(LED_BASE_AMBIENT);
                break;
            case EV_BUMP:
                dispatch_bump(m.speed, m.x, m.y);
                break;
            case EV_NEAR:
                if (m.level > 0) {
                    maze_audio_play(SND_NEAR);
                    ledstrip_fx_set_base(LED_BASE_NEAR);
                } else {
                    ledstrip_fx_set_base(LED_BASE_AMBIENT);
                }
                break;
            case EV_COLLECT:
                maze_audio_play(SND_COLLECT);
                haptics_play(HAPTIC_COLLECT);
                ledstrip_fx_trigger(LED_FX_COLLECT);
                break;
            case EV_WIN:
                maze_audio_play(SND_WIN);
                haptics_play(HAPTIC_WIN);
                ledstrip_fx_trigger(LED_FX_WIN);
                render_win_celebrate();
                ledstrip_fx_set_base(LED_BASE_AMBIENT);   // 庆祝后回常态
                break;
        }
    }
}

esp_err_t feedback_init(void)
{
    s_queue = xQueueCreate(16, sizeof(msg_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(feedback_task, "feedback", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "反馈编排器就绪(四通道)");
    return ESP_OK;
}

static void emit(const msg_t *m)
{
    if (s_queue) xQueueSend(s_queue, m, 0);   // 满了丢弃,绝不阻塞 game_task
}

void feedback_emit_hello(void)                    { emit(&(msg_t){ .ev = EV_HELLO }); }
void feedback_emit_bump(float s, float x, float y){ emit(&(msg_t){ .ev = EV_BUMP, .speed = s, .x = x, .y = y }); }
void feedback_emit_near(int level)                { emit(&(msg_t){ .ev = EV_NEAR, .level = level }); }
void feedback_emit_collect(float x, float y)      { emit(&(msg_t){ .ev = EV_COLLECT, .x = x, .y = y }); }
void feedback_emit_win(void)                      { emit(&(msg_t){ .ev = EV_WIN }); }
