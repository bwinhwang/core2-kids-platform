// 喂怪兽 调参集中地 —— 这些数值要 build+flash+插超声波实测调。
// 目标:手远近→怪兽张嘴/音高/灯即时变(即时因果);靠近就喂到;怎么玩都不会输。
#pragma once

// ── 轮询 / 超声波采样节奏 ────────────────────────────────────────────────
#define POLL_PERIOD_MS      33     // 30Hz 游戏主循环
#define SONIC_READ_TICKS    3      // 每 N 帧读一次距离并重触发(3×33≈99ms ≥ 测量周期;太糊调大)
#define UNIT_RETRY_MS       2000   // 单元没插/没电时的重试探测间隔
#define ERR_STREAK_LOST     20     // 连续多少次 I2C 读失败判"拔线"

// ── 距离映射(mm;须按幼儿桌面/手持实际手臂距离标定)──────────────────────
#define NEAR_MM             60.0f  // 近端封顶:比这更近都算"最近"(嘴最大/音最高)
#define FAR_MM              450.0f // 远端:比这更远算"手离开了"(闭嘴、静默)
#define EAT_MM              90.0f  // 手进到这么近 = 喂食(吃区,大动作边沿触发)
#define EAT_EXIT_MM         140.0f // 手退到这么远才重新武装,防一次靠近连喂(须 > EAT_MM)
#define DIST_ALPHA          0.35f  // 距离低通滤波系数(去抖;越小越稳越钝)
#define SONIC_MOVE_MM       12.0f  // 两次读数差 > 此值 = "手在动" = 有人玩(喂 core2_sleep)

// ── 怪兽嘴(唯一每帧动的主对象;高度随远近变)──────────────────────────────
#define MOUTH_W             86     // 嘴宽(px)
#define MOUTH_H_MIN         10     // 闭嘴(远/无手):一条缝
#define MOUTH_H_MAX         74     // 张到最大(最近)
#define MOUTH_LERP_NUM      1      // 嘴高每帧朝目标插值:new += (tgt-new)*NUM/DEN(平滑)
#define MOUTH_LERP_DEN      3

// ── 声音(C 大调五声音阶 = 空气琴;怎么比划都和谐)──────────────────────────
#define PITCH_STEPS         8      // 距离量化成几档音高(近=高)
#define TONE_MS             150    // 每档音"叮"的时长
#define TONE_AMP            55     // 空气琴音量(0~100)

// ── 喂食循环(靠近就喂,攒够大庆祝)──────────────────────────────────────
#define WIN_FEED_COUNT      5      // 喂满几个饼干 → 全屏庆祝
#define COOKIE_EAT_MS       320    // 饼干飞入嘴的动画时长
#define COOKIE_RESPAWN_MS   260    // 吃掉后隔多久冒出下一颗
#define WIN_HOLD_MS         2600   // 庆祝持续,之后清零重开
#define BURST_COUNT         8      // 庆祝迸发的小星/饼干数量(限量,压帧预算)

// ── 灯带(core2_sleep 托管 IDLE/OFF;清醒态按远近切 NEAR/AMBIENT)────────────
#define LED_NEAR_T          0.55f  // t(近=1)超过此值 → 灯带 NEAR(更亮暖),否则 AMBIENT

// ── 省电(core2_sleep 托管;手在探头前活动也算"有人玩")───────────────────
#define PLAY_BRIGHTNESS     60
#define NAP_BRIGHTNESS      10
#define NAP_AFTER_MS        20000  // 桌面/手持玩法,静止判定比倾斜迷宫(12s)放宽些
#define DEEP_AFTER_MS       60000  // 深度省电会切 M-Bus 5V → 超声波断电,拿起机身才醒
