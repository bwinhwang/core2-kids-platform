// kv_store —— NVS 封装(标定值/设置持久化)
//
// 每个 app 一个 namespace(如 "unit_bench"/"power_lab");init 一次后全局单 handle,
// 后续 get/set 直接用。不做 schema 迁移——键名一旦定案就别改类型,改了旧值读不出来
// 也无害(get 走 default 兜底)。
//
// 用法:
//   kv_store_init("unit_bench");
//   int32_t offset; kv_store_get_i32("ultra_offset", &offset, 0);
//   kv_store_set_i32("ultra_offset", new_offset);
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NVS flash(含 NO_FREE_PAGES/NEW_VERSION_FOUND 擦除重试)+ 打开本 app 的
 *        namespace(NVS_READWRITE)。全局只需调一次(建议 core2_board_init 之后)。
 * @param ns namespace 名(建议用 app 工程名,≤15 字符,NVS 限制)。
 * @return ESP_OK 成功;其余 = nvs_flash_init/nvs_open 失败。
 */
esp_err_t kv_store_init(const char *ns);

/** @brief 读 int32,key 不存在或读失败时返回 def(*out=def,函数仍返回 ESP_OK)。 */
esp_err_t kv_store_get_i32(const char *key, int32_t *out, int32_t def);

/** @brief 写 int32(内部立即 nvs_commit)。 */
esp_err_t kv_store_set_i32(const char *key, int32_t val);

/** @brief 读 float(NVS 无原生 float 类型,内部走 blob),key 不存在时返回 def。 */
esp_err_t kv_store_get_f32(const char *key, float *out, float def);

/** @brief 写 float(内部走 blob,立即 commit)。 */
esp_err_t kv_store_set_f32(const char *key, float val);

/** @brief 读字符串,key 不存在或 cap 不够时 out 写为 def 的拷贝(截断到 cap-1)。 */
esp_err_t kv_store_get_str(const char *key, char *out, size_t cap, const char *def);

/** @brief 写字符串(立即 commit)。 */
esp_err_t kv_store_set_str(const char *key, const char *val);

/** @brief 清空本 namespace 全部 key(nvs_erase_all + commit)。慎用,不可撤销。 */
esp_err_t kv_store_erase_ns(void);

#ifdef __cplusplus
}
#endif
