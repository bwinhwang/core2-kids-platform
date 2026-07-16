// slingshot_feed —— 反馈编排器实现(仿 busy_bus/tilt_maze feedback.c 的队列+后台任务形状)。
#include "feedback.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "audio_fx.h"
#include "haptics.h"
#include "ledstrip_fx.h"

#include "sling_link.h"

#include "tuning.h"

static const char *TAG = "feedback";

typedef enum { EV_HELLO, EV_FIRE, EV_EAT, EV_MISS, EV_GROW, EV_PARTY, EV_LOCK, EV_CALL } ev_t;

typedef struct {
    ev_t ev;
    int  n;         // EV_EAT:本只已喂第几口(五声音阶第 n 音,SPEC §5.4)
    int  species;   // EV_EAT/EV_GROW:物种(改进 C:各自音色,VOICE[])
} msg_t;

static QueueHandle_t s_queue;

// 五声音阶(C5 D5 E5 F5 G5),feed 进度可听(SPEC §5.4)
static const uint16_t SCALE[5] = { 523, 587, 659, 698, 784 };

// 每种动物的"嗓音"基频(改进 C:熊低沉 / 鸡尖 / 蛙中低 / 兔 / 猫),吃/长大的引子音随物种变
static const uint16_t VOICE[ANIMAL_SPECIES] = { 330, 660, 392, 523, 494 };

static void dispatch(const msg_t *m)
{
    switch (m->ev) {
    case EV_HELLO:
        audio_fx_play(SND_HELLO);
        haptics_play(HAPTIC_HELLO);
        ledstrip_fx_set_base(LED_BASE_AMBIENT);
        break;

    case EV_FIRE:
        audio_fx_play_notes((audio_note_t[]){ { 720, 30, 55 }, { 340, 55, 45 } }, 2);   // twang
        haptics_play(HAPTIC_BUMP_MED);
        sling_link_joy_rgb(255, 255, 255);
        break;

    case EV_EAT: {
        int idx = m->n - 1; if (idx < 0) idx = 0; if (idx > 4) idx = 4;
        int sp = m->species; if (sp < 0 || sp >= ANIMAL_SPECIES) sp = 0;
        audio_fx_play_notes((audio_note_t[]){ { VOICE[sp], 35, 50 }, { SCALE[idx], 90, 55 } }, 2);
        haptics_play(HAPTIC_COLLECT);
        ledstrip_fx_trigger(LED_FX_COLLECT);
        sling_link_joy_rgb(70, 230, 90);
        break;
    }

    case EV_MISS:
        audio_fx_play_notes((audio_note_t[]){ { 392, 50, 40 }, { 440, 60, 40 } }, 2);   // 中性上扬,不低沉
        haptics_play(HAPTIC_BUMP_LIGHT);
        break;

    case EV_GROW: {
        int sp = m->species; if (sp < 0 || sp >= ANIMAL_SPECIES) sp = 0;
        audio_fx_play_notes((audio_note_t[]){ { VOICE[sp], 55, 45 }, { 659, 60, 50 }, { 880, 95, 55 } }, 3);
        haptics_play(HAPTIC_COLLECT);
        ledstrip_fx_trigger(LED_FX_SWEEP_L2R);
        break;
    }

    case EV_PARTY:
        audio_fx_play(SND_WIN);
        haptics_play(HAPTIC_WIN);
        ledstrip_fx_trigger(LED_FX_WIN);
        break;

    case EV_LOCK:   // 改进 A:瞄准对准嘴,轻快两声"来嘛~"(自教强化,音量压低防腻;无震动)
        audio_fx_play_notes((audio_note_t[]){ { 784, 26, 30 }, { 988, 34, 30 } }, 2);
        break;

    case EV_CALL:   // 改进 A:动物久等"还要~"(温柔下行 + 轻震一下)
        audio_fx_play_notes((audio_note_t[]){ { 587, 70, 40 }, { 494, 110, 40 } }, 2);
        haptics_play(HAPTIC_HELLO);
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
    if (xTaskCreate(feedback_task, "fb_sling", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "反馈编排器就绪(四通道)");
    return ESP_OK;
}

static void emit(const msg_t *m)
{
    if (s_queue) xQueueSend(s_queue, m, 0);   // 满了丢弃,绝不阻塞 game_task
}

void feedback_emit_hello(void)             { emit(&(msg_t){ .ev = EV_HELLO }); }
void feedback_emit_fire(void)              { emit(&(msg_t){ .ev = EV_FIRE }); }
void feedback_emit_eat(int n, int species) { emit(&(msg_t){ .ev = EV_EAT, .n = n, .species = species }); }
void feedback_emit_miss(void)              { emit(&(msg_t){ .ev = EV_MISS }); }
void feedback_emit_grow(int species)       { emit(&(msg_t){ .ev = EV_GROW, .species = species }); }
void feedback_emit_party(void)             { emit(&(msg_t){ .ev = EV_PARTY }); }
void feedback_emit_lock(void)              { emit(&(msg_t){ .ev = EV_LOCK }); }
void feedback_emit_call(void)              { emit(&(msg_t){ .ev = EV_CALL }); }
