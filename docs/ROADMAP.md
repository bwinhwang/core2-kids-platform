# 路线图 — 平台转向:幼儿游戏掌机 → IoT 评估平台

> 2026-07-17 用户拍板转向。硬件平台不变(Core2 v1.0 + M5GO Bottom2 + 现有 Grove/Chain 单元),
> 受众从幼儿改为 **IoT 爱好者/评估者**,平台定位改为 **IoT 评估平台**。本文回答三件事:
> ①为什么转向(决策记录);②三个 Phase 分别做什么;③历史(幼儿掌机时期的路线图存档)。

---

## 0. 转向决策(2026-07-17,用户拍板)

三个已确认决策:

1. **游戏卡带全部移除**(git 历史留档,沿用 2026-07-17 槽位清洗先例)。tilt_maze / busy_knobs /
   chick_pour / chain_lab / fish_pond / pipe_garden 六个 app 目录整体删除;分区表/存储布局沿用
   不改,首批 IoT 应用占 ota_0/ota_1 两个槽。
2. **暂不联网**:不引入 Wi-Fi/BLE/ESP-NOW/MQTT,先做本地评估台。网络阶段见下方 Phase 3(占位)。
3. **评估重心**:(a) 外设/单元评估台(`unit_bench`);(b) 功耗/系统评估(`power_lab`,利用
   AXP192 可读电压电流)。

摸底结论:硬件 bring-up(`core2_board`)、8 个单元驱动、AXP192 直控(`core2_power`)、卡带机制
(`app_slot`)、截图自查(`screenshot` + `tools/screenshot.py`)全部直接复用;全仓零网络代码、
零 NVS 使用(不用清理);缺口 = NVS 封装、数值/文字 UI 控件层、AXP192 遥测组件、数据导出约定。
**受众识字是最大架构红利**:UI 可用文字,launcher 可数据驱动渲染工程名,彻底消灭"加 app 必须
重刷 launcher 加图标分支"的摩擦。

顺带修的已知 bug:`tools/new_app.sh` 模板引用已删除的 `app_slot_enable_button_exit()`,脚手架
build 必然链接失败——已在 Phase 0 修复。

### 新设计第一性原则(已回写 `CLAUDE.md` §2)

1. **可观测优先**:被评估对象的状态屏上实时可见、串口可拿原始数据。
2. **错误显式呈现**:init 失败/总线卡死/拔线一律屏上红字+错误码,绝不静默(与幼儿"零失败"反向)。
3. **数据可导出**:数值带时间戳导出(串口 CSV 为主,SPIFFS 录制为离线场景)。
4. **热插拔容错**(继承):2s 重试 + 连续失败判拔线的既有形态。
5. **渲染红线**(继承,硬件约束与受众无关):永不每帧整屏重绘;chart/数值卡走脏矩形。
6. **省电纪律**(继承但可被接管):默认 core2_sleep;评估 app 可禁 DEEP/接管 5V/手动驱动休眠阶段。

---

## 1. Phase 0 —— 清理 + 文档 + 脚手架(纯 WSL 可完成)

**删除**(git 留档):`apps/{tilt_maze,busy_knobs,chick_pour,chain_lab,fish_pond,pipe_garden}/`、
`tools/verify_mazes.py`(tilt_maze 专用)。

**重写**:`CLAUDE.md`(§1 定位、§2 原则、§8 幼儿安全→UI 可读性纪律、§10 新 app 指南保留技术红线
删幼儿玩法、§12 索引清空;保留不动:§3 硬件、§6 渲染纪律 + 追加数值/波形 UI 脏矩形小节、§7 电源、
§9 分区纪律、§10.1 截图自查、§11 MCP 清单追加 AXP192 ADC/库仑计 + esp_pm/DFS 条目)、本文件、
`README.md`、`launcher/README.md` 定位段、`tools/flash_map.md` + `tools/flash_one.sh` 槽表(
ota_0=unit_bench、ota_1=power_lab、ota_2~5 预留)、`partitions.csv`(只改头部注释,字段不动)。

