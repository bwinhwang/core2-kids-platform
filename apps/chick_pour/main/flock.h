// 群体倾斜物理 + 门判定 + 重散布点(SPEC §5.1/§5.2/§5.4)。
// 只吃"倾斜向量",不碰 LVGL/I2C(§3.1 分层)。物理与视觉分离:本模块只算
// x/y/vx/vy 与事件,精灵怎么画由 critters.c 负责。
#pragma once

#include <stdbool.h>
#include "imu_mpu6886.h"

typedef enum { ANIMAL_CHICK = 0, ANIMAL_DUCK = 1 } animal_kind_t;

// 本帧门区判定结果(每帧由 flock_step 写,game_state 消费)
typedef enum {
    GATE_EV_NONE = 0,
    GATE_EV_CAPTURE,   // 进了对的家:已从物理仿真移除(active=false),位置停在门区(动画起点)
    GATE_EV_BOUNCE,    // 进错家:已沿门法线温柔弹出(位置/速度已改),反馈由调用方节流后发
} gate_event_t;

typedef struct {
    float          x, y;          // 位置(px,屏幕坐标)
    float          vx, vy;        // 速度(px/s)
    float          gain_mul;      // 增益抖动倍率(1±GAIN_JITTER_PCT%),init 时随机定死,整局不变
    animal_kind_t  kind;          // 小鸡→鸡窝 / 小鸭→池塘(语义匹配,SPEC §5.2)
    bool           active;        // false = 已归家,从物理仿真移除(§5.3),重散时复活
    uint8_t        gate_event;    // gate_event_t:本帧门判定结果,每帧清零重写
    bool           bumped;        // 本帧是否撞上栅栏/灌木/家外墙("够不够快"由调用方按 BUMP_MIN_SPEED 判)
    float          bump_speed;    // 本帧最大撞击法向速度(px/s)
} animal_t;

/** @brief 初始化 n 只动物:预置网格布点(带小抖动,永不失败)+ 各自随机增益抖动 +
 *         kind 按下标交替分配(5:5)+ 全部 active。 */
void flock_init(animal_t animals[], int n);

/** @brief 推进一帧(只处理 active 的动物):共享一份低通+减除式死区滤波 → 每只按各自
 *         gain_mul 转加速度→阻尼→封顶→积分 → 两两软分离(§5.1②)→ 门判定(§5.2,
 *         写回 gate_event;捕获即置 active=false)→ 栅栏/灌木/家外墙滑行碰撞(§5.1③,
 *         家外墙在门区段豁免,写回 bumped/bump_speed)。 */
void flock_step(animal_t animals[], int n, const imu_accel_t *accel_raw, float dt);

/** @brief 重散一批(SPEC §5.4):约束随机布点(两两 ≥SCATTER_MIN_GAP、离门区
 *         ≥SCATTER_GATE_CLEAR、避开灌木/两家、栅栏内),拒绝采样 SCATTER_MAX_TRIES 次
 *         不满足退化为预置网格(永不失败);布完跑校验断言,失败也走网格。
 *         全部动物复活(active=true)、速度清零;增益抖动保留(init 时定死)。
 *  @return true = 约束随机成功;false = 走了网格兜底(仅日志/调参参考,行为都正确)。 */
bool flock_scatter(animal_t animals[], int n);
