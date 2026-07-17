# unit_bench — 外设/单元评估台(as-built)

> 本文件是 **unit_bench 应用的竣工记录**;功能规格见 `SPEC.md`。跨应用平台事实(EXTEN/
> DCDC3/repeated-start/桌面省电坑)见根 `CLAUDE.md` §7/§10/§11。占 **ota_4(0x990000)**。
> 烧录:`tools/flash_one.sh unit_bench`(= esptool write-flash 0x990000)。

产出:从 `tools/new_app.sh` 生成的空骨架,实现到 build 通过的完整评估台——列表页扫描
PORT.A(I2C)+ PORT.C(Chain UART)、6 个详情页(DLight/Ultrasonic/Gesture/8Encoder/
Chain Encoder/Chain Joystick)、热插拔发现与丢失、串口 CSV 导出、超声波零点标定。

**本轮完全在 WSL 环境下开发,没有实机设备可烧录验证**——所有"实机行为"的判断只到"代码逻辑
自洽 + build 通过"为止,详见文末「待实机点检」,如实标注、不假装已验证。

## 代码结构

```
apps/unit_bench/main/
  app_main.c            应用入口:平台 bring-up 顺序 + core2_sleep 配置 + 10Hz 主循环
                         (只管喂 IMU 给 core2_sleep_feed,UI 相关全部转给 unit_bench_ui)
  unit_bench_scan.h/.c   model 层:PORT.A 扫描 + 已知单元挂载状态机 + Chain 节点探测,
                         不碰 LVGL,可独立于 UI 理解/测试
  unit_bench_ui.h/.c     view 层:状态栏 + 列表页 + 6 个详情页的 LVGL 构建与轮询,
                         只对外暴露 unit_bench_ui_start() / unit_bench_ui_tick()
```

三层拆分(app_main 编排 / scan 做 I/O 与状态 / ui 做 LVGL)对应 CLAUDE.md §4"应用逻辑不
直接依赖裸驱动"的精神延伸:`unit_bench_ui.c` 也不直接管理"挂没挂"这件事,只查询
`unit_bench_scan` 的只读状态。

## 关键设计取舍

### 1. core2_sleep 禁用 DEEP 阶段的具体实现

`core2_sleep_cfg_t` 目前没有现成的"只留 NAP、禁用 DEEP"布尔开关。读了
`components/core2_sleep/core2_sleep.c` 源码后,判断进入 DEEP 的条件是(`core2_sleep_feed`
的 `CORE2_SLEEP_NAP` 分支):

```c
s->frames++;
if (s->frames > s->cfg.deep_after_ms / s->cfg.frame_ms) enter_deep(s);
```

`s->frames` 是 `int`,每个 NAP 阶段帧 +1。选择把 `deep_after_ms` 设成 `INT32_MAX`
(app_main.c):阈值 = `2147483647 / 100`(`frame_ms=100`,与主循环节拍对齐)≈ 21,474,836
帧,按 100ms/帧折算约 **24.8 天连续静止**才会碰到——不会溢出(`frames` 远达不到 int32 上限
约 21 亿),事实上等价于"永不进入 DEEP"。配合 `manage_bus_5v=false`(即使某天真被触发,也
不会切 M-Bus 5V 断掉被测单元供电),两条防线叠加,不依赖组件改动就达到了"评估台只打盹、不
深度断电"的效果。

**为什么不直接改 `core2_sleep` 组件加开关**:任务范围明确是"apps/unit_bench 从零骨架实现
到完整可 build",共享组件改动会影响其它已完工的 app(4 张游戏卡带),风险与收益不对等;
`INT32_MAX` 这个办法在不改组件公开 API/行为的前提下达成了同样的效果,是更保守的选择。若后续
真的需要"DEEP 演练"能力(如 power_lab 那样),`core2_sleep_force_stage()` 已经是组件公开
API,不需要为 unit_bench 这个取舍再动组件。

### 2. 桌面评估防误打盹:按"活动"而非"每次成功读"kick

`CLAUDE.md` §10 要求"旋钮转/手挥/光变/摇杆推都要 `core2_sleep_kick`"。但如果无条件地在
每次成功读数时都 kick(哪怕读数没变化),详情页打开着就会永久阻止打盹——这既违背省电初衷,
也不是 §10 真正想表达的意思(§10 说的是"评估对象被操作",不是"评估对象存在")。因此各
`poll_*` 函数都各自维护一份"上一次值",只在**数值变化超过噪声阈值**(DLight >5lx、
Ultrasonic >3mm、Chain Joystick 归一化值变化 >5)或**离散状态变化**(按键/开关翻转、
新手势事件、8Encoder 增量非零)时才调 `core2_sleep_kick`。真正静止摆在原地的评估对象(如
DLight 对着稳定光源)仍然允许机身打盹降亮。

