// 躲猫猫昼夜屋 调参集中地 —— 这些数值要 build+flash+插 DLight 实测调。
// 目标:捂住→立刻入夜、松开→立刻天亮(即时因果);任何房间明暗都好用;怎么玩都不会输。
#pragma once

// ── 轮询 / 照度采样节奏 ──────────────────────────────────────────────────
#define POLL_PERIOD_MS      33     // 30Hz 游戏主循环
#define DLIGHT_READ_TICKS   4      // 每 N 帧读一次照度(4×33≈132ms ≥ BH1750 测量周期 ~120ms)
#define UNIT_RETRY_MS       2000   // 单元没插/没电时的重试探测间隔
#define ERR_STREAK_LOST     20     // 连续多少次 I2C 读失败判"拔线"

// ── 昼夜判据(自适应基准:自动适配房间明暗,不必按房间标定绝对 lux)────────────
//   s_ref 追踪"没捂住时的房间亮度":向上跟得快(松手立刻回到房间亮度)、向下跟得极慢
//   (捂住时基准几乎不掉),于是"捂住"= 照度掉到基准的一个比例以下。
#define LUX_ALPHA           0.40f  // 照度低通(去抖;越小越稳越钝)
#define REF_RISE            0.20f  // 基准跟随"变亮"的速度(每读一次逼近 20% 差距)
#define REF_FALL            0.006f // 基准跟随"变暗"的速度(极慢:捂住时基准几乎不下降)
#define REF_MIN_LUX         15.0f  // 基准下限(很暗的房间兜底,避免阈值趋零)
#define COVER_FRAC          0.35f  // 照度 < 基准×此值 → 判"捂住"(入夜)
#define UNCOVER_FRAC        0.60f  // 照度 > 基准×此值 → 判"松开"(天亮);与 COVER 之间是迟滞带
#define MOVE_FRAC           0.15f  // |Δ照度| > 基准×此值 = 手在动 = 有人玩(喂 core2_sleep)

// ── 天亮计数(每 N 次"天亮/揭晓"庆祝一次)────────────────────────────────────
#define WIN_CYCLES          5      // 攒够几次天亮 → 全屏小庆祝
#define WIN_HOLD_MS         2400   // 庆祝迸发持续,之后清零重开
#define BURST_COUNT         8      // 庆祝迸发的小星数量(限量,压帧预算)

// ── 夜景微动(限量小精灵,压 §9.2 帧预算;绝不整屏动)──────────────────────────
#define STAR_COUNT          6      // 眨眼的星星
#define FIREFLY_COUNT       4      // 漂移的萤火虫

// ── 省电(core2_sleep 托管;捂住/松开使照度变化 = 有人玩,会 kick 防误打盹)────────
#define PLAY_BRIGHTNESS     60
#define NAP_BRIGHTNESS      10
#define NAP_AFTER_MS        20000  // 桌面/手持玩法,静止判定比倾斜迷宫(12s)放宽些
#define DEEP_AFTER_MS       60000  // 深度省电会切 M-Bus 5V → DLight 断电,拿起机身才醒
