// shared_state.h —— 任务间共享状态(portMUX 快照,机制通用)
//
// 生产者(sensor/engine 任务)写,消费者(led/audio/display/supervisor)读。
// 用 portMUX 自旋锁保护一次性结构体拷贝,避免读到撕裂的半新半旧值,全放内部 RAM。
// payload 字段可按应用增删(记得同步 shared_state.c 的 init/get)。
#pragma once

#include "app_state.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float       intensity;     // engine 输出的连续强度 i∈[0,1]
    uint32_t    last_peak_ts;  // 最近一次离散事件时间戳(ms):消费者比对检测“新事件”
    float       peak_mag;      // 最近一次事件的强度
    app_state_t state;         // 当前应用状态
    float       batt_voltage;  // 电池电压(V)
} shared_state_t;

void shared_state_init(void);

void shared_state_set_shake(float intensity, uint32_t last_peak_ts, float peak_mag);
void shared_state_set_app_state(app_state_t s);
void shared_state_set_batt(float v);

void shared_state_get(shared_state_t *out);   // 一次性原子读取整份快照

#ifdef __cplusplus
}
#endif
