// fish_pond 可调参数(一行一个,实机标定改这里)—— SPEC.md §9 为主体,不足处按同章节风格补齐
// ★ = 最可能要实机标定 / 调难度(SPEC §9 标法延续)
#pragma once

// ── 轮询节奏(SPEC §5:30Hz game_task)───────────────────────────────────
#define POLL_HZ            30
#define POLL_PERIOD_MS     (1000 / POLL_HZ)     // ≈33ms

// Chain 链上扫描(chain_lab 同套硬件,数值抄 chain_lab 定案)
#define CHAIN_MAX_ID       4
#define ATTACH_RETRY_MS    2000                 // 没探到节点时的重扫周期
#define ERR_STREAK_LOST    8                    // 连续读失败多少次判"拔线/断电"

// 省电(桌面玩法:机身不动≠没人玩,靠 chain 活动 kick;数值抄 chain_lab 定案)
#define NAP_AFTER_MS       20000
#define DEEP_AFTER_MS      60000
#define PLAY_BRIGHTNESS    70
#define NAP_BRIGHTNESS     12

// ── 摇杆标定(chain_lab 同硬件同装配,数值照抄起步;★ 若装配方向不同需重标定)──────
#define JOY_HALF_SPAN      1500.0f
#define JOY_INVERT_X       1        // ★
#define JOY_INVERT_Y       0        // ★
#define JOY_SWAP_XY        0        // ★

// 防打盹"有人在玩"判据(帧间 ADC 变化量为主,回中恒定偏置顶不穿;道理见 chain_lab README)
#define JOY_MOVE_ADC       80
#define JOY_HOLD_KICK      0.35f
// 自适应回中(修摇杆机械回中漂移;chain_lab 定案照抄)
#define JOY_RECENTER_BAND  0.25f
#define JOY_RECENTER_PCT   1

// 船控制死区(盖住摇杆居中噪声,死区外线性重缩放到满速)
#define BOAT_JOY_DEADZONE  0.04f

// ── 布局(§3 大对象合规表为准,改小 = 违 §8 红线)────────────────────────
#define WATERLINE_Y        48
#define LAYER_SPLIT_Y      144   // 浅/深层分界
#define BOAT_W             96
#define BOAT_H             40
#define BOAT_MIN_X         56    // 船中心活动范围(右侧给木桶让位)
#define BOAT_MAX_X         220
#define BAIT_SIZE          64
#define LINE_MIN_PX        12    // 收到头 = 出水
#define LINE_MAX_PX        176   // 放到底(饵 y ≈ 224)
#define BUCKET_X           244
#define BUCKET_Y           16    // 72×64,跨水线半浸

// ── 输入(chain_lab 定案照抄死区/基线)────────────────────────────────────
#define BOAT_SPEED_MAX     120   // ★ px/s 满杆
#define CRANK_PX_PER_DET   6     // ★ 每格线长变化;方向实机标定,反了翻符号
#define BAIT_EASE_TAU_MS   150   // 饵缓动(宽容物理)

// ── 鱼 ───────────────────────────────────────────────────────────────
#define FISH_FAT_W         96
#define FISH_FAT_H         72
#define FISH_FAT_SPEED     25    // ★ px/s
#define FISH_FAT_Y         104   // ★ 浅层巡游中心 y(WATERLINE_Y..LAYER_SPLIT_Y 之间)
#define FISH_LAZY_W        128
#define FISH_LAZY_H        96
#define FISH_LAZY_SPEED    15    // ★
#define FISH_LAZY_Y        190   // ★ 深层巡游中心 y(LAYER_SPLIT_Y..240 之间)
#define FISH_X_MARGIN      8     // ★ 巡游贴边留白
#define SENSE_R            80    // ★ 感知圈;P1 降级预案 = 调到 999(SPEC §12-①)
#define MOUTH_R            40
#define BITE_R             30    // ★ 宽容咬钩判定
#define APPROACH_MULT      1.5f
#define GIGGLE_COOLDOWN_MS 800
#define FISH_SWIM_FRAME_MS 250   // 游动 A/B 切换周期(2fps 摆尾,氛围档,SPEC §6.6)
#define FISH_RENDER_MS     50    // 巡游态渲染节流 = 20fps(SPEC §6.5 帧预算:两鱼错帧,常态合计≤15k px/帧)
#define CHOMP_HOLD_MS      250   // 咬合猛帧停留,再转入 HOOKED 收线循环

// ── 收线 / 元循环 ──────────────────────────────────────────────────────
#define REEL_LAZY_RATIO    0.5f  // 大懒鱼更重(摇一格只升一半)
#define WIGGLE_AMP_PX      8
#define WIGGLE_HZ          2
#define REEL_DOZE_MS       1200  // 停手多久算"打盹"(zzz 泡 + 摆动停,SPEC §6.3)
#define SPLASH_HOLD_MS     250   // 出水水花停留,再开始入桶抛物线
#define CATCH_FLIGHT_MS    800   // 罐头动画:鱼弧线入桶时长
#define BUCKET_FULL_N      3
#define PARTY_HOLD_MS      3500

// ── 配色(暖色家族,延续 chain_lab/crane_game 同色系)──────────────────────
#define COL_SKY            0xFCEAC8
#define COL_SUN             0xFFC94A
#define COL_SHALLOW         0x7FD1C9
#define COL_DEEP            0x2E7A82
#define COL_WATERLINE_FOAM  0xE8FBF6
#define COL_LAYER_SPLIT     0x1F5F68
#define COL_GRASS           0x4E9A6B
#define COL_STONE           0x8A8272

#define COL_BOAT_HULL       0xC97A3D
#define COL_BOAT_CABIN      0xF3EEE0
#define COL_LINE            0x6B6357
#define COL_BAIT            0xE6533C
#define COL_BAIT_HL         0xFFD9CE

#define COL_BUCKET          0x8A5A32
#define COL_BUCKET_RIM      0xDCD3C0
#define COL_DOT_EMPTY       0xCFC6B8
#define COL_DOT_FILLED      0xF5C242

#define COL_FAT_BODY        0xF5C242
#define COL_FAT_BELLY       0xFFF3C2
#define COL_LAZY_BODY       0x5C7A99
#define COL_LAZY_BELLY      0xB7C4D6
#define COL_EYE             0x3A3A38
#define COL_MOUTH           0x7A2B1C

#define COL_HINT_CARD       0xFFFFFF
#define COL_BUBBLE          0xDFF3FA
