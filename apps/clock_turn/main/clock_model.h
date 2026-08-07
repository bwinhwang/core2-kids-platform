// clock_model —— 转转钟的时间状态机(纯逻辑,无 ESP-IDF/LVGL 依赖,可主机单测)
//
// 内部只有一个状态量 t = 分钟数,0..719(12 小时制)。两针角度全部由 t 派生。
// 角度公式必须与 tools/preview.py::hand_angles() 同式(SPEC.md §5.1):
//   时针角度 = t * 0.5          // 一步 2.5°,连续爬行,不吸附整点(§0.1 知识点 2 的关键)
//   分针角度 = (t % 60) * 6.0   // 一步 30°,一圈 12 步
// 0° = 12 点钟方向,顺时针为正。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_MODEL_T_PERIOD 720   // t 的周期(12h × 60min)

/** @brief 把任意整数 t 归一化进 [0, 719](处理负数与超界回绕)。 */
int clock_model_wrap(int t);

/**
 * @brief 在当前 t 上叠加 delta_steps 个"步",每步 = min_per_step 分钟,返回回绕后的新 t。
 * @param t             当前时间量(应已在 [0,719],否则先内部 wrap)。
 * @param delta_steps   编码器帧间 delta(可正可负;正=顺时针=时间前进,由调用方按 ENC_INVERT
 *                      决定是否先翻符号,本函数不处理方向标定)。
 * @param min_per_step  每步对应多少分钟(SPEC §5.1 定案值见 tuning.h::MIN_PER_STEP)。
 */
int clock_model_step(int t, int delta_steps, int min_per_step);

/** @brief t → 时针角度(度,0°=12点方向,顺时针为正)。一步 2.5°,连续爬行,不吸附整点。 */
float clock_model_hour_angle(int t);

/** @brief t → 分针角度(度,0°=12点方向,顺时针为正)。一步 30°,一圈 12 步。 */
float clock_model_minute_angle(int t);

/** @brief 是否落在教学意义的"整齐时刻"(整点或半点,t % 30 == 0,共 24 个,SPEC §0.1/§5.2)。 */
bool clock_model_is_exact(int t);

/** @brief 是否落在"落格反馈"意义的整齐时刻(整点/半点/一刻,t % 15 == 0,共 48 个)。
 *  🔴 这是 §5.2 2026-08-06 判断题里新增的**第二个**判定函数,只给"转一格落格更亮"这类
 *  纯听觉/触觉反馈用,不改 clock_model_is_exact() 的教学语义,两者别混用。 */
bool clock_model_is_quarter(int t);

/**
 * @brief MODE_QUIZ 随机出题(SPEC §5.5,用户拍板豁免根 CLAUDE.md §11 的手工编排铁律)。
 *
 * 题池 = 粒度 grain_min 的全部时刻(默认 5 → 144 个),按 rand_u32 均匀映射抽取,
 * 排除与 current_t / last_target 相同的候选(避免"一进 QUIZ 就已答对"和"连续重复出题")。
 *
 * 🔴 随机源由调用方注入(main.c 传 `esp_random()` 的返回值),本函数**不直接依赖 ESP-IDF**——
 * 本文件其余部分保持"纯逻辑、可主机单测"的性质(见文件头注),真随机 vs 可控测试值的取舍
 * 交给调用方。碰撞(抽中被排除的候选)时线性向后找下一个未被排除的候选,保证一定终止
 * (候选池最少还剩 142 个)。
 *
 * @param current_t   当前钟面 t,新题不得与它相同。
 * @param last_target 上一题的目标 t(首次出题传一个不可能撞上的值,如 -1)。
 * @param grain_min   出题粒度分钟数(= tuning.h QUIZ_GRAIN_MIN)。
 * @param rand_u32    调用方提供的一个随机 32 位数(如 esp_random())。
 * @return 新的目标 t(0..719,且是 grain_min 的整数倍)。
 */
int clock_model_next_quiz(int current_t, int last_target, int grain_min, uint32_t rand_u32);

/** @brief MODE_QUIZ 到位判定(SPEC §5.6):精确相等,不允许"差不多就算对"。 */
static inline bool clock_model_quiz_hit(int t, int target) { return t == target; }

#ifdef __cplusplus
}
#endif
