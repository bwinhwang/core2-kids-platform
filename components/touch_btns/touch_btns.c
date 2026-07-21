#include "touch_btns.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "app_slot.h"
#include "core2_power.h"
#include "haptics.h"
#include "screenshot.h"

static const char *TAG = "touch_btns";

/* ── FT6336U(内部 I2C 0x38)触点寄存器 ─────────────────────────────
 * 0x02 TD_STATUS(低 4 位=触点数),0x03..0x06 = 点1 XH/XL/YH/YL(各高 4 位有效)。*/
#define FT6336_ADDR       0x38
#define FT6336_REG_STATUS 0x02

/* ── 标定旋钮(唯一需实机核对的地方,见 README)──────────────────────
 * 三个圆圈在屏外(y≥240)。若实机发现圆圈报的 y 落在屏内边缘(如 ~218),把
 * BTN_Y_MIN 下调即可;x 三分界同理。首刷时看串口 "raw touch" 日志按一下三个圆圈就能读出真值。*/
/* 实测标定(2026-07-21,本机):三圆圈中心 x≈109/187/269、y≈271~279(整体比理论右偏 ~28px)。
 * 分界取相邻中心的中点。若换机复现请重新按圆圈读串口 raw 日志回填。 */
#define BTN_Y_MIN     240   /* y ≥ 此值才算屏下键区(圆圈报 y≈271~279,屏内 UI 在 y<240,天然不冲突) */
#define X_SPLIT_AB    148   /* x < 148 = BtnA(左圈中心 ~109) */
#define X_SPLIT_BC    228   /* 148..227 = BtnB(中圈 ~187);x ≥ 228 = BtnC(右圈 ~269) */

/* ── 触发门槛(破坏性键长按防误触,关机门槛最高)──────────────────── */
#define LONG_A_MS     800   /* BtnA 长按门槛(默认=回 launcher) */
#define LONG_B_MS     800   /* BtnB 长按门槛(默认无动作,留给 app 绑) */
#define LONG_C_MS    1500   /* BtnC 长按门槛(默认=关机,最不可逆、最高) */
#define TAP_MIN_MS     40   /* 短按有效下限(去抖) */

#define POLL_MS        30   /* 轮询周期:33Hz,对 800/1500ms 长按分辨足够、开销极小 */
#define CAL_LOG_Y     200   /* y≥此值的触点打一行 raw 日志(节流),供实机标定 */
#define CAL_LOG_GAP_MS 400  /* raw 日志节流间隔,避免久压刷屏 */

/* 内部键号与公开 touch_btn_id_t 数值一致(A/B/C=0/1/2),BTN_NONE 仅内部用。 */
enum { BTN_NONE = -1 };

static const uint16_t s_long_ms[TOUCH_BTN_COUNT] = { LONG_A_MS, LONG_B_MS, LONG_C_MS };

/* 绑定表:[键][手势];cb 非 NULL 覆盖内置默认。app init 时设,轮询任务读——单指针读写、
 * 只在启动阶段写一次,不加锁(平台纪律:不过度加锁)。 */
typedef struct { touch_btn_cb_t cb; void *user; } bind_t;
static bind_t s_bind[TOUCH_BTN_COUNT][TOUCH_BTN_GESTURE_COUNT];

static i2c_master_dev_handle_t s_ft;
static bool s_running;

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* 读一帧触点。有触点返回 true 并填 x/y(屏坐标系,同 LVGL)。*/
static bool ft_read(uint16_t *x, uint16_t *y)
{
    uint8_t reg = FT6336_REG_STATUS, buf[5] = {0};
    if (i2c_master_transmit_receive(s_ft, &reg, 1, buf, sizeof(buf), 100) != ESP_OK) {
        return false;   /* 读失败(总线瞬时忙等)当作无触点,下个周期再来 */
    }
    if ((buf[0] & 0x0F) == 0) return false;   /* TD_STATUS 触点数为 0 */
    *x = ((uint16_t)(buf[1] & 0x0F) << 8) | buf[2];
    *y = ((uint16_t)(buf[3] & 0x0F) << 8) | buf[4];
    return true;
}

static int zone_of(uint16_t x, uint16_t y)
{
    if (y < BTN_Y_MIN) return BTN_NONE;        /* 屏内触点交给 LVGL,不当物理键 */
    if (x < X_SPLIT_AB) return TOUCH_BTN_A;
    if (x < X_SPLIT_BC) return TOUCH_BTN_B;
    return TOUCH_BTN_C;
}

