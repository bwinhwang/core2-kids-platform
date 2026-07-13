// busy_bus 可调参数(一行一个,实机标定改这里)——SPEC.md §9 骨架 + 链路/几何补充
#pragma once

// ── 屏幕 ─────────────────────────────────────────────────────────────
#define SCREEN_W  320
#define SCREEN_H  240

// ── 链路轮询节奏(仿 chain_lab)────────────────────────────────────────
#define POLL_HZ            20
#define POLL_PERIOD_MS     (1000 / POLL_HZ)     // 50ms

// Chain 链上扫描的最大位置(单节点直连=1;encoder 若在链上也不影响 joystick 认领)
#define CHAIN_MAX_ID       4
#define ATTACH_RETRY_MS    2000                 // 没探到节点时的重扫周期
#define ERR_STREAK_LOST    8                    // 连续读失败多少次判"拔线/断电"

// 省电(桌面玩法:机身不动≠没人玩,靠摇杆活动 kick)
#define NAP_AFTER_MS       20000
#define DEEP_AFTER_MS      60000
#define PLAY_BRIGHTNESS    70
#define NAP_BRIGHTNESS     12

// 摇杆:原始 ADC(约 12 位,居中 ~2048)→ 归一化;标定值抄 chain_lab 定案(同一只摇杆)
#define JOY_HALF_SPAN      1500.0f
#define JOY_DEADZONE       0.12f
#define JOY_MOVE_KICK      0.15f
#define JOY_INVERT_X       1
#define JOY_INVERT_Y       0
#define JOY_SWAP_XY        0

// 节点板载 RGB 亮度档 0~100(护眼压低)
#define NODE_RGB_BRIGHTNESS 40

// ── 车感(本作成败核心,P1 单独验证)───────────────────────────────────
#define BUS_SPEED_MAX        90    // ★ px/s;太快失控、太慢无聊
#define JOY_EMA_PCT          25    // ★ 速度平滑权重(松手滑行长短)
#define HEADING_COUNT          8
#define HEADING_HYST_DEG      10   // 朝向切换迟滞
#define HEADING_MIN_SPEED_PX   8   // 低于此速度不更新朝向(防静止时抖动换图)

// 驾驶模式(§11①降级预案,一行切换 A/B):0=速度型(推多少走多快) 1=恒速方向(推=走 松=停)
#define BUS_DRIVE_MODE          0
#define BUS_CONST_SPEED_PCT    60  // 恒速模式的速度 = BUS_SPEED_MAX × 此百分比

// ── 接送 ─────────────────────────────────────────────────────────────
#define PICKUP_RADIUS_PX     26    // ★ 蹭到就算,宁大勿小
#define DOOR_ZONE_W          48
#define DOOR_ZONE_H          32
#define PASSENGERS_PER_ROUND  3
#define BUS_CAPACITY           1   // v1 定死,升 2 是趣味增量批候选(§11③)
#define HOP_MS               350
#define WRONG_DOOR_SND_COOLDOWN_MS 800

// ── 喇叭 / 节奏 ───────────────────────────────────────────────────────
#define HONK_COOLDOWN_MS     300
#define HONK_WAVE_RADIUS_PX   60
#define PARTY_HOLD_MS        3500

// ── 撞障碍(滑行反馈节流,非 SPEC §9 列出,实现细节)──────────────────────
#define BUMP_SND_COOLDOWN_MS 250   // 贴着障碍蹭防连环"啵啵啵"

// ── 巴士几何 / 渲染(SPEC §7)──────────────────────────────────────────
#define BUS_SPRITE_PX   28         // 8 张朝向精灵边长(ARGB8888)
#define BUS_R           14         // 碰撞半径(= BUS_SPRITE_PX/2)

// ── 派对彩纸 ─────────────────────────────────────────────────────────
#define CONFETTI_N   8
