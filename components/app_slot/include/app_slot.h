// app_slot —— 多 App 分区自举(factory=launcher + ota_0~5 游戏槽)
//
// 机制:借用 IDF 的 OTA 槽位选择(esp_ota_set_boot_partition / otadata),
//      **没有任何网络成分**——游戏 bin 是用数据线直接烧进各自 ota_x 分区的,
//      otadata 只是"下次启动哪个分区"的选择器。
//
// 启动流转(官方 Graphical Bootloader 范式):
//   launcher(factory)── app_slot_launch(n) ──► 重启进 ota_n 游戏
//   游戏 app_main 第一行 app_slot_return_to_factory()(= 擦 otadata,IDF 源码确认)
//   ──► 之后任何复位/崩溃/断电重启都落回 launcher,天然防变砖。
//
// ⚠️ 槽位数与 partitions.csv 的 ota_0~5 一一对应;改分区表须同步 APP_SLOT_COUNT。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SLOT_COUNT 6   // = partitions.csv 的 ota_0 ~ ota_5

/** @brief 把下次启动分区设回 factory(launcher)。
 *  游戏 app_main **第一行**就调(在任何外设初始化之前):此后游戏内任何
 *  复位/崩溃/断电重启都会回到 launcher(crash-safe)。 */
esp_err_t app_slot_return_to_factory(void);

/**
 * @brief (launcher 用)启动第 idx 号游戏槽:校验镜像 → 写 otadata → esp_restart。
 * @return 成功时不返回(设备重启);返回即失败——
 *         ESP_ERR_NOT_FOUND 分区不存在 / ESP_ERR_OTA_VALIDATE_FAILED 空槽或镜像损坏。
 */
esp_err_t app_slot_launch(int idx);

/** @brief 槽里有没有有效 App(读 esp_app_desc_t,不做完整校验,适合选择页刷新)。
 *  @param name 非 NULL 时带回该 App 的 project_name(用于选图标/调试)。 */
bool app_slot_present(int idx, char *name, size_t name_len);

// 与 esp_app_desc_t 字段等长(见 esp_app_format.h):project_name/version 各 32,
// date/time 各 16(编译期 __DATE__/__TIME__ 字符串,如 "Jul 17 2026"/"12:34:56")。
typedef struct {
    char project_name[32];
    char version[32];
    char date[16];
    char time[16];
} app_slot_info_t;

/**
 * @brief 取第 idx 槽的完整描述(project_name/version/date/time),供 launcher 数据驱动
 *        渲染卡片用。语义与 app_slot_present 一致(空槽/无效镜像返回 false),只是多带
 *        回几个描述符字段。
 * @return false = 空槽或读取失败(*out 内容不定,勿用)。
 */
bool app_slot_info(int idx, app_slot_info_t *out);

#ifdef __cplusplus
}
#endif