**脚手架 + 改名**:`tools/new_app.sh` 删 `app_slot_enable_button_exit()` 调用、模板换 IoT 评估台
骨架;`components/core2_board/include/core2_board.h` 的 `CORE2_BOARD_CFG_KIDS_DEFAULT` →
`CORE2_BOARD_CFG_DEFAULT`(引用点仅 launcher + 模板,一次改完,不留别名)。

**完成标准**:`idf.py -C launcher build` 通过;`tools/new_app.sh smoke_test` + build 通过后删除;
`grep -r "tilt_maze\|busy_knobs\|chick_pour\|chain_lab\|fish_pond\|pipe_garden\|KIDS_DEFAULT"`
无残留(历史留档段/技术脚注类引用除外,详见下方"遗留引用处理")。

**遗留引用处理**:部分仍保留的组件文件里含旧 app 名作**历史脚注**(如 `chain_bus.h` 提到
chain_lab 是"上板验证手段"、`ledstrip_fx.h` 提到 busy_knobs 图案彩蛋是某个 LED 效果的来源、
`unit_chain_joystick/README.md` 和 `docs/units/Chain_Joystick.md` 提到 chain_lab 的摇杆归中做法)——
这些注释描述的**工程结论本身依然成立**(摇杆归中做法、Chain host 验证结论等),只是署名的 app
已下线,不删除技术内容,原样保留(不影响编译/理解)。`docs/platform/BSP_GUIDE.md` 里作为范例
提到的 `apps/tilt_maze` 已在 Phase 0 改为泛化措辞。

---

## 2. Phase 1 —— 平台组件补课(后端先行,launcher 收尾)

1. `sdkconfig.platform` 加 `CONFIG_LV_FONT_MONTSERRAT_16` + `_24`(ASCII 字号)→ 各工程
   `rm -f sdkconfig && fullclean`。
2. `components/core2_power` 加低层原语 `core2_power_read_regs(reg, buf, len)`(复用已有 AXP192
   句柄,避免二次 add_device)。
3. **新组件 ×4 + 小扩展 ×3**(每个带 README,实现前按 AGENTS.md 查 MCP):
   - `components/power_monitor`:`power_monitor_read(power_telemetry_t*)` 一次读全量(batt mV/
     充放 mA、VBUS mV/mA、充电状态、库仑计)+ `coulomb_reset()`。不做后台采样/历史缓冲(归 app)。
     AXP192 ADC/库仑计寄存器实现前查 datasheet 核实,查不到就砍并在 README 记「待查证」。
   - `components/kv_store`:NVS 封装(init 含 NO_FREE_PAGES 擦除重试、每 app 一个 namespace):
     `get/set_i32、get/set_f32(blob)、get/set_str、erase_ns`。不做 schema 迁移。
   - `components/ui_kit`:4 个控件,全守渲染红线 —— `ui_status_bar`(顶 24px:app 名/uptime/电池,
     1Hz 只脏自己)、`ui_value_card`(标签+大数值+单位,阈值变色)、`ui_chart`(lv_chart 封装,
     环形推点)、`ui_list_menu`(可点击行,行内可挂 lv_switch)。v1 全 ASCII,CJK 不进(见风险 1)。
   - `components/data_log`(v1 串口 CSV):`begin(name, cols)` 打 `#CSV-BEGIN` 哨兵 →
     `row(fmt,…)` 自动时间戳 → `end()`;主机侧扩展 `tools/serial_capture.py` 按哨兵提取 .csv。
     SPIFFS 录制(rec_start/stop/dump)推迟到 Phase 2 P4。
   - `components/units/unit_probe`:PORT.A 已知地址表(0x23 DLight/0x41 8Encoder/0x57
     Ultrasonic/0x73 Gesture)+ 全总线扫描标注"已知/未知"。
   - `components/app_slot`:加 `app_slot_info(idx, out)` 取 project_name+version+date(launcher
     数据驱动用)。
   - `components/core2_sleep`:加 `core2_sleep_force_stage(s, stage)`(复用组件内固化的亮度→
     DCDC3→灯带→5V 顺序,power_lab 手动进 NAP/DEEP 用,不许 app 散装重拼)。
