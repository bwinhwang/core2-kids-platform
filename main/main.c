// main.c —— Core2 + Bottom2 幼儿应用模板 · 默认 demo
//
// 展示通用交互回路:摇(IMU)→ engine → 共享状态 → 多模态输出(灯/声/屏),
// 全部经 kids_safety 封顶。这是【可替换的示例】:换应用时改 engine 与各输出 task。
// 初始化顺序由 bsp_init() 统一负责(AXP192 最先,否则屏黑/没声,见 Core2 §2)。
#include "bsp.h"
#include "app_nvs.h"
#include "i2c_scan.h"
#include "shared_state.h"
#include "app_state.h"
#include "engine.h"
#include "app_tuning.h"
#include "kids_safety.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include <math.h>

static const char *TAG = "app";

static inline uint32_t ms_now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 消费者检测“新 peak”:比对 last_peak_ts 是否变化
typedef struct { uint32_t seen; bool init; } peak_watch_t;
static bool new_peak(peak_watch_t *w, uint32_t ts)
{
    if (!w->init) { w->seen = ts; w->init = true; return false; }
    if (ts != w->seen) { w->seen = ts; return true; }
    return false;
}

// ───────────────────────── sensor_task:读 IMU → engine → 共享状态 ───────────
static void sensor_task(void *arg)
{
    (void)arg;
    shake_engine_t eng;
    shake_engine_init(&eng);
    uint32_t pub_peak_ts = 0;
    float    pub_peak_mag = 0.0f;

    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000 / IMU_ODR_HZ);
    while (1) {
        float a[3];
        if (bsp_imu_read_accel(a) == ESP_OK) {
            uint32_t now = ms_now();
            shake_result_t r = shake_engine_update(&eng, a, now);
            if (r.peak) { pub_peak_ts = now; pub_peak_mag = r.peak_mag; }
            shared_state_set_shake(r.intensity, pub_peak_ts, pub_peak_mag);
        }
        vTaskDelayUntil(&last, period);
    }
}

#if CONFIG_BSP_ENABLE_LEDS
// ───────────────────────── led_task:强度→整条亮度/颜色,peak 闪白,idle 呼吸 ─
static void led_task(void *arg)
{
    (void)arg;
    peak_watch_t pw = {0};
    uint32_t flash_until = 0;
    while (1) {
        shared_state_t st;
        shared_state_get(&st);
        float i = clampf(st.intensity, 0.0f, 1.0f);
        if (new_peak(&pw, st.last_peak_ts)) flash_until = ms_now() + 120;

        float t = esp_timer_get_time() * 1e-6f;
        float breath = (sinf(2.0f * (float)M_PI * t / DEMO_IDLE_BREATH_S) * 0.5f + 0.5f) * (1.0f - i);
        float dim = (st.state == APP_DIM) ? 0.3f : 1.0f;
        bool flash = ms_now() < flash_until;

        for (int k = 0; k < BSP_LED_COUNT; k++) {
            if (flash) {
                bsp_leds_set(k, kids_bright(1.0f), kids_bright(1.0f), kids_bright(1.0f));
                continue;
            }
            float lvl = (i + 0.10f * breath) * dim;          // 活跃随强度,静止微呼吸
            uint8_t r = kids_bright(lvl * i * 0.8f);          // 越强越偏白
            uint8_t g = kids_bright(lvl * (0.3f + 0.5f * i));
            uint8_t b = kids_bright(lvl);                     // 蓝为主
            bsp_leds_set(k, r, g, b);
        }
        bsp_leds_refresh();
        vTaskDelay(pdMS_TO_TICKS(1000 / DEMO_LED_FPS));
    }
}
#endif

#if CONFIG_BSP_ENABLE_AUDIO
// ───────────────────────── audio_task:摇一下(peak)响一声 ───────────────────
static void audio_task(void *arg)
{
    (void)arg;
    peak_watch_t pw = {0};
    while (1) {
        shared_state_t st;
        shared_state_get(&st);
        if (new_peak(&pw, st.last_peak_ts)) {
            bsp_audio_play_tone(DEMO_TONE_HZ, 60);   // 温和短音(BSP 内已限幅)
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
#endif

#if CONFIG_BSP_ENABLE_DISPLAY
// ───────────────────────── display_task:背景色随强度加深 ────────────────────
static void display_task(void *arg)
{
    (void)arg;
    while (1) {
        shared_state_t st;
        shared_state_get(&st);
        float i = clampf(st.intensity, 0.0f, 1.0f);
        bsp_display_fill(bsp_rgb565(0, (int)(30 * i), (int)(20 + 120 * i)));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
#endif

// ───────────────────────── supervisor:状态机 + 电量(在 app_main 里跑)────────
static void run_supervisor(void)
{
    app_state_t state = APP_IDLE;
    shared_state_set_app_state(state);
    uint32_t last_active = ms_now(), last_batt = 0;
    float batt = 0.0f;

    while (1) {
        uint32_t now = ms_now();
        if (now - last_batt >= 1000) {
            last_batt = now;
            batt = bsp_power_batt_voltage();
            shared_state_set_batt(batt);
        }
        shared_state_t st;
        shared_state_get(&st);
        if (st.intensity > TRIG_INTENSITY) last_active = now;

        if (batt > 0.0f && batt < KIDS_LOWBAT_VOLT) {
            state = APP_LOW_BAT;                     // 始终可玩,仅状态提示
        } else if (st.intensity > TRIG_INTENSITY) {
            state = APP_ACTIVE;
        } else if (state == APP_ACTIVE && (now - last_active) > ACTIVE_HOLD_MS) {
            state = APP_IDLE;
        } else if (state == APP_IDLE && (now - last_active) > DIM_AFTER_MS) {
            state = APP_DIM;
        }
        shared_state_set_app_state(state);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(app_nvs_init());
    shared_state_init();
    ESP_ERROR_CHECK(bsp_init());

    // 诊断:打印内部总线上的板载设备(应见 0x34/0x38/0x51/0x68)
    i2c_scan(bsp_i2c_internal());

    // WELCOME:一次很轻的扫光 + 一声提示,让家长知道开好了(kids_safety §渐入)
#if CONFIG_BSP_ENABLE_LEDS
    for (int k = 0; k < BSP_LED_COUNT; k++) {
        bsp_leds_clear();
        bsp_leds_set(k, kids_bright(0.1f), kids_bright(0.5f), kids_bright(0.8f));
        bsp_leds_refresh();
        vTaskDelay(pdMS_TO_TICKS(45));
    }
    bsp_leds_clear();
    bsp_leds_refresh();
#endif
#if CONFIG_BSP_ENABLE_AUDIO
    bsp_audio_play_tone(DEMO_TONE_HZ, 150);
#endif

    // 起任务
    xTaskCreatePinnedToCore(sensor_task, "sensor", TASK_STACK_DEFAULT, NULL,
                            TASK_PRIO_SENSOR, NULL, CORE_RT);
#if CONFIG_BSP_ENABLE_AUDIO
    xTaskCreatePinnedToCore(audio_task, "audio", TASK_STACK_DEFAULT, NULL,
                            TASK_PRIO_AUDIO, NULL, CORE_RT);
#endif
#if CONFIG_BSP_ENABLE_LEDS
    xTaskCreatePinnedToCore(led_task, "led", TASK_STACK_DEFAULT, NULL,
                            TASK_PRIO_LED, NULL, CORE_UI);
#endif
#if CONFIG_BSP_ENABLE_DISPLAY
    xTaskCreatePinnedToCore(display_task, "display", TASK_STACK_DEFAULT, NULL,
                            TASK_PRIO_DISPLAY, NULL, CORE_UI);
#endif

    ESP_LOGI(TAG, "进入 IDLE,开摇即玩");
    run_supervisor();   // 不返回
}
