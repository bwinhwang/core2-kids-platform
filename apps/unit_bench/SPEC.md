# unit_bench SPEC —— 外设/单元评估台

> **本文件是功能规格**(这个 app 该做什么);**竣工现状 / 落地差异 / 待实机点检见同目录
> `README.md`(as-built)**。实现前先读根 `CLAUDE.md` §2(六条设计原则)、§4(组件层)、
> §6(渲染红线)、§7(电源·休眠)、§8(UI 可读性)、§10(做新 App 指南:单元接入 + 桌面
> 评估省电坑)。占 `ota_4`(`0x990000`),槽表见 `tools/flash_map.md`。

## 0. 定位

`unit_bench` 是平台的**外设/单元评估台**:接上 PORT.A(I2C 外接单元)或 PORT.C(Chain UART
菊花链)的评估对象,屏上实时看数值/趋势、串口能拿 CSV、超声波能标零点。不是游戏,不追求
"好玩",追求 CLAUDE.md §2 的六条评估台原则(可观测优先 / 错误显式呈现 / 数据可导出 / 热插拔
容错 / 渲染红线 / 省电纪律)。

## 1. 覆盖的评估对象

| 对象 | 接口 | 地址/位置 | 驱动组件 |
|---|---|---|---|
| DLight(BH1750,照度) | PORT.A I2C | 0x23 | `unit_dlight` |
| Ultrasonic(RCWL,测距) | PORT.A I2C | 0x57 | `unit_ultrasonic` |
| Gesture(PAJ7620U2,手势) | PORT.A I2C | 0x73 | `unit_gesture` |
| 8Encoder(STM32F030 从机) | PORT.A I2C | 0x41 | `unit_8encoder` |
| Chain Encoder(U207) | PORT.C UART 菊花链 | id=1(单节点直连) | `unit_chain_encoder` |
| Chain Joystick(U205) | PORT.C UART 菊花链 | id=1(单节点直连) | `unit_chain_joystick` |

未知 I2C 地址(不在上表)也要能被看见(十六进制展示),不能因为"不认识"就在列表里消失——
评估台的价值就在于"接上什么都能先看到点什么"。

## 2. 页面结构

### 2.1 列表页(开机默认页)

- 顶部状态栏(24px):app 名 / uptime / 电量+USB(`ui_status_bar`,`power_monitor` 遥测)。
- 中间:PORT.A 扫描结果列表(已知单元显示名字,未知地址显示十六进制)+ Chain 节点探测结果
  (固定一行,显示"未接"/"Encoder"/"Joystick"/"未知类型")。
- 底部:Rescan 按钮(手动即时刷新一次;不按也会后台每 2s 自动扫)。
- **总线异常显式呈现**:扫描前检查 `core2_board_port_a_stuck()`,拉死时列表清空 + 顶部红字
  "PORT.A 总线异常,检查供电/线缆"(不静默、不误导成"什么都没插")。
- 点已在位的行 → 进对应详情页;点未在位的行/未知地址行 → 轻反馈,不跳转。

### 2.2 详情页(6 个,布局共性:内容区 0~184px + 底部按钮条 184~216px)

| 页面 | 数值卡 | chart | 特有交互 |
|---|---|---|---|
| DLight | Lux | 有(lux 趋势) | — |
| Ultrasonic | Dist(mm,含标定偏移) | 有(mm 趋势) | Cal-/Cal+ 零点标定,持久化 `kv_store` |
| Gesture | Last(文字)+ Count | 无(事件型,不适合连续 chart) | — |
| 8Encoder | Enc0/Enc1(累计)+ BtnCnt + SW | 无(多通道,不适合单线 chart) | — |
| Chain Encoder | Count + Button | 有(count 趋势) | — |
| Chain Joystick | X/Y(归一化 %)+ Z Btn | 无(2D,不适合单线 chart) | 进页采一次居中值软件归中 |

每个详情页底部按钮条至少有 Back(回列表)+ Log(CSV 导出开关);Ultrasonic 额外有 Cal-/Cal+。

### 2.3 密度合规(CLAUDE.md §8)

单屏最多 4 数值卡 + 1 chart。本 app 各详情页最多同时用到 4 张卡(8Encoder)或 1 卡+1 chart
(DLight/Ultrasonic/Chain Encoder),均在预算内。

## 3. 热插拔

- **发现**:后台每 2s 一次 PORT.A 全总线扫描(`unit_probe_scan`,zero-length write 探 ACK,
  无副作用)+ Chain 设备类型探测(`chain_bus_get_device_type`);对"当前未挂载"的已知单元
  尝试 init 接管,成功即在列表/详情页里"接管"(不需要用户手动操作)。
- **丢失**:两条路径都会判定单元丢失——① 后台扫描本轮没探到该地址;② 详情页内连续读失败
  ~20 帧(10Hz 主循环下约 2s)。判丢失后详情页显式红字("未接"/"断线"),不静默;下一轮后台
  扫描会继续尝试重新接管。
