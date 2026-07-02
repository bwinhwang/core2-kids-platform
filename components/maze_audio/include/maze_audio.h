// 音效 (CLAUDE.md §10)。走 BSP 音频(NS4168/esp_codec_dev)。
// 首版用程序化合成(正弦+包络),不依赖外部 PCM 资源文件。
// 防爆音:整局保持喇叭 open,统一 16kHz/mono/16bit,首尾淡入淡出(§10)。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SND_HELLO = 0,    // 开机问候(C5→E5)
    SND_BUMP_LIGHT,   // 撞墙-轻(低"啵")
    SND_BUMP_MED,     // 撞墙-中
    SND_BUMP_HARD,    // 撞墙-重
    SND_NEAR,         // 接近目标(上扬叮铃)
    SND_COLLECT,      // 收集星(清脆叮)
    SND_WIN,          // 过关(上行琶音)
    SND_MAX,
} sound_id_t;

/** @brief 初始化:打开喇叭 codec(整局保持),起音频任务 + 队列。 */
esp_err_t maze_audio_init(void);

/** @brief 非阻塞播放一个音效(投队列;满了丢弃,不阻塞调用方)。 */
void maze_audio_play(sound_id_t id);

/** @brief 设音量 0~100(留上限防过响,§13)。 */
void maze_audio_set_volume(uint8_t vol);

#ifdef __cplusplus
}
#endif
