// 躲猫猫昼夜屋(v2「夜里来客」)调参集中地 —— 这些数值要 build+flash+插 DLight 实测调。
// 目标:捂住→立刻入夜(悬念递进)、松开→立刻天亮(访客揭晓);任何房间明暗都好用;怎么玩都不会输。
#pragma once

// ── 轮询 / 照度采样节奏(v1 保留)────────────────────────────────────────
#define POLL_PERIOD_MS      33     // 30Hz 游戏主循环
#define DLIGHT_READ_TICKS   4      // 每 N 帧读一次照度(4×33≈132ms ≥ BH1750 测量周期 ~120ms)
#define UNIT_RETRY_MS       2000   // 单元没插/没电时的重试探测间隔
#define ERR_STREAK_LOST     20     // 连续多少次 I2C 读失败判"拔线"

// ── 昼夜判据(v1 保留;自适应基准:自动适配房间明暗,不必按房间标定绝对 lux)──────
//   s_ref 追踪"没捂住时的房间亮度":向上跟得快(松手立刻回到房间亮度)、向下跟得极慢
//   (捂住时基准几乎不掉),于是"捂住"= 照度掉到基准的一个比例以下。
#define LUX_ALPHA           0.40f  // 照度低通(去抖;越小越稳越钝)
#define REF_RISE            0.20f  // 基准跟随"变亮"的速度(每读一次逼近 20% 差距)
#define REF_FALL            0.006f // 基准跟随"变暗"的速度(极慢:捂住时基准几乎不下降)
#define REF_MIN_LUX         15.0f  // 基准下限(很暗的房间兜底,避免阈值趋零)
#define COVER_FRAC          0.35f  // 照度 < 基准×此值 → 判"捂住"(入夜)
#define UNCOVER_FRAC        0.60f  // 照度 > 基准×此值 → 判"松开"(天亮);与 COVER 之间是迟滞带
#define MOVE_FRAC           0.15f  // |Δ照度| > 基准×此值 = 手在动 = 有人玩(喂 core2_sleep)

// ── 夜景微动 / 庆祝迸发限量(v1 保留,压§12帧预算;绝不整屏动)───────────────
#define STAR_COUNT          6      // 眨眼的星星
#define BURST_COUNT          8      // 迸发小星数量(游行彩纸复用)

// ── 省电(v1 保留;core2_sleep 托管;照度变化=有人玩,会 kick 防误打盹)─────────
#define PLAY_BRIGHTNESS     60
#define NAP_BRIGHTNESS      10
#define NAP_AFTER_MS        20000  // 桌面/手持玩法,静止判定比倾斜迷宫(12s)放宽些
#define DEEP_AFTER_MS       60000  // 深度省电会切 M-Bus 5V → DLight 断电,拿起机身才醒

// ── 访客 ─────────────────────────────────────────────────────────────
#define VISITOR_COUNT        6
#define RARE_PCT             8      // 彩虹鸟替换概率(%,P3;0=关)
#define ENTR_FAST_SCALE      0.6f   // 猛松:出场时长×0.6、弹跳×1.5
#define ENTR_PEEK_SCALE      1.5f   // 慢掀:出场时长×1.5
#define ENTR_PEEK_HOLD_MS    400    // 慢掀:探半头停顿
#define GAZE_MS              2000   // 圆圆看向访客保持时长
#define GAZE_DX              3      // 瞳孔偏移(px)

// ── 夜悬念(P2)───────────────────────────────────────────────────────
#define TEASE_RUSTLE_MS      800    // 入夜 → 草丛动静
#define TEASE_EYES_MS        1500   // 入夜 → 眼睛亮起
#define TEASE_CALL_PERIOD_MS 3000   // 闷叫重复周期
#define TEASE_CALL_AMP_PCT   40     // 闷叫响度(%)
#define TEASE_CALL_FREQ_PCT  75     // 闷叫频率(%,下限 200Hz)
#define DREAM_STEP_S         2      // 梦泡泡长一档间隔(3 档)
#define FIREFLY_START        2      // 入夜萤火虫起始只数
#define FIREFLY_MAX          6      // 累积上限(预建 6 只)
#define FIREFLY_ADD_S        2      // 每 N 秒 +1 只
#define NIGHT_LONG_S         6      // ≥此夜长 = 长夜(醒来有梦)

// ── 松开速度档(P3;单位 = 迟滞带内读数次数,一次 ≈132ms)──────────────────
#define DAWN_FAST_READS      2      // ≤ 此 = 猛松
#define DAWN_SLOW_READS      6      // ≥ 此 = 慢掀

// ── 游行 ─────────────────────────────────────────────────────────────
#define PARADE_LEAD_IN_MS    600    // 第 6 位入册 → 游行开场
#define PARADE_WALK_MS       3000   // 单个访客横穿时长
#define PARADE_STAGGER_MS    250    // 逐个错峰

// ── 黄昏(P4)─────────────────────────────────────────────────────────
#define DUSK_ENABLED         1      // 实机抖动就置 0,不返工其它
#define DUSK_DWELL_READS     3      // 迟滞带驻留读数 ≥ 此 = 黄昏(~400ms)

// ── NVS(P4)──────────────────────────────────────────────────────────
#define PEEKABOO_NVS_NS      "peekaboo"
#define PARADE_STARS_MAX     5      // 游行小星显示上限
