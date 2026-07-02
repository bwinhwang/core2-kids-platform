# AGENTS.md — AI 协作通用规范(ESP-IDF 项目)

本文件是通用规范,可复制到任意 ESP-IDF v6 项目。**项目特定内容请勿写在这里**,放 `CLAUDE.md`。

---

## 0. MCP 前置约束(硬性铁律,先读)

本项目的 AI 协作依赖 **三个 Espressif MCP**,三者必须**都已配置且可访问**才能工作:

| MCP | 用途 | 当前环境工具名(以你的 MCP 配置为准) |
|---|---|---|
| **esp-idf stdio** | 编译 / 刷写 / 设 target / 清理 | `mcp__esp-idf__build_project`、`flash_project`、`set_target`、`clean_project` |
| **组件注册表** | 组件 README / API / changelog | `mcp__…Espressif_Registry__search_components`、`fetch_component_detailed_information` |
| **官方文档** | ESP-IDF 文档语义检索 | `mcp__…Espressif_Docs__search_espressif_sources` |

**铁律(不可绕过):**

1. **编译和刷写只能走 esp-idf stdio MCP** —— 用 `build_project` / `flash_project`,**禁止**用 `idf.py build` / `idf.py flash` 命令行代替。设 target、清理同理走 `set_target` / `clean_project`。命令行只用于 MCP 未覆盖的操作(见 §3)。
2. **组件注册表 或 官方文档 MCP 无法访问时,立刻停止、退出,交回用户接管** —— 不要凭训练数据猜着往下写驱动/寄存器/API。
3. **三个 MCP 有任一未配置或不可用,先提示用户去配置**,配好再开工;不要用命令行或训练数据变通。开工前若不确定,先确认这三个 MCP 都在。

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

**这两个 MCP 若返回访问错误/不可达,按 §0 铁律 2 立刻停下交回用户**,不要退回训练数据。仅当 MCP 正常返回但**内容里确实没有**相关信息时,才显式说明"文档无此项"再谨慎推理。

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
| 运行时正确性 | esp-idf MCP `build_project` + `flash_project` + 串口日志 |

跨层问题:**先查最低那一层的 MCP**,再在更高层推理。**不要发明引脚号、寄存器值、结构体字段名。**

---

## 3. 加载 IDF 环境 / 构建调试

**编译、刷写、设 target、清理走 esp-idf stdio MCP(§0 铁律 1),不用 `idf.py` 命令行:**

| 操作 | 用 MCP 工具 |
|---|---|
| 编译 | `mcp__esp-idf__build_project` |
| 刷写 | `mcp__esp-idf__flash_project`(需指定串口) |
| 设目标芯片 | `mcp__esp-idf__set_target`(只在新项目做一次) |
| 清理 build/ | `mcp__esp-idf__clean_project` |

**命令行仅用于 MCP 未覆盖的操作**(串口监视、擦 flash、解 backtrace 等),且每次开新 shell 先 source 激活脚本:

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh
idf.py -p <PORT> monitor          # 串口监视(MCP 不覆盖)
idf.py -p <PORT> erase-flash      # 擦掉所有分区(NVS/storage 一并清)
xtensa-esp32-elf-addr2line -pfiaC -e build/<proj>.elf <addr...>   # panic 解 backtrace
```

**改 `sdkconfig.defaults` 后**:先 `rm -f sdkconfig`(文件操作),再用 `clean_project` + `build_project`,否则旧值不会被覆盖。

构建警告里和**已弃用 API 相关的不要默默吞掉**,显式提出来问要不要迁移。

`ESP_ERROR_CHECK` 包住每个驱动 init 调用,不要为了"代码干净"省掉 —— 静默 init 失败是这里第一大耗时坑。

---

## 4. 通用工作循环

1. 编辑代码
2. `set_target` MCP → 目标芯片(只在新项目做一次)
3. `build_project` MCP 编译
4. 构建失败:读完整错误,**对错误里出现的不熟悉 API 先查 MCP 再改**
5. `flash_project` MCP 刷写(指定串口)
6. 命令行 `idf.py -p <PORT> monitor` 监控串口,等 boot log
7. crash:解 backtrace,**先定位根因再改代码**,不要"试一改"
8. 外设"不工作但不报错"时,用**开机自检隔离软硬件、一次烧录定位**:喂固定测试信号 / 逐项扫配置各跑一遍,看/听哪个对。例:喇叭没声 → 先放固定测试音(绕过业务逻辑)判断功放链路;再依次切几种 I2S 槽格式各放一声 → 听出对的那个。灯不亮 → 逐颗点亮定物理顺序。**有底噪无音调 = 功放通、数据格式错**这类"现象→结论"也要随手记进 `HARDWARE.md`。

---

## 5. 风格

- 4 空格缩进,无 tab
- C 标识符 snake_case,常量/宏 UPPER_SNAKE
- 每个外设独立 component(放 `components/`),应用逻辑住 `main/`,**不直接依赖裸驱动**
