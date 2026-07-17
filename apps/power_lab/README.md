# power_lab — 功耗/系统评估台(as-built)

> 本文件是 **power_lab 应用的竣工记录**;功能规格见 `SPEC.md`。跨应用平台事实(EXTEN/
> DCDC3/桌面省电坑)见根 `CLAUDE.md` §7/§10/§11。占 **ota_5(0xB90000)**。
> 烧录:`tools/flash_one.sh power_lab`(= esptool write-flash 0xB90000)。

产出:从 `tools/new_app.sh` 生成的空骨架,实现到 build 通过的完整评估台——只读遥测面板
(1Hz)、负载开关矩阵(背光/灯带/5V/喇叭/震动/CPU 锁频)、休眠演练(NAP/DEEP 手动触发 +
采样回放)、续航估算、SPIFFS 离线录制 + 回连 dump。

**本轮完全在 WSL 环境下开发,没有实机设备可烧录验证**——所有"实机行为"的判断只到"代码
逻辑自洽 + build 通过"为止,详见文末「待实机点检」,如实标注、不假装已验证。

## 代码结构

```
apps/power_lab/main/
  app_main.c          应用入口:平台 bring-up 顺序 + core2_sleep 初始化(仅供演练用)+
                       10Hz 固定节拍主循环(ctl_tick 先于 ui_tick,见下方顺序保证)
  power_lab_ctl.h/.c  model 层:遥测轮询 + 负载矩阵状态 + 休眠演练状态机(含 SPIFFS/
                       演练两处"请求位 + tick 里执行"两段式解耦)+ 续航估算,不碰 LVGL
  power_lab_ui.h/.c   view 层:两页 LVGL 画面(page1 遥测+矩阵,page2 演练+续航+录制)+
                       翻页,只对外暴露 power_lab_ui_start() / power_lab_ui_tick()
```

三层拆分(app_main 编排 / ctl 做状态机 / ui 做 LVGL)对应 `apps/unit_bench` 的既有拆分
惯例(见其 README「代码结构」),同一职责边界。

## 关键设计取舍

### 1. power_lab 不跑 core2_sleep 的自动闲置省电(CLAUDE.md §7 明确例外)

`app_main.c` 的主循环**不调用 `core2_sleep_feed()`**——`core2_sleep_t` 只初始化一次,
之后全程只经 `core2_sleep_force_stage()` 在"演练"场景下手动驱动。理由:自动闲置省电会
在用户操作负载矩阵观察电流时(比如刚把背光调到 100% 想看电流跳多少)于 12s 后自动把背光
降回打盹亮度,把矩阵记录的挡位和实际硬件状态弄脱节,和这个 app 的核心用途(手动控制负载、
观察确定性的电流台阶)直接冲突。CLAUDE.md §7 原文已明确点名 power_lab 是这条纪律的例外
("power_lab 例外——它本身就是评估功耗的工具,会主动接管/演练休眠阶段")。

演练结束(`power_lab_ctl.c` 的 `drill_tick`)后,负载矩阵的挡位会被重新贴回
(`apply_backlight`/`apply_led`/`core2_power_bus_5v`),因为 `core2_sleep` 的唤醒顺序会
把亮度/灯带恢复到它自己的"清醒"默认值,与矩阵此刻记的挡位不一定一致。

### 2. CPU 锁频查证结论:两档 DFS 频率切换,不开自动轻睡眠

查证顺序:先用 `mcp__claude_ai_Espressif_Docs__search_espressif_sources` 查
`esp_pm_configure`/`esp_pm_config_t` 签名与官方 power_management 文档,再用本机
`$IDF_PATH`(esp-idf v6.0)读 `components/esp_pm/include/esp_pm.h` 与
`components/esp_pm/Kconfig` 源码交叉核对——两处结论一致(Confirmed via espressif-docs +
本地 esp-idf v6.0 源码,2026-07-17)。

