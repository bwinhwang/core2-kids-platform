// 主机跑 layout.c 用的 esp_log.h 假体(verify_host.sh 用 -I 抢在 IDF 版本前面)。
// 存在的意义:让 layout.c **一行不改**就能在主机上编译 —— 测的是要烧进板子的那份代码,
// 不是它的翻译版。2026-08-13 的教训:tools/preview.py 的 Python 版校验一直是过的,
// C 版三张图纸全挂,两份实现各说各话,只有实机才暴露。
#pragma once

#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