4. **launcher 重写**(`launcher/main/app_main.c`,它是新组件的第一个集成测试场):删
   mascot/7 个图标函数/strcmp 分支/马卡龙色表;槽卡片数据驱动渲染(工程名+版本/编译日期+
   `ota_N` 角标);空槽显示 `empty / ota_N`;顶部 `ui_status_bar`(电池/USB);深灰工程风配色。
   保留 3×2 网格、`app_slot_present` 自动发现、点击进槽、久置省电、崩溃回 factory。

**完成标准**:全组件+launcher build 通过 → 用户 Windows 侧**全量刷 launcher 一次**(改 factory
的一次性成本,此后加 app 永不再刷)→ `tools/screenshot.py` 截图验收:状态栏电压合理、空槽显示
正确;插/拔 USB 电压变化说得通。

---

## 3. Phase 2 —— 两张卡带(`tools/new_app.sh` 起工程,登记槽表)

### unit_bench(ota_0,外设/单元评估台)
- **范围**:PORT.A I2C 全扫描+已知单元识别(未知地址 hex 列出);Chain 链枚举(设备类型+固件
  版本);每单元详情视图(数值卡+chart 趋势,2~5Hz 推点);热插拔(复用既有形态)+ I2C 卡死
  自愈**屏上显示事件**;超声波零点标定(±步进,存 kv_store);详情页 Log 按钮启停 CSV 流。
- **布局**:顶部状态栏 24px / 底部按钮条 32px(Back/Rescan/Log/Cal);列表页中间
  `ui_list_menu`;详情页左列 2~3 数值卡 + 右侧 ~180×150 chart。
- **省电**:`manage_bus_5v=false` 且禁 DEEP(深睡切 5V 会杀被测单元),仅留 NAP 降亮。
- **里程碑**:B1 扫描+列表(验收:插/拔 DLight 截图)→ B2 三个纯 I2C 传感器详情+chart+热插拔
  → B3 8Encoder(分笔读纪律)+ Chain 枚举与 Enc/Joy 视图 → B4 CSV 导出 + 标定持久化(重启偏置
  仍在)。

### power_lab(ota_1,功耗/系统评估台)
- **范围**:遥测面板(1Hz);负载开关矩阵看电流跳变(背光 0/10/60/100%、EXTEN 5V、灯带
  0/48/255、喇叭测试音、震动、CPU 锁频——esp_pm 实现前必查 MCP,不顺就降级两档或砍掉);休眠
  演练(`force_stage` 触发 NAP/DEEP,深睡电流存内存,唤醒后屏上回放均值/时长);续航估算(放电
  电流+库仑计外推 500mAh);串口 CSV + SPIFFS 离线录制。
- **布局**:页 1 左列开关矩阵 + 右侧大电流数值卡(Montserrat 24)+ 电流 chart;页 2 休眠触发/
  回放卡/续航卡/录制启停。
- **里程碑**:P1 只读遥测(验收:插/拔 USB 两张截图,拔线后放电电流 >0)→ P2 负载矩阵+chart
  (开灯带前后台阶清晰)→ P3 休眠演练+续航估算 → P4 SPIFFS 离线录制+回连 dump(拔 USB 录 10
  分钟放电曲线)。

每个里程碑闭环:WSL build → 打印 `tools/flash_one.sh` 单刷命令给用户 Windows 执行 →
screenshot.py 截图 + serial_capture.py 抓 CSV 验收。收尾回填 `CLAUDE.md` §12 竣工索引。

---

## 4. Phase 3(占位不展开)

网络阶段:Wi-Fi/ESP-NOW/MQTT 评估;届时再评估网络栈对各槽 2MB 的体积冲击与 lwIP 缓冲进 PSRAM。

---

## 5. 风险与决策点

1. **CJK 字体**:v1 全 ASCII(全量中文字库 0.7–1.2MB 塞不进 factory 1.5MB);确需中文时用
   lv_font_conv 按需子集,Phase 2 后显式决策。
2. **NVS vs SPIFFS**:各司其职——设置/标定走 NVS(16KB),波形/放电曲线走 SPIFFS storage
   (2.4MB,首次 mount 格式化数秒需屏上提示)。
