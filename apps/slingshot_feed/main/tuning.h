// slingshot_feed 可调参数(一行一个,实机标定改这里)——SPEC.md §9 骨架逐条落地 +
// 链路/几何/节奏补充(标注"非 SPEC §9"的是实现细节,busy_bus tuning.h 同款纪律)。
#pragma once

// ── 屏幕 ─────────────────────────────────────────────────────────────
#define SCREEN_W  320
#define SCREEN_H  240

// ── 链路轮询节奏 ─────────────────────────────────────────────────────
// ★ 非 20Hz(busy_bus/chain_lab 惯例):SPEC §5.2 要求果子弹道 40~60fps 才够顺滑,
// 20ms 周期(50Hz)落在区间内。chain_bus 单次请求超时 40ms 有余量(REQ_TIMEOUT_MS,
// unit_chain_joystick.c),20ms 帧内一次 ADC + 一次按键请求仍充裕(未实机验证吞吐,
// README 记为待观察偏差)。
#define POLL_HZ            50
#define POLL_PERIOD_MS     (1000 / POLL_HZ)     // 20ms

// Chain 链上扫描的最大位置(单节点直连=1;encoder 若在链上也不影响 joystick 认领)
#define CHAIN_MAX_ID       4
#define ATTACH_RETRY_MS    2000                 // 没探到节点时的重扫周期
#define ERR_STREAK_LOST    8                    // 连续读失败多少次判"拔线/断电"

// 省电(桌面玩法:机身不动≠没人玩,靠摇杆活动 kick)
#define NAP_AFTER_MS       20000
#define DEEP_AFTER_MS      60000
#define PLAY_BRIGHTNESS    70
#define NAP_BRIGHTNESS     12

// 摇杆:原始 ADC(约 12 位,居中 ~2048)→ 归一化;标定值抄 chain_lab/busy_bus 定案
// (同一只摇杆,SPEC §1"摇杆标定值...抄定案,不另列")
#define JOY_HALF_SPAN      1500.0f
#define JOY_DEADZONE       0.12f
#define JOY_MOVE_KICK      0.15f
#define JOY_INVERT_X       1
#define JOY_INVERT_Y       0
#define JOY_SWAP_XY        0

// 自适应回中(实机标定,抄 busy_bus poll_joy 用):松手静止点偏离开机校准中心的
// 幅度,只在此带内才每帧校正中心;推杆超出这个带就暂停校正(避免长按被吃成新中心)。
#define JOY_RECENTER_BAND  0.35f
#define JOY_RECENTER_PCT   3

// 节点板载 RGB 亮度档 0~100(护眼压低)
#define NODE_RGB_BRIGHTNESS 40

// ══════════════════════════════════════════════════════════════════════
// ── 拉-放弹射(本作成败核心,P1 单独验证)── SPEC.md §9 原文 ────────────────
#define FIRE_MODE           0     // ★ 0=松手发射(真弹弓,默认) / 1=按 Z 发射(降级)
#define AIM_MIN             0.30f // 进入蓄力/显示预览的最小拉动量(归一化)
#define RELEASE_THRESH      0.12f // 判"松手"的回中阈值
#define RELEASE_WINDOW_MS   0     // ★ 0=跌破即发(默认)/ >0=窗口内跌落才算,实机 A/B
#define FIRE_LOCKOUT_MS     250   // 发射后输入锁定,防回中震荡幻影蓄力(§5.1)
#define PULL_HIST_LEN       4     // 拉力短历史窗样本数(力度取峰值、角度取松手前末稳定样)
#define LAUNCH_POWER        420   // ★ px/s per 单位拉力
#define GRAVITY             520   // ★ px/s^2
#define TRAJ_DOTS           8     // 预览虚线点数

// ── 命中 / 喂饱 / 收集 ── SPEC.md §9 原文 ──────────────────────────────
#define HIT_TOL_PX          28    // ★ 入嘴容差,宁大勿小
#define FEED_PER_ANIMAL     3     // ★ 每只喂几口长大
#define ANIMAL_QUOTA        4     // 喂够几只触发派对
#define MISS_SND_COOLDOWN_MS 400
#define MISS_FLOWER_MAX     6     // 草地 miss 小花上限

// ── 节奏 ── SPEC.md §9 原文 ────────────────────────────────────────────
#define GROW_MS             600
#define PARTY_HOLD_MS       3500
// ══════════════════════════════════════════════════════════════════════

// ── 弹道飞行 / 落地(非 SPEC §9 列出,实现细节)───────────────────────────
#define FLIGHT_MAX_MS       3000  // 飞行时间封顶(§5.2"飞行时间/出界封顶")
#define GROUND_Y_PX          222  // 落地判定 y(草地地平线附近,超过此线判"落地未中")

