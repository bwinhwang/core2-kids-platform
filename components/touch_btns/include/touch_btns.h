#pragma once
/*
 * touch_btns —— Core2 屏下三个圆圈键区(BtnA/BtnB/BtnC)的平台级三键
 *
 * 硬件事实:Core2 的 FT6336U 触摸面板物理尺寸 320×280,比 LCD(320×240)向下多
 * 出 40px,那三个丝印圆圈落在 y≥240 的屏外扩展区。BSP 只把触摸挂给 LVGL indev
 * (画布仅 240 高),屏外圆圈的触点落在所有 LVGL 对象之外、走不到点击回调。本组件
 * 旁路 LVGL,直接轮询 FT6336U(内部 I2C 0x38)原始坐标,按 y 阈值 + x 三分区把
 * 圆圈识别成 BtnA/B/C,做成一套"跨 app 一致的全局元操作"。
 *
 * 三键默认功能(app 什么都不做即白拿,零代码):
 *   BtnA(左)  长按 ~800ms  → 回 launcher(app_slot_return_to_factory + esp_restart)
 *   BtnB(中)  短按        → 截屏(screenshot_dump_now,吐日志串口,主机 screenshot.py --watch 接)
 *   BtnC(右)  长按 ~1500ms → 关机(core2_power_shutdown,AXP192 0x32 bit7)
 * 破坏性键(回 launcher / 关机)用长按防误触——屏外圆圈被手掌蹭一下不会中招;关机
 * 门槛更高。命中/门槛常量见 touch_btns.c 顶部,是唯一需实机标定的旋钮(见 README)。
 *
 * 可重绑:每个键的「短按 / 长按」都是可覆盖的槽位。app 调 touch_btns_bind() 绑自己的
 * 回调即覆盖该槽(含 A/C——全部可覆盖);未绑的槽走上面的内置默认。既保留零代码 app
 * 白拿全局三键,又让想定制的 app 改任意键。用途例:评估台把 BtnB 改成"翻页";续航测试
 * 里把 BtnA 长按绑成"二次确认再回 launcher"防误退;给某键空回调=屏蔽该默认动作。
 *
 * 读 FT6336U 与 BSP 的 lvgl_port 触摸读并存无副作用(都只读当前触点、不清状态,
 * I2C 总线自带互斥)。INT 脚(G39)已被 BSP touch handle 独占,本组件走轮询、不碰 INT。
 */
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 起三键轮询任务(BtnA/B/C 的检测与全局动作全固化在内部)。
 *        须在 bsp_display_start()/core2_power_init() 之后调用(截屏依赖 LVGL,
 *        关机依赖 core2_power 已绑定 AXP192)。core2_board_init 已代调,app 无须自己碰。
 *        重复调用安全(只起一次任务)。
 * @param bus 内部 I2C 总线句柄(来自 core2_board_i2c()/bsp_i2c_get_handle(),
 *            FT6336U @0x38 就在这条总线上)。
 * @return ESP_OK;ESP_ERR_INVALID_ARG bus 为 NULL;其余为设备添加/建任务失败。
 */
esp_err_t touch_btns_init(i2c_master_bus_handle_t bus);

/* 三个物理键;左/中/右对应屏下三个丝印圆圈。 */
typedef enum {
    TOUCH_BTN_A = 0,   /* 左 */
    TOUCH_BTN_B = 1,   /* 中 */
    TOUCH_BTN_C = 2,   /* 右 */
    TOUCH_BTN_COUNT = 3,
} touch_btn_id_t;

/* 两种手势;短按=松手于长按门槛前,长按=按住达门槛(门槛见 touch_btns.c,A/B ~800ms、C ~1500ms)。 */
typedef enum {
    TOUCH_BTN_SHORT = 0,
    TOUCH_BTN_LONG  = 1,
    TOUCH_BTN_GESTURE_COUNT = 2,
} touch_btn_gesture_t;

/* 绑定回调:在轮询任务上下文里被调用(非 LVGL 任务),要碰 LVGL 请自行 bsp_display_lock。 */
typedef void (*touch_btn_cb_t)(void *user);

/**
 * @brief 覆盖某键某手势的动作。cb 非 NULL=改跑该回调;cb=NULL=恢复该槽的内置默认。
 *        建议在 app 初始化阶段(UI 交互开始前)调用;绑定即时生效。
 *        未被覆盖的槽仍是全局默认(A长按=回launcher / B短按=截屏 / C长按=关机),
 *        其余槽(如 A短按 / B长按 / C短按)默认无动作,绑了才有。
 * @param id 目标键;@param g 目标手势;@param cb 回调(NULL=清除);@param user 透传给回调。
 */
void touch_btns_bind(touch_btn_id_t id, touch_btn_gesture_t g, touch_btn_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif
