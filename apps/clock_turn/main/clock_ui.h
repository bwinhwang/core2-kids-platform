// clock_ui —— 钟面渲染:静态层(圆盘+边框+60刻度+12数字+信息区三底卡)一次性画,
// 动态层(两针 lv_line + 中心帽、状态条①两位模式开关、数字钟读数、反馈脸、提示弧)
// 按事件只刷各自的小脏矩形(根 CLAUDE.md §6 渲染红线)。
//
// 2026-08-06 大改版(SPEC §5.3):新增信息区三子区(①状态条两位模式开关+长按切换、
// ②数字钟按需揭晓/题面、③反馈脸 idle/yay/huh)+ 渐进提示弧。语音整章作废,
// 原"轻触播报"输入路径删除。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CLOCK_UI_FACE_IDLE = 0,   // 平静微笑(常态,两模式共同基线)
    CLOCK_UI_FACE_YAY,        // 大笑 + 眼睛弯成弧(QUIZ 答对)
    CLOCK_UI_FACE_HUH,        // 歪头 + 波浪嘴(QUIZ 按键但未到位)
} clock_ui_face_t;

/** @brief 状态条①长按满 `MODE_HOLD_MS` 时触发一次(在 LVGL 任务上下文调用,勿在其中
 *         调用 bsp_display_lock ——已经在 LVGL 自己的上下文里,不需要也不能再取锁)。
 *         回调里只应该记一个"待切换"标记或直接改应用层的模式变量,真正的模式号仍由
 *         main.c 的状态机决定,再回头调用 clock_ui_set_mode() 让 UI 跟上。 */
typedef void (*clock_ui_mode_toggle_cb_t)(void *user);

/**
 * @brief 建静态层(圆盘/边框/60刻度/12数字/信息区三底卡)+ 两根 lv_line 指针 + 中心帽 +
 *        信息区各动态子控件(状态条模式图标/连接点/电量壳/长按进度条、数字钟读数/占位点、
 *        反馈脸、提示弧)。只调一次(进场画一次,之后静态部分永不重画)。须在
 *        core2_board_init() 之后调用;内部自己 bsp_display_lock/unlock。
 */
void clock_ui_create(void);

/**
 * @brief 按 t(0..719 分钟)更新两针指向。t 与上次相同时是no-op(不碰 LVGL)。
 *        只移动两针 + 中心帽所在的小容器,不碰静态层,脏矩形≈两针包围盒(SPEC §6.2)。
 *        内部自己 bsp_display_lock/unlock。
 */
void clock_ui_set_time(int t);

/** @brief 注册状态条①长按切模式的回调(§5.3.5)。cb=NULL 取消注册。 */
void clock_ui_set_mode_toggle_cb(clock_ui_mode_toggle_cb_t cb, void *user);

/** @brief 应用层模式变化后调用,让状态条①的两位开关跟着换选中态(§5.3.5)。
 *         quiz=false → 槽A(人形)选中;quiz=true → 槽B(屏形)选中。no-op 保护:
 *         与上次相同时不重画。 */
void clock_ui_set_mode(bool quiz);

/** @brief 旋钮连接状态(拔线/插上)→ 状态条①连接点变色(绿/红)。no-op 保护。 */
void clock_ui_set_linked(bool linked);

/**
 * @brief ② 数字钟读数(SPEC §5.3.2.1/§5.4)。
 * @param show        false = 只显三个占位点(不泄露任何时刻)。
 * @param t           show=true 时要显示的时刻(MODE_FREE 揭晓态传当前 t;
 *                    MODE_QUIZ 传目标 t)。
 * @param quiz_style  底卡是否换成橙色描边+暖橙暗底(= 当前是 MODE_QUIZ)。
 * @param correct     是否把数字染成"答对"的绿色(MODE_QUIZ 庆祝期间为 true)。
 */
void clock_ui_set_panel(bool show, int t, bool quiz_style, bool correct);

/** @brief ③ 反馈脸表情切换(SPEC §5.3.4)。no-op 保护(与上次相同不重画)。 */
void clock_ui_set_face(clock_ui_face_t mood);

/**
 * @brief 渐进提示弧(SPEC §5.6/§6.1):走时针角、半径 HINT_R,从当前 t 指向目标 t
 *        的短边方向。width=0 隐藏(答对/换题时调用方传 0)。
 * @param cur_t    当前钟面 t。
 * @param target_t MODE_QUIZ 目标 t。
 * @param width    HINT_ARC_W1(第1次按错)/ HINT_ARC_W2(第2次+)/ 0(隐藏)。
 */
void clock_ui_set_hint(int cur_t, int target_t, int width);

/**
 * @brief MODE_QUIZ 答对庆祝(SPEC §6.3):钟面单次柔和泛光(非连闪)+ 12 刻度数字点亮
 *        (橙色)。数字何时变回原色由下一次 `clock_ui_set_panel(quiz_style=true, correct=false)`
 *        (= 下一题开始时)自然带过,本函数不负责收尾计时。
 */
void clock_ui_play_win(void);

#ifdef __cplusplus
}
#endif
