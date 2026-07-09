// magic_wand 调参常量集中一处(SPEC.md §11)。默认值待实机标定。
#pragma once

// ── 手势判定 / 微光兜底 ──────────────────────────────────────────────
#define GESTURE_POLL_MS        30     // 轮询周期
#define RECAST_COOLDOWN_MS     500    // 同一手势重复触发的冷却(覆盖一次 CASTING 动画)
#define SHIMMER_IDLE_MS        2500   // 退化方案:固定低频存活性 ping 间隔(无"在场未分类"信号,见 unit_gesture.h)
#define ATTACH_RETRY_MS        2000   // 无单元时的重试周期(仿其它 app 惯例)
#define ERR_STREAK_LOST        20     // 连续读失败判定拔线(仿 feed_monster/chain_lab 惯例)

// ── 法术编排 ─────────────────────────────────────────────────────────
#define PEEK_HOLD_MS           300    // 躲猫咒:缩小停顿时长
#define WHIRL_STEP_MS           80    // 旋风咒:粒子步进间隔(8 方位查表)
#define ZOOM_SCALE_PCT          130   // 冲天咒:放大比例

// ── 法术书 / 派对 ────────────────────────────────────────────────────
#define SPELLBOOK_SIZE           9
#define PARTY_STEP_MS           300   // 派对接力回放:每个法术压缩时长
#define COMBO_WINDOW_MS         2500  // 隐藏法术:连击判定窗口
#define COMBO_NEEDED              3   // 连击判定:窗口内需要几种不同手势

// ── 魔法棒 RGB ───────────────────────────────────────────────────────
#define WAND_MAX_BRIGHTNESS      60   // 贴身道具,亮度上限可比底座(48)略高但仍克制
