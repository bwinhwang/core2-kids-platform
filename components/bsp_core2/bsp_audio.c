// bsp_audio.c —— NS4168 I2S 扬声器能力(Core2, I2S)
//
// 只提供“播 PCM”能力;沙声/音效合成属应用逻辑。NS4168 实测监听右声道:必须
// Philips/16bit/STEREO,左右写同一份 PCM(mono 左槽完全没声,见 Core2 §3 踩坑)。
#include "bsp.h"
#include "bsp_priv.h"
#include "driver/i2s_std.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include <math.h>
#include <string.h>

static const char *TAG = "bsp_audio";
static i2s_chan_handle_t s_tx;

#define AUDIO_SR_HZ     22050
#define CHUNK_FRAMES    256          // 每次交织写的帧数
#define TONE_VOL        0.35f        // 自检音量(温和;正式音效用应用侧封顶)

esp_err_t bsp_audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "new channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SR_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BSP_PIN_I2S_BCLK,
            .ws   = BSP_PIN_I2S_WS,
            .dout = BSP_PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "init std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");

    // 先喂静音帧,保证拉高 SPK_EN 时总线已有数据(防爆音第一步)
    static int16_t silence[CHUNK_FRAMES * 2];
    memset(silence, 0, sizeof(silence));
    size_t wr;
    for (int k = 0; k < 4; k++) i2s_channel_write(s_tx, silence, sizeof(silence), &wr, 100);

    ESP_LOGI(TAG, "I2S 就绪(%d Hz, L=R, BCLK=%d WS=%d DOUT=%d)",
             AUDIO_SR_HZ, BSP_PIN_I2S_BCLK, BSP_PIN_I2S_WS, BSP_PIN_I2S_DOUT);
    return ESP_OK;
}

esp_err_t bsp_audio_write_mono(const int16_t *pcm_mono, size_t frames)
{
    int16_t st[CHUNK_FRAMES * 2];
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done;
        if (n > CHUNK_FRAMES) n = CHUNK_FRAMES;
        for (size_t i = 0; i < n; i++) {
            st[2 * i] = st[2 * i + 1] = pcm_mono[done + i];   // L=R
        }
        size_t wr;
        ESP_RETURN_ON_ERROR(i2s_channel_write(s_tx, st, n * 2 * sizeof(int16_t), &wr,
                                              portMAX_DELAY), TAG, "write");
        done += n;
    }
    return ESP_OK;
}

esp_err_t bsp_audio_play_tone(int freq_hz, int ms)
{
    int total = AUDIO_SR_HZ * ms / 1000;
    int ramp  = AUDIO_SR_HZ / 200;    // ~5ms 淡入淡出,防爆音
    float phase = 0.0f, dphase = 2.0f * (float)M_PI * freq_hz / AUDIO_SR_HZ;
    int16_t mono[CHUNK_FRAMES];
    int idx = 0;
    while (idx < total) {
        int n = (total - idx > CHUNK_FRAMES) ? CHUNK_FRAMES : (total - idx);
        for (int i = 0; i < n; i++) {
            float env = 1.0f;
            if (idx < ramp)              env = (float)idx / ramp;
            else if (idx > total - ramp) env = (float)(total - idx) / ramp;
            if (env < 0.0f) env = 0.0f;
            mono[i] = (int16_t)(sinf(phase) * TONE_VOL * env * 32767.0f);
            phase += dphase;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
            idx++;
        }
        ESP_RETURN_ON_ERROR(bsp_audio_write_mono(mono, n), TAG, "tone");
    }
    return ESP_OK;
}
