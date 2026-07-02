// 手感调参集中地 (CLAUDE.md §8.3) —— 这些数值只能 build+flash+抱真机实测调。
// 目标:重而稳、不飘、好上手。改完直接 flash 看球。
#pragma once

// ── 工作区(M2 无迷宫,球被屏幕四边围住)────────────────────────────
#define PLAY_W          320.0f
#define PLAY_H          240.0f
#define BALL_R          12.0f    // 球半径(px);直径 24≈0.6 格(§18.4),40px 走廊留 8px 余地
#define GOAL_R          20.0f    // 到家判定半径(px,≈半格)

// ── 屏幕轴 ↔ IMU 轴映射(§8.3 step3)────────────────────────────────
// 默认不交换/不取反。抱真机看球:左右反了翻 INVERT_X,上下反了翻 INVERT_Y,
// 横竖串了开 SWAP_XY。一行改一个 0/1,重新 flash 即可。
#define TILT_SWAP_XY    0
#define TILT_INVERT_X   1    // 实测:上下左右都反,X、Y 同时取反
#define TILT_INVERT_Y   0

// ── 物理(倾斜→加速度→速度→位置)──────────────────────────────────
#define TILT_ALPHA      0.25f    // 低通滤波系数(越小越稳越钝)
#define TILT_DEADZONE   0.06f    // 死区(g):倾斜小于此当 0,防平放自己飘
#define TILT_GAIN       1800.0f  // 增益:px/s² per g。起步偏小,宁慢勿飘
#define VEL_DAMPING     0.90f    // 每帧速度阻尼(强摩擦让球会停)
#define VEL_MAX         220.0f   // 速度封顶 px/s(幼儿必过度倾斜,必须封顶)
#define WALL_RESTITUTION 0.2f    // 撞边回弹(像被轻轻"顶"一下)
#define BUMP_MIN_SPEED   35.0f   // 撞墙反馈触发阈值(法向速度 px/s,低于此不反馈)

// ── 物理步长 ─────────────────────────────────────────────────────
#define PHYS_DT         (1.0f / 60.0f)   // 固定 dt
#define PHYS_PERIOD_MS  16               // 任务周期(60Hz≈16ms;FREERTOS_HZ=1000)

// ── 状态机(§7)─────────────────────────────────────────────────────
#define ATTRACT_TILT_THRESH 0.22f        // 标题页:倾斜偏移超此(g 和)→ 开始
#define CALIB_FRAMES        48           // 校准采样帧数(~0.75s)
#define WIN_HOLD_MS         1800         // 过关庆祝停留时长(到下一关)

// ── idle 打盹 / 唤醒 / 省电(§7, §14)──────────────────────────────────
#define PLAY_BRIGHTNESS     60           // 常态背光(%)
#define IDLE_BRIGHTNESS     10           // 打盹背光(%,省电护眼)
#define IDLE_TIMEOUT_MS     12000        // PLAY 中无动作多久 → 打盹(降亮)
#define IDLE_STILL_THRESH   0.04f        // 帧间加速度变化小于此算"没动"(g)
#define IDLE_WAKE_THRESH    0.12f        // 帧间变化大于此算"动了" → 唤醒

// 深度省电:打盹后再持续无动作 → 关屏 + 灯带熄 + 切 M-Bus 5V,IMU 降频轮询
#define DEEP_IDLE_TIMEOUT_MS 60000       // 进打盹后再无动作多久 → 深度省电
#define DEEP_IDLE_POLL_MS    120         // 深度省电时 IMU 轮询周期(降 CPU 唤醒频率省电)
#define WAKE_DEBOUNCE_FRAMES 3           // 连续多少帧明显动作才算真唤醒:去抖,防单帧 IMU
                                         // 噪声尖峰把打盹→深度省电的 60s 计时打断/误唤醒

// ── 家长菜单(§13)──────────────────────────────────────────────────
#define PARENT_LONGPRESS_MS 3000         // 底部长按多久进家长菜单(防误触)
