#include "app_slot.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "app_slot";

static const esp_partition_t *slot_partition(int idx)
{
    if (idx < 0 || idx >= APP_SLOT_COUNT) return NULL;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_APP_OTA_MIN + idx, NULL);
}

esp_err_t app_slot_return_to_factory(void)
{
    const esp_partition_t *fac = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (!fac) {
        ESP_LOGE(TAG, "找不到 factory 分区(分区表不对?)");
        return ESP_ERR_NOT_FOUND;
    }
    // 传 factory 分区 = 擦 otadata(esp_ota_ops.c 内部行为),之后任何重启都回 launcher
    esp_err_t err = esp_ota_set_boot_partition(fac);
    if (err != ESP_OK) ESP_LOGE(TAG, "设回 factory 失败: %s", esp_err_to_name(err));
    return err;
}

esp_err_t app_slot_launch(int idx)
{
    const esp_partition_t *p = slot_partition(idx);
    if (!p) {
        ESP_LOGE(TAG, "游戏槽 ota_%d 不在分区表里", idx);
        return ESP_ERR_NOT_FOUND;
    }
    // 含镜像校验:空槽/坏镜像在这里挡下,launcher 可温柔提示而不是重启进垃圾
    esp_err_t err = esp_ota_set_boot_partition(p);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "游戏槽 %s 启动失败(%s):空槽或镜像损坏", p->label, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "启动 %s @0x%06" PRIx32, p->label, p->address);
    esp_restart();  // 不返回
}

bool app_slot_present(int idx, char *name, size_t name_len)
{
    const esp_partition_t *p = slot_partition(idx);
    if (!p) return false;
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(p, &desc) != ESP_OK) return false;  // 空槽/无效
    if (name && name_len) {
        strlcpy(name, desc.project_name, name_len);
    }
    return true;
}

bool app_slot_info(int idx, app_slot_info_t *out)
{
    if (!out) return false;
    const esp_partition_t *p = slot_partition(idx);
    if (!p) return false;
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(p, &desc) != ESP_OK) return false;  // 空槽/无效
    strlcpy(out->project_name, desc.project_name, sizeof(out->project_name));
    strlcpy(out->version, desc.version, sizeof(out->version));
    strlcpy(out->date, desc.date, sizeof(out->date));
    strlcpy(out->time, desc.time, sizeof(out->time));
    return true;
}
