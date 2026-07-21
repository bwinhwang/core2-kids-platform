#include "kv_store.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "kv_store";
static nvs_handle_t s_h;
static bool s_ready = false;

esp_err_t kv_store_init(const char *ns)
{
    // 标准 ESP-IDF NVS 擦除重试套路(分区版本不兼容 / 无空闲页时先擦后重 init,
    // 官方示例/编程指南通用写法,非板级细节,不走 MCP 查证流程)。
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除重建(%s),擦除后重试", esp_err_to_name(err));
        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase 失败: %s", esp_err_to_name(erase_err));
            return erase_err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init 失败: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_open(ns, NVS_READWRITE, &s_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(\"%s\") 失败: %s", ns, esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "kv_store 就绪(namespace=\"%s\")", ns);
    return ESP_OK;
}

esp_err_t kv_store_get_i32(const char *key, int32_t *out, int32_t def)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = def;
    if (!s_ready) return ESP_OK;
    int32_t v;
    esp_err_t err = nvs_get_i32(s_h, key, &v);
    if (err == ESP_OK) *out = v;
    return ESP_OK;  // key 不存在/读失败均已用 def 兜底,不算调用方错误
}

esp_err_t kv_store_set_i32(const char *key, int32_t val)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_i32(s_h, key, val);
    if (err != ESP_OK) { ESP_LOGW(TAG, "set_i32(%s) 失败: %s", key, esp_err_to_name(err)); return err; }
    return nvs_commit(s_h);
}

esp_err_t kv_store_get_f32(const char *key, float *out, float def)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = def;
    if (!s_ready) return ESP_OK;
    float v;
    size_t len = sizeof(v);
    esp_err_t err = nvs_get_blob(s_h, key, &v, &len);
    if (err == ESP_OK && len == sizeof(v)) *out = v;
    return ESP_OK;
}

esp_err_t kv_store_set_f32(const char *key, float val)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_set_blob(s_h, key, &val, sizeof(val));
    if (err != ESP_OK) { ESP_LOGW(TAG, "set_f32(%s) 失败: %s", key, esp_err_to_name(err)); return err; }
    return nvs_commit(s_h);
}

esp_err_t kv_store_get_str(const char *key, char *out, size_t cap, const char *def)
{
    if (!out || cap == 0) return ESP_ERR_INVALID_ARG;
    if (s_ready) {
        size_t len = cap;
        esp_err_t err = nvs_get_str(s_h, key, out, &len);
        if (err == ESP_OK) return ESP_OK;
    }
    strncpy(out, def ? def : "", cap - 1);
    out[cap - 1] = '\0';
    return ESP_OK;
}

esp_err_t kv_store_set_str(const char *key, const char *val)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (!val) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_set_str(s_h, key, val);
    if (err != ESP_OK) { ESP_LOGW(TAG, "set_str(%s) 失败: %s", key, esp_err_to_name(err)); return err; }
    return nvs_commit(s_h);
}

esp_err_t kv_store_erase_ns(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_erase_all(s_h);
    if (err != ESP_OK) { ESP_LOGW(TAG, "erase_ns 失败: %s", esp_err_to_name(err)); return err; }
    return nvs_commit(s_h);
}
