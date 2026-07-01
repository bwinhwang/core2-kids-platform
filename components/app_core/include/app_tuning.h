// app_tuning.h —— 应用【行为】参数集中地(与硬件引脚分离)
//
// 引脚/地址等硬件事实在 bsp_core2/platform_pins.h;安全红线在 kids_safety.h。
// 这里只放“挙動”:任务规格、engine 曲线、状态机时序、demo 输出参数。
// 上板对真人幼儿试玩后在此微调,不要把魔法数字散到各 .c。
#pragma once

// ───────────────────────── 任务规格集中表 ──────────────────────────────────
// 核绑定:延迟最敏感的 audio/sensor 放 CORE_RT,UI(灯/屏)放 CORE_UI
#define CORE_RT             1
#define CORE_UI             0
#define TASK_PRIO_SENSOR    6
#define TASK_PRIO_AUDIO     6
#define TASK_PRIO_LED       4
#define TASK_PRIO_DISPLAY   3
#define TASK_STACK_DEFAULT  4096

// ───────────────────────── 晃动 engine(默认) ──────────────────────────────
#define IMU_ODR_HZ          200        // sensor_task 轮询率
#define GRAVITY_EMA_A       0.02f      // 重力 EMA;越小越稳(方向无关关键)
#define ENV_RELEASE         0.92f      // 包络慢放 @200Hz;越大输出越“黏”
#define ENERGY_FLOOR_G      0.05f      // 死区:低于此视作静止
#define ENERGY_MAX_G        2.0f       // 饱和:高于此封顶
#define INTENSITY_GAMMA     0.6f       // 低端提升,轻摇也响
#define PEAK_THRESH_G       0.30f      // 单次甩动判定门限
#define PEAK_REFRACT_MS     80         // 两次 peak 最小间隔

// ───────────────────────── 状态机时序 ──────────────────────────────────────
#define TRIG_INTENSITY      0.08f      // IDLE→ACTIVE 触发门限
#define ACTIVE_HOLD_MS      1500       // 低于门限持续多久回 IDLE
#define DIM_AFTER_MS        30000      // 静止多久进 DIM 省电

// ───────────────────────── demo 输出(灯 + 声) ─────────────────────────────
#define DEMO_LED_FPS        50
#define DEMO_IDLE_BREATH_S  4.0f       // IDLE 呼吸周期(秒)
#define DEMO_AUDIO_FRAMES   256        // 每次合成/写的帧数
#define DEMO_TONE_HZ        660        // peak 提示音频率
