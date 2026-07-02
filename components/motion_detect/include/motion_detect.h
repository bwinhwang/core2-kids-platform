// motion_detect —— "有没有人在玩"检测(帧间加速度变化 + 去抖)
//
// 纯逻辑组件,无硬件依赖;每帧喂三轴加速度即可。固化两个实机踩坑(CLAUDE.md §20.6):
//
// 1) 判"没人玩"只看机身动作量,**不要看应用量(如球速)**:
//    机身平放但相对校准零点有残余倾斜时,物理量可能永远不归零(球以 ~20px/s 慢爬),
//    拿它当活动信号会"永不打盹"。机身动作量 = |Δax|+|Δay|+|Δaz|(帧间差),
//    平放静止 ≈0.005~0.03g(噪声尖峰偶达 ~0.08),被拿起/倾斜 >0.12g。
//
// 2) 判"真的动了"必须连续多帧去抖:平放时 IMU 单帧噪声尖峰偶尔超阈值,
//    单帧判定会误唤醒、把深度省电计时整段作废(实测 60s 几乎永远熬不满)。
//
// 用法(60Hz 帧循环):
//    motion_detect_t md;
//    motion_detect_init(&md, 0.12f, 3);                    // Core2 实测定案值
//    每帧: motion_detect_feed(&md, have ? acc : NULL);      // 读不到样本传 NULL
//    需要计静止的状态: if (motion_detect_tick_still(&md) > N) 进打盹;
//    等唤醒的状态:     if (motion_detect_tick_wake(&md)) 唤醒;
//    状态切换时:       motion_detect_reset(&md);
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // 配置
    float wake_thresh;    // "明显动作"阈值(g),Core2+Bottom2 实测 0.12
    int   wake_frames;    // 唤醒去抖:连续多少帧明显动作才算真动,实测 3
    // 状态(只读;由 feed/tick 维护)
    float motion;         // 最近一帧动作量 |Δax|+|Δay|+|Δaz|(g)
    int   still_frames;   // 连续"无明显动作"帧数
    int   moving_frames;  // 连续"明显动作"帧数(去抖用)
    // 内部
    float prev[3];
    bool  prev_valid;
} motion_detect_t;

/** @brief 初始化。wake_thresh/wake_frames 传 0 用 Core2 实测默认(0.12g / 3 帧)。 */
void motion_detect_init(motion_detect_t *md, float wake_thresh, int wake_frames);

/**
 * @brief 每帧喂一次三轴加速度(g);本帧没读到样本传 NULL(动作量记 0,不污染帧间差)。
 * @return 本帧动作量(g)。
 */
float motion_detect_feed(motion_detect_t *md, const float accel_g[3]);

/** @brief 在"关注静止"的状态每帧调:动作超阈值清零计数,否则累加;返回连续静止帧数。 */
int motion_detect_tick_still(motion_detect_t *md);

/** @brief 在"等唤醒"的状态每帧调:去抖后返回 true = 真的动了(单帧噪声尖峰被忽略)。 */
bool motion_detect_tick_wake(motion_detect_t *md);

/** @brief 清空静止/动作计数(进出状态时调;不清帧间差基准)。 */
void motion_detect_reset(motion_detect_t *md);

#ifdef __cplusplus
}
#endif
