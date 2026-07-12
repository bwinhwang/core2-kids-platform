// magic_wand v2.1「在场+手势」调参常量集中一处(SPEC.md §10)。默认值全部占位,
// 待实机标定(CALIB_LOG=1 打印 `CALIB pres raw=... ema=... size=... lvl=... state=...`
// 一行,回填 PRES_ON_TH/PRES_OFF_TH/PRES_LVL2_TH/PRES_LVL3_TH 后置 0 重编)。
//
// P1 落地"在场 API + 标定日志 + SEEK/DANCE 状态机 + 盘旋三档 + 九手势方向翻滚 +
// 近距音阶 + 你好/再见"(SPEC §12);P2(5 目标/方向拜访/在场兜底/BLOOM)、P3(派对/
// 暮色/流星彩蛋/招手引导)、P4(灯笼)的常量仍按 SPEC §10 全量列出——先占好位置,
// 不堵死后续加入点,但本阶段代码不消费它们(P2 的 VISIT_WAKE_N/PRES_WAKE_FALLBACK_MS
// 除外)。
//
// v2 光标模式跟手方案已实机否决(见 SPEC.md §0/README.md),CUR_*/LOST_GRACE_MS 等
// 滤波链常量随之整体删除,不再出现在本文件。
#pragma once

// ── 在场信号(§4.1,P1 核心)──────────────────────────────────────────
#define PRES_POLL_MS        33    // 30Hz,与 game_task 同拍
#define PRES_EMA_PCT        30    // EMA 新值权重 %
#define PRES_ON_TH          30    // 实机标定 2026-07-11:本底 0(单帧杂散 36 由 EMA 滤),远 15cm≈160
#define PRES_OFF_TH         15    // 迟滞下限
#define PRES_HOLD_MS      1500    // 离场保持(桥接实测 2.5s 级断续)
#define PRES_LVL2_TH       185    // 中档阈值(实测远 15cm≈150~165 归 L1)
#define PRES_LVL3_TH       235    // 近档阈值(实测贴近 5cm 顶满 255)
#define PRES_LVL_HYST       15    // 档位迟滞,防蹦档

// ── 舞蹈 / 翻滚(§5)─────────────────────────────────────────────────
#define DANCE_STEP_MS_L1   140    // 远档步速(最慢)
#define DANCE_STEP_MS_L2   100    // 中档步速
#define DANCE_STEP_MS_L3    70    // 近档步速(最快,CW/CCW 筋斗云沿用此速)
#define TUMBLE_MS          220    // 方向翻滚单次时长
#define TUMBLE_COOLDOWN_MS 400    // 同方向手势冷却
#define HELLO_GAP_MS      2000    // 离场多久后再入场才算新「你好」
#define HOME_FLY_MS       1200    // 回家动画时长

// ── P2 进度(占位,本阶段代码不消费,先占位置)───────────────────────
#define VISIT_WAKE_N          2
#define PRES_WAKE_FALLBACK_MS 45000

// ── 容错 / 标定(沿用)────────────────────────────────────────────────
#define ATTACH_RETRY_MS   2000
#define ERR_STREAK_LOST     20
#define CALIB_LOG            0    // 已回填(2026-07-11 实机标定),再标定改回 1

// ── 引导 / 派对(P3,沿用 v2 §10,本阶段代码不消费)───────────────────
#define ATTRACT_IDLE_MS    8000    // 必须 < 打盹判据 12s
#define PARTY_MS           5000

// ── 灯笼(P4,沿用 v2 §10,本阶段代码不消费)─────────────────────────
#define LANTERN_MAX_BRIGHTNESS 60
