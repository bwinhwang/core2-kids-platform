#include "feedback.h"
#include "critters.h"
#include "scene.h"
#include "tuning.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "audio_fx.h"
#include "haptics.h"
#include "ledstrip_fx.h"

static const char *TAG = "feedback";

// (仿 tilt_maze feedback.c 的 ev_t + msg_t + 队列 + 后台任务形状)
typedef enum { EV_BUMP, EV_BOUNCE, EV_COLLECT, EV_PARTY, EV_HELLO, EV_SHAKE } ev_t;

typedef struct {
    ev_t ev;
    int  idx;    // BUMP/BOUNCE:第几只
    int  kind;   // COLLECT:动物种类(定叽/嘎音色)
    int  total;  // COLLECT:已归家总数 1..ANIMAL_COUNT(定五声音阶第几音)
} msg_t;

static QueueHandle_t s_queue;
static TickType_t    s_last_snd_tick;   // 撞墙 boing 的全群共用节流时戳

// 五声音阶 C 宫 1..10(SPEC §5.3:第 n 音 = 已归家总数,进度天然可听)
static const uint16_t PENTA_HZ[ANIMAL_COUNT] = {
    262, 294, 330, 392, 440, 523, 587, 659, 784, 880,
};

static void dispatch_bump(int idx)
{
    critters_squash(idx);   // 屏幕通道:挤压脉冲,必做,不节流(每只自己的视觉反馈)

    // 音频通道:极轻 boing,10 只同时蹭栅栏时全群共用一个节流窗口,防机枪音
    // (SPEC §6"极轻 boing(节流)";按时间戳而非帧计数节流,不依赖消息到达节奏)。
    TickType_t now = xTaskGetTickCount();
    if (now - s_last_snd_tick >= pdMS_TO_TICKS(WALL_BUMP_SND_COOLDOWN_MS)) {
        audio_fx_play(SND_BUMP_LIGHT);
        s_last_snd_tick = now;
    }
    // 触觉 / 灯带:SPEC §6 本事件均为"—"(群体撞墙高频,不震/不亮),故意不发。
}

static void dispatch_bounce(int idx)
{
    critters_shake_head(idx);   // 屏幕:左右各摆一下(§5.2)

    // 轻"啵":中性中音单音(SPEC §6"中性,不低沉"—— SND_BUMP_LIGHT 是低"啵",不合适)
    audio_fx_play_notes((audio_note_t[]){ { 660, 45, 50 } }, 1);
    haptics_play(HAPTIC_BUMP_LIGHT);
    // 灯带:"—"(§6),不发。
}

static void dispatch_collect(int kind, int total)
{
    // 叽(小鸡,高短双音)/ 嘎(小鸭,低降双音)+ 短顿 + 五声音阶第 total 音。
    // 单笔 play_notes 保证节奏连贯;总时长 ~290ms < audio_fx 400ms 合成上限。
    audio_note_t seq[4];
    if (kind == 0) {   // ANIMAL_CHICK:叽
        seq[0] = (audio_note_t){ 1480, 40, 60 };
        seq[1] = (audio_note_t){ 1760, 45, 55 };
    } else {           // ANIMAL_DUCK:嘎
        seq[0] = (audio_note_t){ 392, 55, 65 };
        seq[1] = (audio_note_t){ 330, 60, 60 };
    }
    seq[2] = (audio_note_t){ 0, 25, 0 };
    int n = (total < 1) ? 1 : (total > ANIMAL_COUNT ? ANIMAL_COUNT : total);
    seq[3] = (audio_note_t){ PENTA_HZ[n - 1], 160, 70 };
    audio_fx_play_notes(seq, 4);

    haptics_play(HAPTIC_COLLECT);
    ledstrip_fx_trigger(LED_FX_COLLECT);
    if (total >= HOME_STRETCH_COUNT) {
        ledstrip_fx_set_base(LED_BASE_NEAR);   // 冲刺感:基色加亮一档(§5.3)
    }
}

static void dispatch_party(void)
{
    audio_fx_play(SND_WIN);
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
    scene_party_bounce();   // 两家弹跳 + 限量彩纸(scene 内部自己拿 LVGL 锁,跨任务安全)
    scene_confetti();
    ledstrip_fx_set_base(LED_BASE_AMBIENT);   // 彩虹放完回常态,顺带清掉 ≥8 的冲刺加亮
}

static void dispatch_hello(void)
{
    // 醒来合唱(SPEC §6:叽×2 嘎×1):高高低三声连播;小跳视觉由 game_state 直接起
    audio_fx_play_notes((audio_note_t[]){
        { 1480, 45, 60 }, { 1700, 45, 55 }, { 0, 25, 0 }, { 392, 90, 65 },
    }, 4);
    haptics_play(HAPTIC_HELLO);
    ledstrip_fx_set_base(LED_BASE_AMBIENT);   // AMBIENT 暖亮(开机 board_init 已设,这里兜底)
}

static void dispatch_shake(void)
{
    // 摇一摇彩蛋(SPEC §6):叽嘎混声合唱 + 中震 + 灯带彩虹一闪;不改进度
    audio_fx_play_notes((audio_note_t[]){
        { 1600, 45, 60 }, { 420, 60, 60 }, { 1450, 45, 55 }, { 370, 70, 60 },
    }, 4);
    haptics_play(HAPTIC_BUMP_MED);
    ledstrip_fx_trigger(LED_FX_WIN);
}

static void feedback_task(void *arg)
{
    msg_t m;
    for (;;) {
        if (xQueueReceive(s_queue, &m, portMAX_DELAY) != pdTRUE) continue;
        switch (m.ev) {
            case EV_BUMP:    dispatch_bump(m.idx);              break;
            case EV_BOUNCE:  dispatch_bounce(m.idx);            break;
            case EV_COLLECT: dispatch_collect(m.kind, m.total); break;
            case EV_PARTY:   dispatch_party();                  break;
            case EV_HELLO:   dispatch_hello();                  break;
            case EV_SHAKE:   dispatch_shake();                  break;
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
    s_last_snd_tick = 0;
    ESP_LOGI(TAG, "反馈编排器就绪(P2:bump/bounce/collect/party 四类事件)");
    return ESP_OK;
}

static void emit(const msg_t *m)
{
    if (s_queue) xQueueSend(s_queue, m, 0);   // 满了丢弃,绝不阻塞 game_task
}

void feedback_emit_bump(int idx)   { emit(&(msg_t){ .ev = EV_BUMP,   .idx = idx }); }
void feedback_emit_bounce(int idx) { emit(&(msg_t){ .ev = EV_BOUNCE, .idx = idx }); }
void feedback_emit_collect(int kind, int total)
                                   { emit(&(msg_t){ .ev = EV_COLLECT, .kind = kind, .total = total }); }
void feedback_emit_party(void)     { emit(&(msg_t){ .ev = EV_PARTY }); }
void feedback_emit_hello(void)     { emit(&(msg_t){ .ev = EV_HELLO }); }
void feedback_emit_shake(void)     { emit(&(msg_t){ .ev = EV_SHAKE }); }