3. **AXP192 电流测量的坑**(写进 power_lab 屏上提示):只测电池路和 VBUS 路,无 per-rail;
   USB 插着时电池电流≈0 → 看 VBUS 电流;真实续航必须拔 USB → 没串口 → SPIFFS 录制是唯一手段
   (P4 存在理由);几 mA 小负载(震动)可能淹在 ADC 噪声里,chart 加滑动平均。
4. **launcher 重刷时机**:Phase 1 一次性全量刷 factory,向用户明示;之后永不再为加 app 重刷。
5. **烧录现实**:WSL 只 build,所有烧录命令打印出来由用户 Windows 侧手动执行(既有工作流)。

## 6. 关键文件

- `CLAUDE.md`、本文件(文档主战场)
- `launcher/main/app_main.c`(数据驱动重写)
- `tools/new_app.sh`(修缺符号 bug + 换模板)、`tools/flash_one.sh`、`tools/flash_map.md`
- `components/core2_power/`(read_regs 原语)、`components/core2_board/include/core2_board.h`
  (宏改名)
- 新增:`components/{power_monitor,kv_store,ui_kit,data_log}/`、`components/units/unit_probe/`
- 新工程:`apps/unit_bench/`、`apps/power_lab/`(含各自 SPEC.md/README.md,沿用仓库惯例)

## 7. 验证方式

- **Phase 0**:launcher + smoke_test 脚手架工程均 build 通过;grep 无游戏残留。
- **Phase 1**:全组件 build;用户全量刷 launcher 后 screenshot.py 截图验收状态栏/空槽卡片。
- **Phase 2**:每里程碑单刷 → 截图(视觉项)+ serial_capture.py 抓 CSV(数据项)+ 真人操作
  (热插拔/休眠这类截图看不见的);标定持久化用"重启后偏置仍在"验证。

---

## 附录:幼儿掌机时期路线图存档(2026-07-12 定稿,已被本次转向取代)

> 以下内容是转向前的最后一版路线图(Phase 2,基于幼儿实机偏好反馈),**历史存档,不再指导
> 当前开发**,保留供查阅当时的选型分析方法论(实体感/连续比例映射/技能成长/收集元循环四条
> 归因)与槽位清洗决策脉络。

### 反馈解读:三强赢在哪、三冷输在哪(2026-07-12)

六张卡带按输入方式和玩法结构对照:tilt_maze / busy_knobs / chain_lab 明显更受幼儿喜爱、反复玩;
peekaboo / feed_monster / magic_wand 只是试一下就放下。归因四条(按重要度排序):①实体感是第一
位的(三强都有真实的力/位移反馈,三冷全是"对空气做动作");②连续比例映射,信息维度要够;
③要有随技能成长的真实决策;④收集/庆祝/彩蛋元循环放大重复游玩。

由此确立的选型铁律(幼儿玩法专属,IoT 评估台阶段不适用):新卡带的主输入必须同时满足可抓握+
连续比例映射+决策空间随技能成长;隔空/环境类传感器不再作为主输入立项。

### 2026-07-17 增补:大对象护眼约束落地 + 槽位清洗

出于幼儿视力保护,主角/可交互目标/状态图标最小边 ≥ ~64px;数学后果:同屏合规对象 ≤5~6 个、
平滑运动 ≤2 个 → "多而小"的群体精灵玩法对新卡带永久出局。**槽位清洗**:peekaboo / feed_monster
/ magic_wand(三冷)、busy_bus(巴士~28px)、slingshot_feed(果子16×16)因主角尺寸违新约束或
未实机验证、沉没成本最低时点删除;fish_pond 按新约束重设计为"少而大"形态立项 ota_5。

在役保留(转向前):tilt_maze / busy_knobs / chick_pour / chain_lab。这一状态在 **2026-07-17
平台转向**中被整体取代——见本文件 §0~§4。完整的历史分析、候选卡带评审(pipe_garden /
slingshot_feed 等)、marble_machine 否决记录等均可在 git 历史中查阅本文件转向前的版本
(`git log -- docs/ROADMAP.md`)。