- **8Encoder 分笔读**:严格遵守 `docs/units/_MCU_Firmware_I2C_Units.md` 的"写寄存器号+STOP,
  再单独读"两笔事务规则(已在 `unit_8encoder` 驱动内部实现,应用层不重复处理)。
- **超声波无回波**:`unit_ultrasonic_read_mm` 返回 `ESP_ERR_NOT_FOUND` 不算失败(器件仍在,只
  是暂无目标),不计入拔线判定,画面展示为量程上限而非红字错误。

## 4. 数据导出(`data_log`)

每个详情页的 Log 按钮是一个开关:开→ `data_log_begin(单元名, 列名)`,之后每次成功读数顺带
`data_log_row(...)`;再点一次 → `data_log_end()`。列名设计上尽量比屏上展示更完整(如
8Encoder 的 CSV 记录全部 8 路,UI 只精选展示 4 路摘要)。离开详情页(点 Back)若还在录制会
自动收尾,不留悬空的 `data_log_begin` 没有对应 `data_log_end`。

主机侧 `tools/serial_capture.py --csv` 从串口哨兵行提取 CSV。

## 5. 超声波零点标定(`kv_store`)

Ultrasonic 详情页 Cal-/Cal+ 按钮每次调整偏移量 ±1mm,存 `kv_store`(namespace `unit_bench`,
key `ultra_offset`),重启后从 NVS 读回继续生效。展示值 = 原始读数 + 偏移量。

## 6. 电源 / 省电

- 评估台默认仍做两级省电(`core2_sleep`),但两条平台默认行为在评估台语境下要覆盖:
  1. **`manage_bus_5v=false`**:深度省电切 M-Bus 5V 会顺带切断 PORT.A/PORT.C 被测单元的
     供电,这是评估台语境下的错误行为(CLAUDE.md §10)。
  2. **事实上禁用 DEEP 阶段**:只留 NAP(降亮),不断电、不断被测单元供电、不影响正在进行
     的评估。具体实现方式(`deep_after_ms` 设 `INT32_MAX`)见 README「设计取舍」。
- **桌面评估防误打盹**:机身不动 ≠ 没人在评估——旋钮转/手挥/摇杆推/超声波前有物体经过/
  光照变化都要 `core2_sleep_kick()`,否则评估中打盹(CLAUDE.md §10)。各详情页按各自"活动"
  判据(数值变化超噪声阈值 / 按键状态变化 / 新手势事件)决定何时 kick,而不是每帧无条件 kick
  (无条件 kick 会让"真正静止"时也永不打盹,失去省电意义)。

## 7. 渲染纪律(CLAUDE.md §6)

- 列表页/详情页各自的静态层(行/卡片边框/标签/按钮)只在进页时画一次;页内轮询只更新数值
  文本 / chart 推点 / 列表行文字,不整屏重绘。
- 列表页与详情页是两个常驻的 LVGL 容器,用 `LV_OBJ_FLAG_HIDDEN` 互斥切换,不是每次都重新
  创建整个页面(详情页内容区因为 6 种视图差异较大,进入时 `lv_obj_clean` 后按视图类型重建
  一次——这发生在用户点击导航的一次性代价上,不是每帧行为,不违反渲染红线)。

## 8. 里程碑(对应实施计划 B1~B4)

- **B1**:扫描 + 列表页 + 热插拔发现 + 总线异常提示。
- **B2**:DLight / Ultrasonic / Gesture 三个纯 I2C 传感器详情页 + chart + 热插拔丢失处理。
- **B3**:8Encoder 详情页(累计计数)+ Chain Encoder / Joystick 详情页(含摇杆软件归中)。
- **B4**:CSV 导出(Log 按钮)+ 超声波零点标定(Cal 按钮 + kv_store 持久化)。

## 9. 明确不做(本轮范围外)

- 阈值告警变色(`ui_value_card` 支持但本轮未启用,数值卡统一 `thresh=NULL`)——评估台场景下
  "正常/异常"的阈值因单元/环境而异,没有一个通用默认值值得硬编码,留给用户看数值自行判断。
- 8Encoder 逐路 RGB 反馈 / 逐路数值全展示(密度预算只够展示 2 路+摘要,CSV 导出补全 8 路)。
- Chain 节点 RGB 反馈(`chain_bus_set_rgb`)——评估台以数值/图表为主信息通道,不强制四通道
  齐鸣(CLAUDE.md §5),错误场景已有红字+haptics 报警,正常读数不需要额外节点灯反馈。
- 运行时 Chain 节点类型切换(评估中途拔 Encoder 换插 Joystick):详情页内若节点类型与当前
  视图不符会显式判"未接",需要用户手动回列表页重新进入正确的详情页。
