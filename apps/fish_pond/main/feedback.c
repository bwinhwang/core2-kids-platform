// feedback —— 事件 → 四通道编排实现(SPEC.md §7)
#include "feedback.h"

#include "audio_fx.h"
#include "haptics.h"
#include "ledstrip_fx.h"

void feedback_bait_splash(void)
{
    audio_fx_play_notes((audio_note_t[]){ { 440, 40, 40 }, { 260, 60, 30 } }, 2);   // 噗通(降调短音)
}

void feedback_fish_notice(void)
{
    audio_fx_play_notes((audio_note_t[]){ { 587, 60, 40 }, { 784, 70, 45 } }, 2);   // "哦?" 上行两音
    ledstrip_fx_set_base(LED_BASE_NEAR);                                          // 向鱼色渐亮
}

void feedback_bite(void)
{
    audio_fx_play(SND_BUMP_HARD);        // 咔嗯!
    haptics_play(HAPTIC_BUMP_HARD);
    ledstrip_fx_trigger(LED_FX_BUMP);    // 鱼色闪一次
}

void feedback_reel_tick(void)
{
    audio_fx_play_notes((audio_note_t[]){ { 900, 18, 30 } }, 1);   // 棘轮声(短促,密度由调用频率体现)
    haptics_play(HAPTIC_BUMP_LIGHT);                               // tick
}

void feedback_doze(void)
{
    audio_fx_play_notes((audio_note_t[]){ { 220, 90, 25 } }, 1);   // 轻鼾一声
}

void feedback_giggle(void)
{
    audio_fx_play_notes((audio_note_t[]){ { 700, 40, 35 }, { 900, 50, 35 } }, 2);   // 轻快咯咯,非失败音色
}

void feedback_surface(void)
{
    audio_fx_play_notes((audio_note_t[]){ { 523, 50, 40 }, { 659, 60, 45 }, { 880, 90, 50 } }, 3);   // 上扬滑音
    haptics_play(HAPTIC_COLLECT);
    ledstrip_fx_trigger(LED_FX_FLASH);   // 白闪一次
}

void feedback_bucket_add(void)
{
    audio_fx_play(SND_COLLECT);          // 噗通+叮(用清脆叮代表进桶达成)
    haptics_play(HAPTIC_BUMP_LIGHT);
    ledstrip_fx_trigger(LED_FX_COLLECT);
}

void feedback_party(void)
{
    audio_fx_play(SND_WIN);
    audio_fx_play_notes((audio_note_t[]){ { 659, 80, 50 }, { 784, 80, 50 }, { 988, 130, 55 } }, 3);  // 上行琶音
    haptics_play(HAPTIC_WIN);
    ledstrip_fx_trigger(LED_FX_WIN);
}
