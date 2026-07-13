#pragma once
/*
 * screenshot —— 串口触发的屏幕截图导出(开发调试用,量产可整组件裁掉)
 *
 * 用途:主机(Claude Code)跑 tools/screenshot.py 向 UART0 发 "SHOT\n",
 *       固件把当前 LVGL 活动屏渲染进 PSRAM(lv_snapshot),RLE+Base64 从
 *       日志串口吐回,脚本解码存 PNG。全程不打断游戏(独立低优先级任务,
 *       仅 snapshot 渲染瞬间持 LVGL 锁 ~50ms)。
 *
 * 依赖:CONFIG_LV_USE_SNAPSHOT=y(已进 sdkconfig.platform;旧工程需
 *       rm -f sdkconfig && fullclean 重建,否则编译期 #error 提醒)。
 *
 * 限制:只截"活动屏"(lv_screen_active)对象树;lv_layer_top/sys 上的
 *       浮层不入镜(平台各 app 均画在活动屏上,目前无此场景)。
 */
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 起串口监听任务(UART0 收到一行 "SHOT" 即截图)。须在 bsp_display_start
 * 之后调用;core2_board_init 已代调,app 无须自己碰。重复调用安全。 */
esp_err_t screenshot_init(void);

/* 立即截图并从串口导出(阻塞到吐完,秒级)。给程序化触发留的口子。 */
esp_err_t screenshot_dump_now(void);

#ifdef __cplusplus
}
#endif