/* ── 三个全局动作 ──────────────────────────────────────────────── */
static void act_home(void)   /* BtnA 长按 */
{
    ESP_LOGW(TAG, "BtnA 长按 → 回 launcher");
    haptics_play(HAPTIC_WIN);
    vTaskDelay(pdMS_TO_TICKS(120));            /* 让确认震动先响 */
    app_slot_return_to_factory();              /* 擦 otadata:下次启动进 factory=launcher */
    esp_restart();                             /* 不返回 */
}

static void act_shutdown(void)  /* BtnC 长按 */
{
    ESP_LOGW(TAG, "BtnC 长按 → 关机");
    haptics_play(HAPTIC_BUMP_HARD);
    vTaskDelay(pdMS_TO_TICKS(150));            /* 让报警震动响完再断电 */
    core2_power_shutdown();                    /* AXP192 0x32 bit7:正常不返回 */
    ESP_LOGE(TAG, "关机写寄存器返回了(I2C 失败?),继续运行");
}

static void act_screenshot(void)  /* BtnB 短按 */
{
    ESP_LOGI(TAG, "BtnB 短按 → 截屏(吐日志串口;主机需 screenshot.py --watch 接)");
    haptics_play(HAPTIC_COLLECT);              /* 本地反馈="已触发";有没有被主机接住无回传通道 */
    screenshot_dump_now();                     /* 阻塞 ~1s 吐 Base64,期间暂不轮询,可接受 */
}

/* 手势派发:app 绑了走 app 回调,否则走内置默认(A长按=回launcher / B短按=截屏 / C长按=关机);
 * 其余槽(A短按 / B长按 / C短按)默认无动作,绑了才有。 */
static void dispatch(int id, touch_btn_gesture_t g)
{
    bind_t b = s_bind[id][g];
    if (b.cb) { b.cb(b.user); return; }
    if (id == TOUCH_BTN_A && g == TOUCH_BTN_LONG)       act_home();
    else if (id == TOUCH_BTN_B && g == TOUCH_BTN_SHORT) act_screenshot();
    else if (id == TOUCH_BTN_C && g == TOUCH_BTN_LONG)  act_shutdown();
}

static void touch_task(void *arg)
{
    (void)arg;
    int     cur = BTN_NONE;   /* 当前按住的键 */
    int64_t t0 = 0;           /* 本次按下起始 ms */
    bool    fired = false;    /* 长按动作已触发(避免久压重复触发) */
    int64_t last_cal_log = 0;

    for (;;) {
        uint16_t x = 0, y = 0;
        bool touched = ft_read(&x, &y);
        int  hit = touched ? zone_of(x, y) : BTN_NONE;
        int64_t t = now_ms();

        /* 标定辅助:键区附近的原始坐标节流打串口(首刷对三个圆圈按一下即可读真值) */
        if (touched && y >= CAL_LOG_Y && t - last_cal_log >= CAL_LOG_GAP_MS) {
            ESP_LOGI(TAG, "raw touch x=%u y=%u → zone %d", x, y, hit);
            last_cal_log = t;
        }

        if (hit != cur) {
            /* —— 离开原键:未达长按门槛就松手 = 短按,在"松手"这一刻结算 —— */
            if (cur != BTN_NONE && !fired && (t - t0) >= TAP_MIN_MS) {
                dispatch(cur, TOUCH_BTN_SHORT);
            }
            /* —— 进入新键:一下轻震确认"按到了" —— */
            cur = hit;
            t0 = t;
            fired = false;
            if (hit != BTN_NONE) haptics_play(HAPTIC_HELLO);
        } else if (cur != BTN_NONE && !fired) {
            /* —— 同键持续按住:达该键长按门槛即触发长按(只触发一次)—— */
            if ((t - t0) >= s_long_ms[cur]) {
                fired = true;
                dispatch(cur, TOUCH_BTN_LONG);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t touch_btns_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (s_running) return ESP_OK;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = FT6336_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_ft);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U(0x38)设备添加失败:%s", esp_err_to_name(err));
        s_ft = NULL;
        return err;
    }

    if (xTaskCreate(touch_task, "touch_btns", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "轮询任务创建失败");
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    ESP_LOGI(TAG, "屏下三键就绪:BtnA长按=回launcher BtnB短按=截屏 BtnC长按=关机(可 touch_btns_bind 覆盖)");
    return ESP_OK;
}

void touch_btns_bind(touch_btn_id_t id, touch_btn_gesture_t g, touch_btn_cb_t cb, void *user)
{
    if ((int)id < 0 || id >= TOUCH_BTN_COUNT || (int)g < 0 || g >= TOUCH_BTN_GESTURE_COUNT) return;
    s_bind[id][g].cb   = cb;
    s_bind[id][g].user = user;
}
