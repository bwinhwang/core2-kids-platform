// 手感调参集中地(CLAUDE.md §8.3 同款体例)—— 这些数值只能 build+flash+抱真机实测调。
// P1 目标:群体"重而稳不飘",倾斜赶群自然散开、不结坨不抖(SPEC §12 P1 验收)。
//
// 本文件 = SPEC.md §9 全量落地(含 P2/P3 才用得到的常量,先占位、标注未启用)+
// P1 编译实际需要、但 SPEC §9 未列出的补充项(逐项注明理由,不是擅自加戏)。
#pragma once

// ── 群体(SPEC §9)────────────────────────────────────────────────────
#define ANIMAL_COUNT        10     // ★ 视觉混乱就减到 8
#define ANIMAL_KINDS        2      // ★ 分拣认知过难可降 1(全归一家),过易升 3
                                    //   P1 代码按 ANIMAL_KINDS==2、5:5 均分实现,调此值需同步改 flock_init
#define ANIMAL_R            8
#define GAIN_JITTER_PCT     12     // 每只增益抖动 ±%,群体散开的关键
#define SEP_PAD             2      // 软分离触发间距余量
#define SEP_CORRECT_PCT     35     // ★ 每帧位置修正比例(太大抖、太小叠)

// ── 手感(tilt_maze 定案值起步,SPEC §9)──────────────────────────────
// 2026-07-12 P1 实机反馈"响应可以再快一点"→ 按 tilt_maze README 既定调参路径:
// 提 GAIN(1800→2100)+ 减滤波迟滞(ALPHA 0.25→0.32);死区/封顶不动。
#define TILT_GAIN           2100
#define DAMPING             0.90f
#define VEL_MAX             180    // 低于 maze 的 220:群体可读性优先
#define DEADZONE            0.06f  // 减除式,勿动(tilt_maze 定案)
#define TILT_ALPHA          0.32f

// ── 门 / 布点(SPEC §9,P2 已启用)─────────────────────────────────────
#define GATE_W              44     // 门区宽 ≈ 动物直径×2.75
#define GATE_BOUNCE_SPEED   70
#define BOUNCE_SND_COOLDOWN_MS 300 // 同一只动物的弹出反馈节流(§5.2,game_state 侧计时)
#define CORNER_BUSH_R       36     // 四角灌木碰撞圆半径(见 scene.h)
#define SCATTER_MIN_GAP     24
#define SCATTER_GATE_CLEAR  40
#define SCATTER_MAX_TRIES   200

// ── 节奏 / 彩蛋(SPEC §9,全部启用)─────────────────────────────────────
#define CAPTURE_ANIM_MS     350    // 捕获"蹦进门"动画时长(§5.3)
#define PARTY_HOLD_MS       3500   // 派对停留(§4),期间 nap_eligible=false
#define CONFETTI_N          8      // 派对彩纸片数上限(SPEC §6/§7:≤8,§6.5 庆祝档)
#define HOME_STRETCH_COUNT  8      // 归家数 ≥此值灯带基色加亮一档(§5.3"冲刺感")
#define ATTRACT_TILT_THRESH 0.22f  // 睡醒倾斜阈值(g,水平两轴合成幅度)
#define SHAKE_THRESH        1.2f   // 摇一摇:帧间三轴加速度变化和 > 此值算"晃了一下"(busy_knobs 定案)
#define SHAKE_NEEDED        3      // 带泄漏攒够几下才算"摇一摇",防单次磕碰误触
#define SHAKE_COOLDOWN_MS   2000   // 触发后冷却(SPEC 值;busy_knobs 用 1500,本作群体音效更长取 2000)

// ── P1 补充项(SPEC §9 未列,编译/手感落地必需,逐项注明理由)──────────

// 工作区尺寸:SPEC §9 没单列,但碰撞/布点/渲染全依赖它,直接抄 tilt_maze 的整屏工作区。
#define PLAY_W          320.0f
#define PLAY_H          240.0f

// 屏幕轴 ↔ IMU 轴映射:SPEC §1"连同 tilt_maze 的定案值起步(TILT_INVERT_X=1 等,
// 同一台机器同一标定)"——数值原样抄 tilt_maze/main/tuning.h,不是本轮新标定。
#define TILT_SWAP_XY    0
#define TILT_INVERT_X   1
#define TILT_INVERT_Y   0

// 撞边回弹系数:tilt_maze physics.c 同款("像被轻轻顶一下",不粘墙不来回震);
// SPEC §9 未单列,但 §5.1③"滑行碰撞"要求就是这一套。
#define WALL_RESTITUTION 0.2f

// 撞墙反馈阈值:SPEC §6 表格"撞栅栏/灌木(速度超阈)"要求一个阈值,但 §9 没给数值,
// 沿用 tilt_maze BUMP_MIN_SPEED 定案值起步(法向速度 px/s,低于此不算"够快",不反馈)。
#define BUMP_MIN_SPEED   35.0f

// 撞墙音节流:SPEC §6"极轻 boing(节流)"——tilt_maze 只有一颗球不需要节流,
// chick_pour 一群 10 只可能同时蹭栅栏,新增一个全群共用的节流窗口防机枪音
// (类比 §9 已有的 BOUNCE_SND_COOLDOWN_MS,但那个是 P2 门弹出专用,语义不同不能复用)。
#define WALL_BUMP_SND_COOLDOWN_MS 250

// 物理步长:SPEC §9 未列,tilt_maze 同款(60Hz 固定 dt;FREERTOS_HZ=1000 才能撑住)。
#define PHYS_DT         (1.0f / 60.0f)
#define PHYS_PERIOD_MS  16