// ── EAT/MISS 短状态时长(非 SPEC §9,实现细节)────────────────────────────
#define EAT_MS               520  // 嚼两下的总时长(4 个相位 × 130ms)
#define MISS_MS              450  // miss 小花反馈停顿再重装填

// ── 弹弓几何(改进:移到屏幕左右正中、仍在底部;可往左上/右上两个方向发射)──────────
// anchor=兜/果子装填点=发射原点。居中(x=160)+ 对称 Y 叉后,拉杆朝哪边、果子就朝反向飞哪边,
// 瞄准从"右上单扇区"扩成左右一整个半圆——真技能轴(角度)的变化量直接翻倍(SPEC §5.5 力度轴
// 本就塌缩成常数,靠这个把"单轴指方向"救成"半圆扫瞄")。validate_spots 用 fabsf(dx) 天生对称,无需改判据。
#define SLING_ANCHOR_X       160
#define SLING_ANCHOR_Y       188
#define FORK_TIP_L_X         146
#define FORK_TIP_L_Y         150
#define FORK_TIP_R_X         174
#define FORK_TIP_R_Y         150
#define FORK_HANDLE_BOTTOM_Y 214
#define PULL_VISUAL_PX        40   // 拉动时兜(pouch)视觉最大位移 px,独立于 LAUNCH_POWER 物理标度
#define BAND_DOTS             3    // 皮筋每股用几个小圆点示意(不做运行时旋转贴图,§6 渲染红线)

// ── 动物位置/造型表(非 SPEC §9;§5.5 校验准则用 LAUNCH_POWER/GRAVITY 现算)───────
#define ANIMAL_SPECIES          5   // 熊 / 小鸡 / 青蛙 / 兔 / 猫(改进 C:各自轮廓+叫声)
#define ANIMAL_W                34
#define ANIMAL_H                40
#define ANIMAL_BODY_SZ           26
#define ANIMAL_EYE_SZ            4   // 眼睛基准边长(sprites 建眼 + render_all 眨眼/锁定放大共用,单一来源)
#define MOUTH_OPEN_H              9
#define MOUTH_CLOSED_H            3

// ── 果子(非 SPEC §9;色相着色见 SPEC §1)─────────────────────────────────
#define FRUIT_SZ                12

// ── 派对彩纸(非 SPEC §9;§7"限量 ≤8 片,15–20fps 庆祝档")───────────────────
#define CONFETTI_N                8

// ── 动物"活起来"(改进 A:期待 / 自教 / 生命感;逐帧偏移都在 render_all 一处落,零额外失效区)──
// 眼睛跟瞄准点、身体微倾、瞄准对准嘴时兴奋+落点光圈——把 SPEC §11.2 预授权的"落点光圈+自教"
// 从兜底升级成核心玩法:孩子扫过去动物就有反应,瞄准 = 逗一只馋动物,既自教又讨喜。
#define EYE_TRACK_PX             2    // 眼睛跟随瞄准点/飞行果子的最大像素偏移
#define LEAN_PX                  3    // 身体朝拉的反方向(=发射方向)微倾的最大像素
#define AIM_LOCK_TOL_PX      (HIT_TOL_PX * 3 / 2)   // 预览弧任一点距嘴 < 此值 → "瞄准锁定"(兴奋+光圈+一声"来嘛~")
#define HALO_SZ              (HIT_TOL_PX * 2)        // 落点光圈直径(套在嘴上呼应命中容差)
#define BLINK_PERIOD_MS       3400   // 眨眼周期(生命感)
#define BLINK_MS               120   // 单次眨眼时长
#define LOCK_SND_COOLDOWN_MS   350   // "来嘛~"锁定音节流(防连续扫瞄机枪式响)
#define IMPATIENT_AFTER_MS    4500   // AIM 态一直不蓄力多久后动物"还要~"催一下并小跳
#define IMPATIENT_HOP_MS       340   // "还要~"小跳时长

// ── "好朋友"聚集排(改进 B:喂饱的动物蹦到草地边攒成一排,凑够 ANIMAL_QUOTA 只 → 派对群跳)──
// 把隐形的 s_animal_fed_count 配额变成看得见、留得下的收集:成功第一次有持久痕迹(此前只有失败=小花留痕)。
#define FRIEND_SZ               18    // 朋友小精灵直径(身体色=所喂物种,+两只小眼)
#define FRIEND_Y               206    // 朋友排 y(篱笆上方,左右分布避开正中弹弓)