### 3. 列表页固定 8+1 行,而非真正动态行数

`ui_list_menu` 只有 `add_row`(创建一次)+ `set_row_text`(改文字),没有"删除行"/"动态行数"
API。列表页在建 UI 时一次性创建 `UB_SCAN_MAX_ROWS=8` 个通用扫描结果行 + 1 个固定的 Chain
行,每轮扫描把探到的地址(最多 8 个,按 `unit_probe_scan` 的升序扫描顺序填入前 N 行,其余
清空为空文本)刷进这些行的文字,而不是"按需增减行数"。8 个足够覆盖平台已知的 4 个 PORT.A
单元 + 少量意外挂上的其它 I2C 器件;若真的挂满 8 个以上未知地址,第 9 个起会被截断(不显示,
不是崩溃),这是一个已知的展示上限,记在下面「已知限制」。

### 4. Gesture "Last" 手写卡片而非 `ui_value_card`

`ui_value_card` 只有两种显示态:数字(`set_value`,正常/告警两色)和任意文字但固定告警红色
(`set_error`)。手势名字(如 "WAVE")是**正常态的文字**,用 `set_error` 会被误显示成红色
"出错"观感,语义不对。因此 Gesture 详情页的"Last"卡片是手写的一个迷你面板(`make_text_card`,
视觉上与 `ui_value_card` 同款深灰面板+标签+大字,只是直接 `lv_label_set_text` 而不经过
数字格式化),没有改动 `ui_kit` 组件本身。

### 5. 超声波两笔式测量拆成两个主循环 tick

`unit_ultrasonic_trigger()` 和 `unit_ultrasonic_read_mm()` 是两笔独立 I2C 事务,中间要等
一个测量周期(~50~120ms)。主循环节拍 100ms,所以设计成"本 tick 触发、下一 tick 读"的两阶段
状态机(`s_ultra_phase`),两个 tick(~200ms)完成一次测量 = 5Hz,落在 CLAUDE.md §6.6 建议
的 2~10Hz 区间。

## 已知限制

- **列表页扫描结果最多展示 8 个地址**(见上「设计取舍 3」);Chain 只支持单节点直连(id=1),
  不支持多节点级联展示。
- **Gesture "Last" 卡片没有告警色切换**——断线/未接只改文字("未接"/"断线"),不像
  `ui_value_card` 那样变红,视觉上不如其它详情页醒目(功能仍然正确显式呈现,只是没有颜色
  强调)。
- **8Encoder 详情页只精选展示 Enc0/Enc1 两路 + 按下计数 + 开关状态**,受 CLAUDE.md §8"单屏
  最多 4 数值卡"密度上限约束,其余 6 路增量不在 UI 上展示(CSV 导出记录全部 8 路,导出数据
  比屏上更完整)。
- **阈值告警变色未启用**:所有 `ui_value_card_set_value` 调用都传 `thresh=NULL`,不同评估
  对象/环境下"正常范围"差异很大,没有为它们各自定一个默认阈值(SPEC §9 已声明为范围外)。
- **Chain 节点类型运行时切换未处理**:若用户在 Chain Encoder 详情页停留时把节点拔掉换成
  Joystick,详情页会显式判"未接"(类型不匹配),但不会自动跳去 Joystick 视图,需要手动返回
  列表页重新进入。
- **超声波偏移调整没有长按连续加减**,每次点击 Cal-/Cal+ 只调 ±1mm(简单交互,足够零点微调,
  但大幅调整需要多次点击)。
- **列表页 Rescan 按钮 / 后台周期扫描的 Chain 探测有阻塞**:`chain_bus_get_device_type`
  节点不在位时会等满超时(300ms)才返回,这段时间发生在调用方各自的任务上下文里(后台扫描
  在 app_main 的主循环任务、手动 Rescan 在 LVGL 事件回调任务),都不会阻塞对方,但各自会有
  一次性的 ~300ms 停顿,属已知、可接受的设计取舍(见 `unit_bench_scan.h` 头注释)。

## 待实机点检(WSL 环境下均未验证,如实列出)

**基础 bring-up**:
- [ ] Core2 + Bottom2 组合体正常开机,状态栏电量/USB 显示正常(`power_monitor` 遥测准确性)。
- [ ] 未接任何 PORT.A/PORT.C 设备时,列表页 8 行全空 + Chain 行显示"(未接)",不报错卡死。

