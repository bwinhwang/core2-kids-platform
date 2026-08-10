// 主机假体:换成可复现的 rand()。测试要的是"任意随机序列下布点都合法",
// 所以 verify_host.c 用多个 srand 种子各跑一轮,而不是指望某一次侥幸通过。
#pragma once
#include <stdint.h>
#include <stdlib.h>
static inline uint32_t esp_random(void) { return ((uint32_t)rand() << 16) ^ (uint32_t)rand(); }
