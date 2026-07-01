// app_state.h —— 幼儿应用通用状态机
//
// 这套状态对多数幼儿玩具通用:欢迎 → 待机(呼吸)→ 活跃 → 省电 → 低电提示。
// 增删状态即可裁剪;转移逻辑在 main.c 的 supervisor 里(应用自定义)。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_INIT = 0,   // 初始化中
    APP_WELCOME,    // 开机欢迎(一次很轻的光/声,让家长知道开好了)
    APP_IDLE,       // 待机:低能耗呼吸/微光,等待交互
    APP_ACTIVE,     // 活跃:正在被玩,多模态输出跟随强度
    APP_DIM,        // 长时间静止:进一步压暗省电
    APP_LOW_BAT,    // 低电:叠加提示但【始终可玩】(见 kids_safety.h)
} app_state_t;

const char *app_state_name(app_state_t s);

#ifdef __cplusplus
}
#endif
