# kv_store —— NVS 封装(标定值/设置持久化)

给评估 app 存"重启后还要记得的值"用(如 `unit_bench` 的超声波零点标定、`power_lab` 的
上次续航估算基线)。每个 app 一个 namespace,`kv_store_init(ns)` 一次后全局单 handle。

## 用法

```c
kv_store_init("unit_bench");             // namespace = 建议用 app 工程名

int32_t offset;
kv_store_get_i32("ultra_offset", &offset, 0);   // key 不存在时 offset=0(不算错误)
kv_store_set_i32("ultra_offset", offset + step); // 内部立即 commit

float bias;
kv_store_get_f32("bias", &bias, 0.0f);
kv_store_set_f32("bias", bias);

char name[16];
kv_store_get_str("label", name, sizeof(name), "default");
kv_store_set_str("label", "encoder_a");

kv_store_erase_ns();  // 慎用:清空本 namespace 全部 key
```

## 设计取舍

- **不做 schema 迁移**:键名一旦定案就别改类型(如把某 key 从 i32 改成 f32),NVS 按类型
  存储,类型不符时 `nvs_get_*` 会失败,`kv_store_get_*` 一律用 `def` 兜底(不算调用方
  错误,函数仍返回 `ESP_OK`)——这是有意的设计:标定值读不到时用默认值继续跑,比让
  app 处理一堆 NVS 错误码更符合"评估台别因为一个存储小问题卡死"的原则。
- **float 走 blob**:NVS 没有原生 float 类型,`kv_store_get/set_f32` 内部用
  `nvs_get_blob`/`nvs_set_blob` 存 4 字节原始位模式。
- **每次 set 立即 `nvs_commit`**:评估台场景下写入频率低(标定值不会每帧写),不追求
  极致擦写寿命优化,简单优先、避免"忘记 commit 导致断电丢失"这类坑。
- **NO_FREE_PAGES / NEW_VERSION_FOUND 擦除重试**:标准 ESP-IDF NVS 初始化套路(官方
  示例/编程指南通用写法),分区版本不兼容或无空闲页时自动擦除重建,不需要人工介入。

## 依赖

`REQUIRES nvs_flash`(ESP-IDF 内置组件)。`nvs` 分区已在 `partitions.csv` 定义
(0x9000,16KB),各工程共享同一物理分区,不同 app 靠 namespace 隔离数据。
