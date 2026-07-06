// chain_lab 可调参数(一行一个,实机标定改这里)
#pragma once

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
