// chain_lab 可调参数(一行一个,实机标定改这里)
#pragma once

// ── 显示模式(SPEC.md §7 方案B:编译期开关)────────────────────────────────
// 0 = 抓娃娃机(默认,给孩子玩);1 = 隐藏诊断台(现状表盘/光点 UI,排障用,需改此宏重编重刷)。
#define CHAIN_LAB_DIAG_MODE  0

// 轮询节奏
#define POLL_HZ            20
#define POLL_PERIOD_MS     (1000 / POLL_HZ)     // 50ms

// Chain 链上扫描的最大位置(单节点直连=1;两节点级联=1,2)
#define CHAIN_MAX_ID       4
#define ATTACH_RETRY_MS    2000                 // 没探到节点时的重扫周期
#define ERR_STREAK_LOST    8                    // 连续读失败多少次判"拔线/断电"

// 省电(桌面玩法:机身不动≠没人玩,靠 chain 活动 kick;详见 CLAUDE.md §20.14)
#define NAP_AFTER_MS       20000
#define DEEP_AFTER_MS      60000
#define PLAY_BRIGHTNESS    70
#define NAP_BRIGHTNESS     12

// 编码器:绝对计数 → 表盘指针角度(度/格),纯视觉,随便设
#define ENC_DEG_PER_STEP   15

// 摇杆:原始 ADC(约 12 位,居中 ~2048)→ 归一化
#define JOY_HALF_SPAN      1500.0f              // 满偏离中心的 ADC 量程(留余量,易打满)
#define JOY_DEADZONE       0.12f                // 死区(以内视为居中,不点亮/不算移动)
#define JOY_MOVE_KICK      0.15f                // 偏移超此 = 有人在玩(kick 防打盹)

// 轴向标定(节点 ADC 增向 vs 手推方向不保证一致,现场翻符号;同 tilt_maze 的 TILT_INVERT_*)
#define JOY_INVERT_X       1                    // 左右反了就 0/1 互换(实测左右反 → 置 1)
#define JOY_INVERT_Y       0                    // 上下反了就 0/1 互换
#define JOY_SWAP_XY        0                    // 推左右却上下动(装反 90°)→ 置 1

// 节点板载 RGB 亮度档 0~100(护眼压低)
#define NODE_RGB_BRIGHTNESS 40

// ── 抓娃娃机机制(SPEC.md §9,默认值待实机标定)───────────────────────────
#define CRANE_X_RANGE_PX     220    // 吊臂可移动横向范围(screen-space)
#define DESCEND_PER_TICK       6    // 编码器每格增量对应下降像素(顺时针+,逆时针-)
#define DESCEND_MAX_PX       140    // 最大下降深度(爪子触底)
#define DESCEND_SNAP_TOL      10    // 接近最大深度时"转够了自动到底"的容忍区间
#define GRAB_ALIGN_TOL_PX     24    // 抓取横向对齐容差(战利品中心 ± 此值即可抓中)

// ── 深度分层(深度=选择器:战利品住在不同深度层,SPEC §11② 迭代)──────────────
#define PIT_LAYERS             3    // 坑内深度层数(浅/中/深),最深层 = DESCEND_MAX_PX
#define PIT_LAYER_STEP_PX     44    // 相邻两层的目标深度差(层带之间留 8px 中性间隙)
#define GRAB_DEPTH_TOL_PX     18    // 抓取纵向容差(层目标深度 ± 此值内视为够到了)
#define GRAB_OVERLAP_PX       12    // 命中时爪底压入战利品顶的视觉咬合量
#define TOUCH_DWELL_MS       700    // 碰住战利品且不再转曲柄,持续此时长自动收爪(不按键兜底)

// ── 趣味批(v2.2)────────────────────────────────────────────────────
#define CRANK_TICK_MIN_MS    150    // 拧曲柄"咔"声的最小间隔(节流,防连转刷屏噪音)
#define GOLDEN_CHANCE_PCT     33    // 正常战利品收进展示架后,空坑最深层冒出金星的概率(%)

#define GRAB_MS               500   // 抓取(爪子闭合)动画时长
#define ASCEND_MS             750   // 上升动画时长
#define DEPOSIT_MS            500   // 战利品落架 / 空爪弹回动画时长

// ── 展示架 / 派对 ────────────────────────────────────────────────────
#define PRIZE_TYPES             5   // 当前批次战利品种类数
#define PARTY_HOLD_MS         3500
