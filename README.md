# Core2 + Bottom2 幼儿应用平台模板

面向「M5Stack Core2 v1.0 + M5GO Bottom2 + 各类 UNIT 外设」的幼儿应用快速起步模板。
三层结构:**固定平台 BSP** + **可插拔 UNIT 框架** + **幼儿交互/安全骨架**。

clone 下来即带一个可跑 demo:**摇 Core2 → 灯随强度变化 + 摇一下响一声 + 屏背景加深**。

## 快速开始
```bash
# 1) 激活 IDF 环境(每开新 shell)
source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh
# 2) 改工程名(可选)
tools/new_app.sh my_app
# 3) 构建 / 烧录
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

## 目录
| 路径 | 作用 |
|---|---|
| `components/bsp_core2/` | 平台 BSP(电源/IMU/灯/音频/屏/触摸),开箱即用,可 Kconfig 裁剪 |
| `components/app_core/` | 交互骨架:共享状态、状态机、默认 engine、调参、`kids_safety` |
| `components/units/` | 可插拔 UNIT 外设(含 `unit_template`) |
| `components/common/` | NVS 初始化、I2C 扫描自检 |
| `docs/platform/` | 平台固定硬件真值(Core2 / Bottom2),勿改 |
| `docs/units/` | 各 UNIT 硬件真值(含 `_UNIT_TEMPLATE.md`) |
| `AGENTS.md` / `CLAUDE.md` | 通用 AI 协作规范 / 本应用特定规范 |

## 加一个 UNIT 外设(超声波/手势/光线…)
```bash
tools/add_unit.sh ultrasonic A       # 复制 unit_template + 生成文档雏形(接 PORT.A)
```
然后:填 `docs/units/ultrasonic.md`(地址/寄存器/坑)、在 `main/CMakeLists.txt` 的
`REQUIRES` 加 `unit_ultrasonic`、用 `bsp_i2c_port_a()` 取总线传入 init。
接上硬件后先 `i2c_scan(bsp_i2c_port_a())` 确认地址。

## 裁剪板载能力
`idf.py menuconfig` →「Core2 BSP」勾选灯/声/屏/触摸。不用屏的应用关掉可省 flash/RAM。

## 换成你自己的应用
1. 改 `engine.c`(若输入不是 IMU 而是某 UNIT)
2. 改 `main.c` 的各输出 task(灯/声/屏怎么响应)
3. 调 `app_tuning.h`(行为)与 `kids_safety.h`(红线)
4. 写 `CLAUDE.md`(产品目标)与 `docs/HARDWARE.md`(装配)

## 可选:CI / 单元测试(模板未预置,按需加)
- 单元测试:新建 `test_apps/`,用 pytest-embedded + Unity(host 或 qemu)。
- CI:`.github/workflows/build.yml` 跑 `idf.py build`(可矩阵 esp32)。
