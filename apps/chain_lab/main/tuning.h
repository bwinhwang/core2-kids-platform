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

// 摇杆:原始 ADC → 归一化(实测这颗是 16 位、居中 ~32700,不是注释里曾写的 12 位/2048)
#define JOY_HALF_SPAN      1500.0f              // 满偏离中心的 ADC 量程(留余量,易打满)
#define JOY_DEADZONE       0.12f                // 死区(以内视为居中,不点亮/不算移动;仅诊断台 UI 用)

// ── 防打盹的"有人在玩"判据(2026-07-14 实机标定,见 README「摇杆回中偏置」)──────────
// 主判据 = **帧间 ADC 变化量**,不是绝对偏移:这颗摇杆被推过一次后机械回中点就永久偏
// ~220 ADC(≈0.15 归一化,实测最大 0.21),拿绝对偏移当判据会被这个恒定偏置顶穿 →
// 每帧 kick → 永不打盹。同 CLAUDE.md §7 用 IMU 帧间差判"有没有人在玩"的思路。
#define JOY_MOVE_ADC       80                   // 帧间任一轴变化超此 = 在推(实测静置噪声地板峰值 43)
// 副判据:位置型玩法下孩子会推着杆保持瞄准(帧间不变),偏移显著时也算在玩。
// 0.35 远高于实测最大回中误差 0.21,不会被恒定偏置顶穿。
#define JOY_HOLD_KICK      0.35f

// ── 自适应回中(修上面那个 ~0.15 的回中偏置;位置型专属收紧,别照抄 busy_bus)────────
// 只在偏移还在回中带内(说明没在大幅推杆)才把中心慢慢拉向当前读数;且 **只在 PLAY_IDLE +
// 爪子在顶** 时才校正(crane_game_recenter_ok()),一进下降/抓取就冻结 —— 否则孩子推着杆
// 瞄准的那几秒会被吃成新中心,吊臂自己滑回屏幕中间(busy_bus 是速度型,没这个风险)。
#define JOY_RECENTER_BAND  0.25f                // 刚够盖住实测回中偏移(0.15,峰值 0.21)
#define JOY_RECENTER_PCT   1                    // 每帧校正百分比(20Hz 下时间常数≈5s)

// 吊臂死区:盖住 ±40 ADC 噪声导致的 ±3px 抖动(松手吊臂要稳稳停住);死区外线性重缩放,无跳变
#define JOY_ARM_DEADZONE   0.04f

// 临时标定日志(定案后置 0):每 JOY_CAL_LOG_EVERY 帧打一行原始 ADC / 校准中心 / 归一化 /
// 帧间变化峰值 dmax —— 排查摇杆漂移时打开它,别凭猜测调参数(见 README「摇杆回中偏置」)。
#define JOY_CAL_LOG          0
#define JOY_CAL_LOG_EVERY   10                  // 20Hz 下 = 每 0.5s 一行

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