**PORT.A 单元(热插拔)**:
- [ ] 依次单独接上 DLight/Ultrasonic/Gesture/8Encoder,列表页在 ≤2s 内自动出现对应行
      (不用手动点 Rescan);点进详情页数值正确更新。
- [ ] 运行中拔线 → 详情页 ~2s 内(连续 20 帧读失败)显式红字"断线" + haptics 报警震动;
      插回 → ≤2s 内自动恢复(不需要用户操作)。
- [ ] 同时接多个单元 + 若干未知地址的其它 I2C 器件,列表页正确区分已知/未知,未知地址显示
      十六进制。
- [ ] PORT.A 总线被拉死(如接线短路)时,列表页正确显示"总线异常,检查供电/线缆"红字,不是
      呆住不动或显示误导性的"未接"。
- [ ] 8Encoder 分笔读(写寄存器号+STOP,再单独读)实测无总线卡死;长时间连续转动/按键无异常。

**Chain(PORT.C)**:
- [ ] Chain Encoder 单独接入:列表页显示"Chain: Encoder",详情页 Count/Button/chart 正确
      跟随实际转动/按下。
- [ ] Chain Joystick 单独接入:详情页 X/Y 归一化值跟手,静止居中在 0 附近(个体零偏软件归中
      是否生效);Z 按键正确。
- [ ] Chain 节点插拔实测:拔线后详情页显式判"未接"、haptics 报警;插回后需要的时间(后台
      2s 扫描周期)是否符合预期体验。

**超声波标定**:
- [ ] Cal-/Cal+ 按钮调整偏移,详情页数值实时反映偏移量。
- [ ] 调整后重启设备(掉电重上电),偏移量应从 `kv_store`(NVS)读回并继续生效——这是本轮
      唯一需要跨越"设备重启"验证的功能点,WSL 环境完全无法覆盖。
- [ ] 无回波场景(前方无遮挡)展示为量程上限而非红字断线,单元仍判定"在位"。

**CSV 导出**:
- [ ] 各详情页 Log 按钮开启后,`tools/serial_capture.py --csv` 能正确提取到对应列名与数据行。
- [ ] 8Encoder CSV 的 8 路原始数据与屏上展示的精选 2 路 + 摘要数值一致(交叉核对)。
- [ ] 离开详情页(点 Back)时若仍在录制,确认 CSV 尾部有正确的结束哨兵(自动 `data_log_end`
      是否生效)。

**省电**:
- [ ] 机身静止且当前详情页评估对象也静止(如 DLight 对着稳定光源)→ 实测能正常打盹降亮
      (NAP),且**不会**进一步深度省电断电(长时间静置观察,验证"事实上禁用 DEEP"的取舍
      在实机上确实生效,不只是理论推算)。
- [ ] 机身静止但正在操作评估对象(转 8Encoder 旋钮 / 挥手势 / 推摇杆)→ 不会被误判打盹
      (验证「设计取舍 2」的活动阈值判据在真实噪声环境下是否合适,若太灵敏/太迟钝需要调整
      各 `poll_*` 里的阈值常量)。
- [ ] 拿起/晃动机身能从 NAP 正常唤醒(背光恢复、灯带回常态)。

**视觉自查(可用 `tools/screenshot.py` 远程截图初筛,不需要人整个盯屏)**:
- [ ] 列表页 / 6 个详情页布局在 320×240 实机上不裁字、不重叠,配色符合深灰工程风。
- [ ] 数值卡/chart 是否如预期出现在设计的坐标位置(本轮坐标数值均为纸面计算,未经实机像素
      级校验)。
- [ ] chart 趋势线在真实数据速率下观感是否合适(点数/推点频率是否需要调整)。

## 与其它 app 的关系 / 不受影响的部分

本轮只新增/修改 `apps/unit_bench/` 目录内容,未改动任何共享 `components/`、`launcher/`、
`partitions.csv`、`tools/flash_*`、根 `CLAUDE.md`。四张游戏卡带(tilt_maze/busy_knobs/
chick_pour/chain_lab)与 `power_lab` 不受本轮改动影响。

## 构建

```bash
source ~/.espressif/tools/activate_idf_v6.0.sh && source "$IDF_PATH/export.sh"
python "$IDF_PATH/tools/idf.py" -C apps/unit_bench build
```

**2026-07-17 首次实现完工,build 通过**:`unit_bench.bin` ≈ 0xb1680 字节(约 727KB),
ota_4 槽(0x180000)余 54%。无编译警告。
