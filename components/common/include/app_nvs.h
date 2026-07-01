// app_nvs.h —— NVS 初始化助手(几乎每个工程第一步都要)
#pragma once
#include "esp_err.h"

// 初始化 NVS;遇到 NO_FREE_PAGES/NEW_VERSION_FOUND 自动 erase 重来
esp_err_t app_nvs_init(void);
