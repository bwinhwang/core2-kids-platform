#include "feedback.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "audio_fx.h"
#include "haptics.h"
#include "ledstrip_fx.h"
#include "chain_bus.h"

static const char *TAG = "feedback";

typedef enum { EV_STEP, EV_REVEAL, EV_WIN, EV_MISS, EV_MODE_SWITCH } ev_t;

typedef struct {
    ev_t ev;
    feedback_tick_kind_t kind;   // 仅 EV_STEP 用
} msg_t;

static QueueHandle_t s_queue;
static uint8_t       s_chain_id;   // 0 = 未接管

// Chain 节点 RGB 基线/分类色(与 main.c::try_attach() 的问候色同一套暖色基线一致)
#define RGB_BASE_R   60
#define RGB_BASE_G   46
#define RGB_BASE_B   28

static void chain_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_chain_id) {
        return;   // 没接管(拔线/还没探到),静默跳过,不报错(SPEC §1 通用容错形态)
    }
    chain_bus_set_rgb(s_chain_id, 0, r, g, b, 40);
}

static void dispatch_step(feedback_tick_kind_t kind)
{
    switch (kind) {
        case FEEDBACK_TICK_HOUR:
            audio_fx_play_notes((audio_note_t[]){ { 1500, 30, 60 } }, 1);
            haptics_play(HAPTIC_BUMP_LIGHT);
            ledstrip_fx_trigger(LED_FX_BUMP);
            chain_rgb(40, 200, 60);                 // 整点=绿
            break;
        case FEEDBACK_TICK_HALF:
            audio_fx_play_notes((audio_note_t[]){ { 1500, 30, 60 } }, 1);
            haptics_play(HAPTIC_BUMP_LIGHT);
            ledstrip_fx_trigger(LED_FX_BUMP);
            chain_rgb(50, 110, 220);                // 半点=蓝
            break;
        case FEEDBACK_TICK_QUARTER:
            audio_fx_play_notes((audio_note_t[]){ { 1500, 30, 60 } }, 1);
            haptics_play(HAPTIC_BUMP_LIGHT);
            ledstrip_fx_trigger(LED_FX_BUMP);
            chain_rgb(140, 110, 30);                // 一刻=暗黄
            break;
        case FEEDBACK_TICK_PLAIN:
        default:
            audio_fx_play_notes((audio_note_t[]){ { 1100, 12, 28 } }, 1);   // 极轻咔哒
            chain_rgb(RGB_BASE_R, RGB_BASE_G, RGB_BASE_B);   // 回落暖色基线
            break;
    }
}

static void dispatch_reveal(void)
{
    audio_fx_play_notes((audio_note_t[]){ { 1800, 40, 45 } }, 1);   // 轻「叮」
}

static void dispatch_win(void)
{
    audio_fx_play(SND_WIN);            // 上行琶音(audio_fx 内置词汇表,SPEC §7)
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);   // 彩虹迸发

    // Chain RGB 彩虹一扫(单颗 LED,"转圈"落地成时间上的色相扫过,~480ms)
    if (s_chain_id) {
        static const uint8_t hue_rgb[6][3] = {
            { 220, 60, 60 }, { 220, 160, 40 }, { 200, 220, 60 },
            { 60, 200, 90 }, { 60, 140, 220 }, { 160, 80, 220 },
        };
        for (int i = 0; i < 6; i++) {
            chain_rgb(hue_rgb[i][0], hue_rgb[i][1], hue_rgb[i][2]);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
}

static void dispatch_miss(void)
{
    // 两声软下行(不是刺耳错误音,SPEC §5.7):中音下滑到略低音,短促、音量不大
    audio_fx_play_notes((audio_note_t[]){
        { 520, 70, 45 }, { 0, 15, 0 }, { 420, 90, 40 },
    }, 3);
    haptics_play(HAPTIC_BUMP_LIGHT);
    ledstrip_fx_trigger(LED_FX_FLASH);   // 暖色柔亮一闪(≈250ms 起落,无频闪感,近似"暖黄呼吸一下")

    if (s_chain_id) {
        chain_rgb(150, 120, 20);                          // 暗黄一闪
        vTaskDelay(pdMS_TO_TICKS(180));
        chain_rgb(RGB_BASE_R, RGB_BASE_G, RGB_BASE_B);     // 收回暖色基线
    }
}

static void dispatch_mode_switch(void)
{
    audio_fx_play_notes((audio_note_t[]){
        { 900, 40, 50 }, { 1300, 55, 55 },
    }, 2);
    haptics_play(HAPTIC_BUMP_LIGHT);
}

static void feedback_task(void *arg)
{
    msg_t m;
    for (;;) {
        if (xQueueReceive(s_queue, &m, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (m.ev) {
            case EV_STEP:        dispatch_step(m.kind); break;
            case EV_REVEAL:      dispatch_reveal();      break;
            case EV_WIN:         dispatch_win();         break;
            case EV_MISS:        dispatch_miss();        break;
            case EV_MODE_SWITCH: dispatch_mode_switch(); break;
        }
    }
}

esp_err_t feedback_init(void)
{
    s_queue = xQueueCreate(16, sizeof(msg_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(feedback_task, "feedback", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "反馈编排器就绪(转格/揭晓/答对/还没到/模式切换,§7)");
    return ESP_OK;
}

void feedback_set_chain_id(uint8_t id) { s_chain_id = id; }

static void emit(const msg_t *m)
{
    if (s_queue) {
        xQueueSend(s_queue, m, 0);   // 满了丢弃,绝不阻塞 game_task
    }
}

void feedback_emit_step(feedback_tick_kind_t kind)
                                    { emit(&(msg_t){ .ev = EV_STEP, .kind = kind }); }
void feedback_emit_reveal(void)      { emit(&(msg_t){ .ev = EV_REVEAL }); }
void feedback_emit_win(void)         { emit(&(msg_t){ .ev = EV_WIN }); }
void feedback_emit_miss(void)        { emit(&(msg_t){ .ev = EV_MISS }); }
void feedback_emit_mode_switch(void) { emit(&(msg_t){ .ev = EV_MODE_SWITCH }); }
