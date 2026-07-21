# AGENTS.md — AI 协作通用规范(ESP-IDF 项目)

本文件是通用规范,可复制到任意 ESP-IDF v6 项目。**项目特定内容请勿写在这里**,放 `CLAUDE.md`。

---

## 0. MCP 前置约束(硬性铁律,先读)

AI 协作依赖 **两个 Espressif 查询类 MCP**,二者必须**都已配置且可访问**才能开工:

| MCP | 用途 | 工具名(以你的 MCP 配置为准) |
|---|---|---|
| **官方文档** | ESP-IDF 文档语义检索 | `mcp__…Espressif_Docs__search_espressif_sources` |
| **组件注册表** | 组件 README / API / changelog | `mcp__…Espressif_Registry__search_components`、`fetch_component_detailed_information` |

**铁律(不可绕过):**

1. **驱动 / 寄存器 / API 先查这两个 MCP 再写**(详见 §1)。有任一不可访问就**立刻停止、交回用户接管**,不要凭训练数据猜着往下写。
2. **编译 / 刷写 / 设 target / 清理走命令行 `idf.py`**(见 §3);多工程仓库编译必须 `idf.py -C <工程目录>` 指定工程。(曾用的 esp-idf stdio build MCP 固定指仓库根、多工程后不适用,已弃用;刷写细节见项目 `CLAUDE.md`。)

---

## 1. 不要靠猜改代码 —— 先查 MCP

涉及 ESP-IDF / 官方组件的 API 行为、初始化顺序、状态机时,**先查 MCP 再动手**。不要凭印象写"应该这么用"。

**强制查 MCP 的范围**:

- 外设驱动:I2C / I2S(新 `i2s_std`/`i2s_pdm`,**不是** legacy `driver/i2s.h`)/ SPI / UART / GPIO / LEDC / RMT / ADC
- 音频:ESP-ADF pipeline、`esp_codec_dev` 抽象、ES8311 等具体 codec 寄存器
- 蓝牙:NimBLE host、GAP、GATT、与 Wi-Fi 的 coex
- Wi-Fi:STA/AP、provisioning、power-save
- 内存:PSRAM、malloc caps、partition、NVS
- `esp_event` loop、FreeRTOS task/queue/semaphore
- OTA、secure boot、flash encryption

用 **官方文档 MCP**(`search_espressif_sources`)查 API/文档,用 **组件注册表 MCP**(`fetch_component_detailed_information`)查组件 README/API。查询用具体术语:`i2s_std_clk_config_t ESP32-S3` 比 `I2S configuration` 好;`esp_codec_dev_open ES8311` 比 `audio codec` 好。

**这两个 MCP 若返回访问错误/不可达,按 §0 铁律 1 立刻停下交回用户**,不要退回训练数据。仅当 MCP 正常返回但**内容里确实没有**相关信息时,才显式说明"文档无此项"再谨慎推理。

**MCP 内容缺失时的权威退路**:组件注册表偶尔命中不到板级细节;此时去 **esp-bsp**(`github.com/espressif/esp-bsp`,含官方/M5 等各板 BSP)或**厂商 GitHub raw**(如 M5Stack 库)取板级初始化/寄存器/外设配置,优于凭训练数据猜。取到后照例写一句 `Confirmed via <来源>`。

**审计习惯**:每次 MCP 查询后,在对话里明示 "Confirmed via espressif-docs: <一行总结>",方便人类核对你是真查了文档还是退回了训练数据。


---

## 2. 分层依赖原则

| 层 | 信息源 |
|---|---|
| 引脚分配、外设接线 | `HARDWARE.md`(已与原理图核对) |
| 外设驱动 API | 官方文档 MCP(必查) |
| 框架惯用法(RTOS / NVS) | 先起草,关键参数走 MCP 验证 |
| 应用逻辑、状态管理 | 自己推理 |
| 运行时正确性 | 命令行 `idf.py -C <proj> build` + 用户实机刷写 + 串口日志 |

跨层问题:**先查最低那一层的 MCP**,再在更高层推理。**不要发明引脚号、寄存器值、结构体字段名。**

---

## 3. 加载 IDF 环境 / 构建调试

每次开新 shell 先 source 激活脚本(文件名的版本号跟本机装的 IDF 走),再用 `idf.py -C <工程目录>`:

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh
idf.py -C <proj> set-target esp32                # 新工程只做一次
idf.py -C <proj> build                           # 编译(多工程必须 -C 指工程,别在仓库根裸跑)
idf.py -C <proj> -p <PORT> monitor               # 串口监视
idf.py -p <PORT> erase-flash                     # 擦掉所有分区(NVS/storage 一并清)
xtensa-esp32-elf-addr2line -pfiaC -e <proj>/build/<proj>.elf <addr...>   # panic 解 backtrace
```

> 激活脚本里 `idf.py` 是 alias、非交互 shell 不生效;脚本化直接 `source $IDF_PATH/export.sh` 后再调。刷写见项目 `CLAUDE.md`/`tools/flash_map.md`(WSL 无 USB 直通,由用户手动)。

**改 `sdkconfig.defaults` / 共享 `sdkconfig.platform` 后**:对应工程 `rm -f sdkconfig` → `idf.py -C <proj> fullclean` → build,否则旧值不会被覆盖。

构建警告里和**已弃用 API 相关的不要默默吞掉**,显式提出来问要不要迁移。

`ESP_ERROR_CHECK` 包住每个驱动 init 调用,不要为了"代码干净"省掉 —— 静默 init 失败是这里第一大耗时坑。

---

## 4. 通用工作循环

1. 编辑代码
2. `idf.py -C <proj> set-target esp32`(只在新工程做一次)
3. `idf.py -C <proj> build` 编译
4. 构建失败:读完整错误,**对错误里出现的不熟悉 API 先查 MCP 再改**
5. 刷写(用户实机,WSL 不能烧)→ `idf.py -C <proj> -p <PORT> monitor` 监控串口,等 boot log
6. crash:解 backtrace,**先定位根因再改代码**,不要"试一改"
7. 外设"不工作但不报错"时,用**开机自检隔离软硬件、一次烧录定位**:喂固定测试信号 / 逐项扫配置各跑一遍,看/听哪个对。例:喇叭没声 → 先放固定测试音(绕过业务逻辑)判断功放链路;再依次切几种 I2S 槽格式各放一声 → 听出对的那个。灯不亮 → 逐颗点亮定物理顺序。**有底噪无音调 = 功放通、数据格式错**这类"现象→结论"也要随手记进 `HARDWARE.md`。

---

## 5. 风格

- 4 空格缩进,无 tab
- C 标识符 snake_case,常量/宏 UPPER_SNAKE
- 每个外设独立 component(放 `components/`),应用逻辑住 `main/`,**不直接依赖裸驱动**
