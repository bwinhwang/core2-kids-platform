#include "maze_audio.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "bsp/m5stack_core_2.h"
#include "esp_codec_dev.h"

static const char *TAG = "maze_audio";

#define SR          16000
#define MAX_SAMP    6400            // 最长音效 ~400ms
#define FADE_SAMP   (SR * 6 / 1000) // 6ms 淡入淡出,防爆音

// 一个音段:频率(Hz)、时长(ms)、幅度(0~100);freq=0 表示静音停顿
typedef struct { uint16_t freq; uint16_t ms; uint8_t amp; } seg_t;

#define MAX_SEG 5
static const struct {
    uint8_t n;
    seg_t   seg[MAX_SEG];
} k_snd[SND_MAX] = {
    [SND_HELLO]      = { 2, { {523, 170, 60}, {659, 190, 60} } },          // C5→E5
    [SND_BUMP_LIGHT] = { 1, { {180, 45, 35} } },                           // 低"啵"
    [SND_BUMP_MED]   = { 1, { {165, 70, 55} } },
    [SND_BUMP_HARD]  = { 1, { {150, 100, 75} } },
    [SND_NEAR]       = { 2, { {784, 70, 45}, {988, 80, 45} } },            // 上扬叮铃
    [SND_COLLECT]    = { 1, { {1318, 90, 60} } },                          // 清脆叮(E6)
    [SND_WIN]        = { 4, { {523, 95, 60}, {659, 95, 60}, {784, 95, 60}, {1047, 140, 65} } }, // 琶音
};

static esp_codec_dev_handle_t s_spk;
static QueueHandle_t s_queue;
static uint8_t s_vol = 60;
static int16_t s_buf[MAX_SAMP];

// 合成一个音效到 s_buf,返回样本数
static int synth(sound_id_t id)
{
    int total = 0;
    for (int s = 0; s < k_snd[id].n && total < MAX_SAMP; s++) {
        seg_t g = k_snd[id].seg[s];
        int ns = SR * g.ms / 1000;
        if (total + ns > MAX_SAMP) ns = MAX_SAMP - total;
        float amp = g.amp / 100.0f * 12000.0f;   // 留余量防削顶
        for (int i = 0; i < ns; i++) {
            float env = 1.0f;
            if (i < FADE_SAMP)            env = (float)i / FADE_SAMP;
            else if (i > ns - FADE_SAMP)  env = (float)(ns - i) / FADE_SAMP;
            float v = (g.freq == 0) ? 0.0f
                    : sinf(2.0f * (float)M_PI * g.freq * i / SR);
            s_buf[total + i] = (int16_t)(v * env * amp);
        }
        total += ns;
    }
    return total;
}

static void audio_task(void *arg)
{
    sound_id_t id;
    for (;;) {
        if (xQueueReceive(s_queue, &id, portMAX_DELAY) != pdTRUE) continue;
        if (id >= SND_MAX) continue;
        int n = synth(id);
        if (n > 0) {
            esp_codec_dev_write(s_spk, s_buf, n * sizeof(int16_t));
        }
    }
}

esp_err_t maze_audio_init(void)
{
    s_spk = bsp_audio_codec_speaker_init();
    if (!s_spk) {
        ESP_LOGE(TAG, "喇叭 codec 初始化失败");
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_spk, s_vol);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SR,
        .channel = 1,
        .bits_per_sample = 16,
    };
    esp_err_t err = esp_codec_dev_open(s_spk, &fs);   // 整局保持 open(§10)
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "喇叭 open 失败: %s", esp_err_to_name(err));
        return err;
    }

    s_queue = xQueueCreate(8, sizeof(sound_id_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task, "audio", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "音频就绪(16kHz/mono,程序化合成)");
    return ESP_OK;
}

void maze_audio_play(sound_id_t id)
{
    if (!s_queue) return;
    xQueueSend(s_queue, &id, 0);
}

void maze_audio_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    s_vol = vol;
    if (s_spk) esp_codec_dev_set_out_vol(s_spk, vol);
}
