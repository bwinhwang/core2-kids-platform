#include "app_slot.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core2_power.h"
#include "haptics.h"

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

// ── 电源键退出(游戏侧)────────────────────────────────────────────────
// 低频轮询 AXP192 PEK 按压标志(短按/长按都算,阈值坑见 core2_power.h);
// 命中即轻震确认 → 设回 factory → 重启。
// 独立小任务,不挂在 game_task 上(轮询走 I2C,别占 60Hz 循环)。
// 轮询要够快:BSP 配置下按住 ~1s 置"长按"标志、≥4s AXP 硬断电,须赶在断电前消费。

#define PEK_POLL_MS    150
#define PEK_TASK_STACK 3072

static void button_exit_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(PEK_POLL_MS));
        if (core2_power_pek_pressed()) {
            ESP_LOGI(TAG, "电源键按下 → 回 launcher");
            haptics_play(HAPTIC_WAKE);            // 轻震确认"听到了"(未 init 则安全跳过)
            app_slot_return_to_factory();          // 开机已设过,这里重申一次无妨
            vTaskDelay(pdMS_TO_TICKS(150));        // 让震动发出去再重启
            esp_restart();
        }
    }
}

esp_err_t app_slot_enable_button_exit(void)
{
    (void)core2_power_pek_pressed();        // 丢弃开机按键的残留标志,防一进游戏就退出
    BaseType_t ok = xTaskCreate(button_exit_task, "pek_exit", PEK_TASK_STACK,
                                NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "电源键退出任务创建失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "电源键短按=回主菜单 已启用");
    return ESP_OK;
}