结论与实现:
- `esp_pm_config_t { max_freq_mhz, min_freq_mhz, light_sleep_enable }`,`esp_pm_configure()`
  在 `CONFIG_PM_ENABLE` 未开时返回 `ESP_ERR_NOT_SUPPORTED`。
- `CONFIG_PM_ENABLE` 的可选条件是 `!FREERTOS_SMP && SOC_PM_SUPPORTED`——本仓
  `FREERTOS_SMP` 默认关闭、esp32 经典款 `SOC_PM_SUPPORTED=y`,可选;已加进
  `apps/power_lab/sdkconfig.defaults`(只本工程开,不进共享 `sdkconfig.platform`,因为
  该选项有运行时开销——中断延迟增加,其它评估 app/游戏不需要为它多付代价)。
- `CONFIG_PM_DFS_INIT_AUTO` 默认关闭,意味着单开 `CONFIG_PM_ENABLE` **不会**自动改变
  开机后的运行状态,CPU 频率完全由本 app 主动调 `esp_pm_configure()` 决定。
- 本 app 的两档矩阵项——"240MHz 恒定"(`max=min=240`)vs"80~240MHz 自动调频"
  (`max=240,min=80`)——**都不开 `light_sleep_enable`**。这是刻意的降级/简化:
  1. 官方文档明确指出 `light_sleep_enable=true` 依赖
     `CONFIG_FREERTOS_USE_TICKLESS_IDLE`(未开时 `esp_pm_configure` 直接返回
     `ESP_ERR_NOT_SUPPORTED`),要开这条链路还得再决策一整套 tickless idle 配置,
     与本 app 已有的、自制的 `core2_sleep` 状态机在"什么时候算清醒/该不该休眠"这件事上
     存在语义重叠和潜在冲突(两套省电逻辑同时活跃,行为会很难预测)。
  2. 经典 ESP32 + PSRAM 组合下,light sleep 期间 PSRAM CS 引脚有漏电流问题,官方要求
     `CONFIG_ESP_SLEEP_PSRAM_LEAKAGE_WORKAROUND`(默认 `y` if `SPIRAM`)兜底,虽然默认
     已开、理论上不需要额外动作,但这进一步说明 light sleep 这条路径涉及的配置面比单纯
     "调频"大得多,不是"两档开关"这种简单形态应该承担的复杂度。
  3. 单纯的 DFS 频率切换(不开 light sleep)有官方专门的测试用例佐证是稳定组合
     (`components/esp_hw_support/test_apps/mspi_psram_with_dfs`),且历史上"PSRAM +
     DFS 频率切换崩溃"的已知 bug(`v5.0.4` changelog 记录的 MSPI timing 问题)已在
     v5.0.4 修复,本仓固定用 v6.0,不受影响。
  
  按任务原始要求的降级判据("经典 ESP32 + PSRAM 组合有已知的 DFS 限制、或者
  `esp_pm_configure` 与本仓已有的 `core2_sleep` 自制省电状态机有交互风险说不清楚,按计划
  直接降级成两档开关"),这里选择的正是"两档开关"这个降级形态——完整功能是"锁频 + 可选
  自动轻睡眠",这里只做前半段,该功能没有被整项砍掉,是从"三档/更复杂配置面"收窄到"两档
  纯调频"这一个安全子集。

- `pl_ctl_init()` 用一次 `esp_pm_configure({240,240,false})` 顺带探测可用性
  (`cpu_pm_available`)——若返回非 `ESP_OK`(理论上不会,因为 sdkconfig 已开
  `CONFIG_PM_ENABLE`,但保留这条防御性探测,应对未来有人误删该 Kconfig 项的情况),CPU
  锁频那一行在 UI 上显式禁用(文字变"CPU锁频:不可用",点击无效果),不静默失败。

### 3. 背光/灯带矩阵:循环点击而非 `lv_switch`,原因是 `ui_kit` 没有"设初始状态"的 API

`ui_kit` 的 `ui_list_menu_add_row(..., with_switch=true)` 只提供 `get_switch` 读状态,
**没有** `set_switch` 写状态的 API(读了 `components/ui_kit/ui_list_menu.c` 源码确认)。
本 app 需要在开机时就让矩阵显示的挡位和硬件实际状态一致(比如 EXTEN 基线是 OFF),如果背光
(4 档)/灯带(3 档)也用 `lv_switch`,switch 部件天生只有开/关两态,表达不了三/四档,
所以背光和灯带矩阵行本轮统一设计成"点击整行循环下一档"(与 `unit_bench` 的列表行点击范式
一致),配合 `power_lab_ctl.c` 里的挡位数组单一真源(`s_backlight_levels`/`s_led_levels`)+
只读访问器(`pl_ctl_backlight_pct`/`pl_ctl_led_level`)保证 UI 文字和实际挡位不脱节。

**EXTEN 5V 是唯一用 `with_switch=true` 的行**——它天生就是二态(开/关),而且本 app 的
矩阵基线刻意把 EXTEN 设成 OFF(见下条),与 `lv_switch` 创建后的默认未勾选状态天然吻合,
不存在"没有 set_switch API 就没法同步初始视觉状态"的问题。

### 4. 负载矩阵基线:全部收敛到已知起点,而不是沿用 `core2_board_init` 的默认值

`core2_board_init(CORE2_BOARD_CFG_DEFAULT)` 会把背光设到 70%、EXTEN 打开(因为
`enable_leds=true`)、灯带亮度设到 80。这些默认值本身对"游戏/评估台开机能看见东西"是对的,
但不落在本 app 矩阵定义的挡位表内(70% 不是 {0,10,60,100} 之一),而且 EXTEN 默认开着会
让"演示 5V 开关前后的电流台阶"这个核心场景在开机第一眼就已经处于"开"的状态,不直观。

`pl_ctl_init()` 因此在 `core2_board_init` 成功后,显式把背光/灯带/EXTEN/CPU 频率**重新
设一遍**到矩阵定义的挡位(背光 60%、灯带 0、EXTEN OFF、CPU 240MHz 定频),矩阵挡位记录
(`backlight_idx=2` 等)与硬件实际状态从第一帧起就完全一致。副作用:EXTEN 基线 OFF 意味着
开机瞬间灯带是暗的(尽管 `core2_board_init` 内部为了驱动初始化短暂开过一次 5V)——这正好
顺带演示了 CLAUDE.md §7/§11.2 提到的那个平台坑("灯带 refresh 返回 OK 但不亮,因为 EXTEN
没开"),不是 bug。

### 5. 休眠演练的"两段式请求":避免 LVGL 提示文字来不及画出就被断电盖过去

`power_lab_ctl.h` 的 `drill_pending` 字段 + `pl_ctl_request_drill()`:UI 点击"演练
NAP/DEEP"按钮时,只做两件快事——① 立即把提示文字("演练:NAP 中…"/"演练:DEEP 中(屏将
熄灭)…")画到 `s_drill_status_lbl` 上,② 设 `drill_pending`。真正调用
`core2_sleep_force_stage()`(会立刻降背光甚至断电)推迟到**下一轮** `pl_ctl_tick()`
(在 `app_main.c` 的主循环里,与 LVGL 渲染任务是不同的 FreeRTOS 任务)才执行。

原因:LVGL 事件回调本身跑在 LVGL 任务上下文里,如果在回调里直接调用
`core2_sleep_force_stage()`(会立即执行断电动作),LVGL 自己的渲染循环根本没有被调度的
机会去把刚才 `lv_label_set_text` 改的文字实际刷到 SPI 屏上——用户会看到屏幕直接变暗/变黑,
完全看不到过渡提示。两段式请求让"改文字"和"断电"分属两个不同任务的两次独立调度,中间那
~100ms(主循环节拍)通常足够 LVGL 自己的渲染任务(`esp_lvgl_port` 内部计时器,典型周期远
小于 100ms)至少跑一轮、把文字刷出来。**这不是数学上严格保证的(极端调度抖动下仍可能错过),
只是一个实践上大概率够用的缓解手段**,记入下方「待实机点检」。

`app_main.c` 主循环里 `pl_ctl_tick()` 必须先于 `power_lab_ui_tick()` 调用——这个顺序
保证同一轮循环内,如果 `drill_pending` 被应用(`drill_stage` 从 IDLE 变为
NAP/DEEP),`power_lab_ui_tick()` 马上就能看到新状态并在函数最开头直接跳过(见下条),
不会出现"pending 已设、stage 还没变"的中间态被 UI tick 观察到、误刷新其它内容的情况。

`data_log_rec_start()` 的 SPIFFS 挂载/格式化走同一个"请求位 + tick 里执行"模式(见
`components/data_log/README.md`「设计取舍」),同一套思路复用了两次。

### 6. 休眠演练期间 UI 完全不碰 LVGL(而不是"碰了也没事,只是没必要")

`power_lab_ui_tick()` 第一行就是 `if (ctl->drill_stage != PL_DRILL_IDLE) return;`——
不只是"深睡时屏是黑的,更新了也看不见"这种效率考虑,而是严格照抄任务给的实现约束("这段
时间不能碰 LVGL")。技术上讲,LVGL 任务本身在 DEEP 期间并未被 `core2_sleep` 暂停(只是
背光断电、5V 切断),继续调用 `lv_label_set_text` 之类的 API 理论上不会崩溃,但保守起见
仍然完全跳过,避免任何"背光断电瞬间 LVGL 部件树状态未定义"一类没有实机验证过的假设。

### 7. 电流 chart 的数据源在 VBUS/电池净电流间自动切换

`power_monitor` README 已记录的平台级坑:USB 插着时电池电流≈0,要看 VBUS 电流才有意义。
`power_lab_ctl.c` 的 `telemetry_tick()` 因此按 `vbus_present` 动态选择 chart 的数据源
(`chart_from_vbus` 标志位,UI 侧的 `trend(vbus mA)`/`trend(batt mA)` 小标签同步提示当前
看的是哪一路),而不是固定画电池电流——固定画电池电流的话,插着 USB 做负载矩阵实验时
chart 会一直趴在 0 附近,看不出任何台阶,失去这张图存在的意义。chart 的 Y 轴范围
(`-500..2000` mA)是覆盖两种数据源量级的粗略估计,**没有实机数据校准过**,记入待点检。

### 8. 续航估算显式区分"不能算"和"算出来是 0"

`pl_ctl_endurance_hours()` 在充电中或放电电流 ≤1mA 时返回负数(哨兵值),UI 侧据此显示
"充电中"/"N/A" 而不是一个具体但没有意义的数字(比如放电电流恰好读到 0 时,`500/0` 会是
除零错误;读到 1mA 时会算出 500 小时这种明显不现实的巨大数字,容易被误当作"真实估算结果"
而不是"数据不足"的信号)。

### 9. SPIFFS 离线录制:新增到 `components/data_log`(唯一被允许改动的共享组件)

详细设计见 `components/data_log/README.md`「v2:SPIFFS 离线录制」一节,这里只记与
power_lab 相关的落地点:
- `power_lab_ctl.c` 的 `rec_tick()` 是"两段式请求"模式的具体调用点——`rec_pending_start`/
  `rec_pending_dump` 两个标志位在 `pl_ctl_tick()` 里被消费,阻塞的 SPIFFS 操作永远发生在
  `app_main.c` 主循环任务上下文,不持 `bsp_display_lock`。
- 录制的列:`batt_mv,batt_charge_ma,batt_discharge_ma,vbus_present,vbus_mv,vbus_ma,charging`
  ——比屏上展示的更完整(和 unit_bench CSV 导出"记录比屏上展示更完整"的既有惯例一致)。
- **已验证不破坏 v1 API**:改完 `components/data_log` 后重新 build 了
  `apps/unit_bench`(它只用 v1 `data_log_begin/row/end`,完全没碰 v2 API),build 通过、
  产物体积与改动前一致(`0xb1680` 字节),证明这次新增是纯增量,没有动到 v1 函数的实现。

## 已知限制

- **电流 chart 的 Y 轴范围(-500..2000mA)未经实机校准**,只是覆盖两种数据源量级的粗略
  估计,量程是否合适需要实机点检后调整。
- **休眠演练"提示文字来得及画出"的两段式解耦不是数学上严格保证的**,只是实践上大概率够用
  的缓解手段(见「设计取舍 5」),极端调度抖动下仍可能"演练中…"字样一闪而过没被看清就断电,
  待实机点检确认体验是否可接受。
- **CPU 锁频矩阵项不含自动轻睡眠**(只做 DFS 频率切换),是本轮刻意收窄的范围,不是缺陷,
  见「设计取舍 2」。
- **背光 0% 挡后触屏仍可用、但屏幕本身全黑**:盲点矩阵行原来的按钮位置仍能翻到下一档,是
  已知交互特性而非 bug(FT6336U 触摸走独立 I2C,不受 DCDC3 背光断电影响)。
- **续航估算是简单线性外推**,不考虑电池实际的放电曲线非线性(如 LiPo 低电量区间电压骤降、
  电流可能随温度/负载变化),只给量级参考,不是精确续航预测。
- **`core2_sleep` 唤醒后矩阵挡位需要 `power_lab_ctl.c` 主动重新贴回**(见「设计取舍
  1」),如果 `core2_sleep` 组件未来改了唤醒时的默认亮度/灯带逻辑,这里的"重新贴回"代码
  需要同步检查是否还对。
- **本 app 不接 PORT.A 外接单元**,深度演练/矩阵 EXTEN 关闭只影响灯带供电,不像
  `unit_bench` 那样需要考虑"断电会杀掉被测单元"的问题——这是刻意的设计前提,不是遗漏。

## 待实机点检(WSL 环境下均未验证,如实列出)

**基础 bring-up**:
- [ ] Core2 + Bottom2 组合体正常开机,状态栏电量/USB 显示正常,Page1 遥测卡显示合理数值
      (电压 3.3~4.2V 量级、未接 USB 时电流为放电正值)。
- [ ] 插拔 USB 前后:BattV/BattI/VBUS 卡的数值变化是否符合预期(插 USB 后电池电流应接近
      0,VBUS 电流应有读数;拔 USB 后电池电流应恢复为放电正值)——`CLAUDE.md` §11.2 明确
      要求点检的项。

**负载开关矩阵(P2 核心验收项)**:
- [ ] 背光 0/10/60/100% 四档循环点击,电流(万用表或 AXP192 遥测本身)是否有清晰台阶;
      0% 挡屏幕是否真的全黑(而不是残留微光)。
- [ ] 灯带 0/48/255 三档循环点击(须先把 EXTEN 行打开,否则灯带无论亮度设多少都不亮——
      这是预期行为,见「设计取舍 4」),电流台阶是否清晰。
- [ ] EXTEN 5V 开关切换,灯带随之亮灭;电流台阶是否可辨(5V 供电的灯带本身是这个开关能
      观察到的唯一负载,PORT.A/C 未接单元时台阶可能不大)。
- [ ] 喇叭测试音点击后是否真的听到约 350ms 的持续音;震动测试点击后是否有明显震动。
- [ ] CPU 锁频两档切换:`cpu_pm_available` 是否为 true(即 `CONFIG_PM_ENABLE=y` 在实机上
      正常生效);两档之间电流差异是否可辨(经典 ESP32 定频/DFS 之间的电流差异量级本身可能
      较小,是否淹没在其它负载/ADC 噪声里需要实测确认)。
- [ ] 电流 chart 的数据源切换(`trend(vbus mA)` / `trend(batt mA)`)在插拔 USB 时是否
      正确跟随;Y 轴量程(-500..2000mA)是否需要调整。

**休眠演练(P3 核心验收项)**:
- [ ] 点击"演练 NAP":提示文字"演练:NAP 中…"是否能被看清(哪怕一闪),随后背光是否降低、
      约 6s 后是否自动恢复清醒态(背光/灯带回到矩阵此刻记的挡位,而不是 core2_sleep 自己
      的默认档位)。
- [ ] 点击"演练 DEEP":提示文字是否能被看清,随后屏幕是否真的全黑(约 20s)、灯带是否熄灭、
      期间设备是否仍在正常采样(串口日志能看到吗?——若无串口连接则看不到,这是拔 USB 场景
      的固有限制)、结束后是否自动唤醒并正确回放"回放均值电流"/"回放时长"两张卡。
- [ ] **演练期间电流是否真的下降**(NAP 应比 AWAKE 低,DEEP 应比 NAP 更低,这是"回放均值
      电流"这个数字存在的核心意义,必须实机验证数值量级是否合理)。
- [ ] 演练中途按电源键或做其它操作,是否有除硬件强制断电外的异常行为(理论上不会,因为
      电源键软件触发已整体取消,但没有实机测过组合场景)。

**续航估算**:
- [ ] 不同放电电流下(比如只背光 60% vs 全负载开满),续航估算的小时数是否落在合理量级
      (500mAh 电池,典型几十到一两百 mA 的评估台负载,应该是几小时到十几小时的量级)。
- [ ] 充电中(插 USB)时是否正确显示"充电中"而不是一个数字。

**SPIFFS 离线录制(P4 核心验收项)**:
- [ ] 首次点击"录制:开始":是否触发 SPIFFS 格式化(首次上电后的 storage 分区大概率是未
      格式化状态);格式化期间(可能几秒)UI 状态文字是否卡在"初始化存储中…"、之后能否
      正确变为"录制中"。
- [ ] 录制若干分钟后点击"录制:停止",再点击"Dump 导出"(建议先插回 USB 接串口监视器):
      串口是否完整吐出 `#CSV-BEGIN power_lab` ... `#CSV-END` 之间的数据,行数是否与录制
      时长匹配,`tools/serial_capture.py --csv` 能否正确提取。
- [ ] 拔 USB 场景下真实录制一段较长时间(比如 10~30 分钟)后插回 USB 导出,确认数据连续、
      没有中断或损坏行(意外断电场景不在本轮验证范围,若发生也应如实记录现象)。
- [ ] 重复"录制:开始"(不先 Dump)确认新一段录制正确截断覆盖旧文件(而不是追加/损坏)。

**视觉自查(可用 `tools/screenshot.py` 远程截图初筛)**:
- [ ] Page1/Page2 布局在 320×240 实机上不裁字、不重叠,配色符合深灰工程风。
- [ ] 数值卡/chart/列表矩阵是否如预期出现在设计的坐标位置(本轮坐标数值均为纸面计算,
      未经实机像素级校验)。
- [ ] 翻页按钮("Page 2 >>" / "<< Page 1")文字切换是否正确、点击响应是否灵敏。

## 与其它 app 的关系 / 不受影响的部分

本轮新增/修改 `apps/power_lab/` 目录内容 + `components/data_log/`(新增 v2 SPIFFS API,
纯增量,未改动 v1 `data_log_begin/row/end` 的实现)。四张游戏卡带
(tilt_maze/busy_knobs/chick_pour/chain_lab)与 `apps/unit_bench` 不受本轮改动影响——
`apps/unit_bench` 重新 build 通过、产物体积与改动前一致,已实测验证。

## 构建

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh && source "$IDF_PATH/export.sh"
python "$IDF_PATH/tools/idf.py" -C apps/power_lab build
```

**2026-07-17 首次实现完工,build 通过**:`power_lab.bin` ≈ 0xb1d20 字节(约 727.8KB),
ota_5 槽(0x200000)余 54%,与 `apps/unit_bench`(ota_4,同为 0x200000 槽,余 54%)体积
量级一致。无编译警告。同批验证:改动 `components/data_log` 后重新 build
`apps/unit_bench`,产物体积 `0xb1680` 字节与改动前完全一致,确认 v2 SPIFFS API 新增未
影响 v1 串口 CSV API 的现有行为。
