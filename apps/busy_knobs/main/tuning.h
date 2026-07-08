// 旋钮忙碌台 调参集中地 —— 这些数值要 build+flash+抱真机(+插 8Encoder)实测调。
// 目标:转哪个哪个立刻响应(即时因果)、音高随高度、怎么玩都不会输。
#pragma once

// ── 轮询节奏 ─────────────────────────────────────────────────────────
#define POLL_PERIOD_MS      33     // 30Hz:一次全量轮询 ≈17 个 I2C 小事务 @100kHz ≈12ms,余量足
#define UNIT_RETRY_MS       2000   // 8Encoder 没插/没电时的重试探测间隔

// ── 音柱(8 根,一一对应 8 个旋钮)────────────────────────────────────
#define KNOB_COUNT          8
#define LEVEL_MAX           24     // 每根音柱档位数(0~24;编码器一格=ENC_COUNTS_PER_LEVEL 个计数)
#define ENC_COUNTS_PER_LEVEL 1     // 几个编码器计数算 1 档(实机若一格跳 2 档改成 2)
#define ENC_DIR             (+1)   // 旋转方向系数:实机若"顺时针反而降柱"改成 -1
#define COL_W               34     // 音柱宽(px);8×34 + 7×6 间距 = 314,居中留边 3px
#define COL_GAP             6
#define COL_H_MIN           22     // 档位 0 也留个可见的"苗"(永远有东西可看)
#define COL_H_MAX           190    // 档位拉满的高度(顶到 y=50,给太阳/月亮留头顶)
#define COL_RADIUS          10     // 圆角;柱底多画 COL_RADIUS 伸出屏外,视觉上底是平的

// ── 声音(C 大调五声音阶,怎么乱转都和谐)──────────────────────────────
#define TICK_MS             45     // 转动"叮"的时长
#define TICK_AMP            45     // 转动"叮"的响度(0~100)
#define SING_MS             180    // 按下唱歌的时长
#define SING_AMP            75

// ── 旋钮就地 RGB(单元自带 WS2812,满亮度刺眼)─────────────────────────
#define KNOB_LED_MAX        110    // 就地灯亮度上限(0~255,幼儿压低)
#define KNOB_LED_FLOOR      12     // 档位 0 的底亮(转哪个都先看得见自己的灯)

// ── 全拉满庆祝(唯一"彩蛋",非必经,零失败)────────────────────────────
#define WIN_BOUNCE_MS       220    // 庆祝弹跳单程时长
#define WIN_HOLD_MS         2600   // 庆祝持续
#define SINK_MS             900    // 庆祝后音柱缓缓落回 0 的时长

// ── 图案彩蛋 & 摇一摇(趣味增量)──────────────────────────────────────
#define ARP_MS              42     // "演奏这一排"每个音的时长(8 音 × ≤400ms 上限)
#define ARP_AMP             55     // 图案彩蛋 arp 响度(0~100)
#define SHAKE_THRESH        1.2f   // 帧间三轴加速度变化和 > 此值算"晃了一下"(g;桌面转旋钮远达不到)
#define SHAKE_NEEDED        3      // 带泄漏地攒够几下才算"摇一摇",防单次磕碰/放桌误触
#define SHAKE_COOLDOWN_MS   1500   // 触发后冷却,防一次摇晃连发
#define SHUFFLE_MS          520    // 摇一摇后音柱洗到新队形的动画时长

// ── 省电(core2_sleep 托管;旋钮活动也算"有人玩")───────────────────────
#define PLAY_BRIGHTNESS     60
#define NAP_BRIGHTNESS      10
#define NAP_AFTER_MS        20000  // 桌面玩法,静止判定比倾斜迷宫(12s)放宽些
#define DEEP_AFTER_MS       60000  // 深度省电会切 M-Bus 5V → 8Encoder 断电,拿起机身才醒

// ══ 趣味增量第二批(FUN2_SPEC.md)══════════════════════════════════════

// ── 小脸活化 ──────────────────────────────────────────────────────────
#define BLINK_MIN_S          2      // 眨眼间隔下限(s)
#define BLINK_MAX_S          6
#define BLINK_FRAMES         3      // 合眼持续帧数(~100ms)
#define BLINK_DOUBLE_PCT     20     // 双眨概率(%)
#define GAZE_HOLD_MS         1000   // 看向保持时长
#define GAZE_DX              2      // 瞳孔偏移(px)
#define MOUTH_SMILE_LV       9      // ≥此档 = 微笑
#define MOUTH_OPEN_LV        18     // ≥此档 = 张嘴笑

// ── 小鸟 ─────────────────────────────────────────────────────────────
#define BIRD_VISIT_MIN_S     20     // 自发拜访间隔(s,随机区间)
#define BIRD_VISIT_MAX_S     45
#define BIRD_PERCH_MIN_S     8      // 栖息时长(s,随机区间)
#define BIRD_PERCH_MAX_S     15
#define BIRD_FLY_MS          700    // 飞入/飞出时长(图案召唤时飞入用 500)
#define BIRD_HOP_MS          110    // RIDE 每跳时长
#define BIRD_NOTE_MS         45     // 落地音时长
#define BIRD_NOTE_AMP        45     // 落地音响度(白天)
#define BIRD_NOTE_AMP_NIGHT  32

// ── 图案差异化 ────────────────────────────────────────────────────────
#define WAVE_STEP_MS         45     // 波浪错峰步进(原 pattern_reward 硬编码 45 移进来)
#define EQUAL_NOTE_MS        280    // 一条线齐唱长音
#define EQUAL_NOTE_AMP       60
#define ARP_AMP_NIGHT        40

// ── 夜晚 ─────────────────────────────────────────────────────────────
#define TICK_MS_NIGHT        60
#define TICK_AMP_NIGHT       30
#define SING_AMP_NIGHT       55
#define TWINKLE_MIN_F        24     // 星星切换间隔(帧,0.8~2s)
#define TWINKLE_MAX_F        60

// ── 和弦 ─────────────────────────────────────────────────────────────
#define CHORD_WINDOW_FRAMES  2      // 齐按收集窗口(帧;2 帧≈66ms,仍<100ms 红线)
#define CHORD_NOTE_MS        35
#define CHORD_NOTE_AMP       60
