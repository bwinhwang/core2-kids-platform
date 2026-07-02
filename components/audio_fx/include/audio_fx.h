// audio_fx —— 幼儿应用音效引擎(BSP 音频 NS4168 / esp_codec_dev)
//
// 程序化合成(正弦+包络),不依赖外部 PCM 资源;统一 16kHz/mono/16bit。
// 防爆音纪律(实机验证,勿破坏):
//   · codec 整局保持 open,不要每个音效 close+open / 反复 toggle SPK_EN(会咔哒);
//   · 每段首尾 6ms 淡入淡出;
//   · 只用一种采样率,永不 reopen。
// 内置一套跨应用通用的"幼儿反馈音效词汇表"(SND_*),与 haptics / ledstrip_fx 的
// 词汇一一对应(hello/bump/near/collect/win);新应用也可用 audio_fx_play_notes()
// 播自定义音序,无需改本组件。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 内置音效(通用反馈词汇,幼儿应用直接复用)
typedef enum {
    SND_HELLO = 0,    // 开机问候(C5→E5)
    SND_BUMP_LIGHT,   // 碰撞-轻(低"啵")
    SND_BUMP_MED,     // 碰撞-中
    SND_BUMP_HARD,    // 碰撞-重
    SND_NEAR,         // 接近目标(上扬叮铃)
    SND_COLLECT,      // 收集(清脆叮)
    SND_WIN,          // 达成/过关(上行琶音)
    SND_MAX,
} sound_id_t;

// 自定义音序的一个音符:freq_hz=0 表示静音停顿;amp 0~100。
// 整段合成上限 ~400ms,超出截断(保护合成缓冲,也符合"短而不腻"原则)。
typedef struct {
    uint16_t freq_hz;
    uint16_t ms;
    uint8_t  amp;
} audio_note_t;

#define AUDIO_FX_MAX_NOTES 8

/** @brief 初始化:打开喇叭 codec(整局保持 open),起音频任务 + 队列。 */
esp_err_t audio_fx_init(void);

/** @brief 非阻塞播放一个内置音效(投队列;满了丢弃,不阻塞调用方)。 */
void audio_fx_play(sound_id_t id);

/** @brief 非阻塞播放自定义音序(≤ AUDIO_FX_MAX_NOTES 个音符,总时长超 ~400ms 截断)。 */
void audio_fx_play_notes(const audio_note_t *notes, int n);

/** @brief 设音量 0~100。幼儿应用请在应用层限上限(声级计实测 ≲75dBA @25cm)。 */
void audio_fx_set_volume(uint8_t vol);

#ifdef __cplusplus
}
#endif
