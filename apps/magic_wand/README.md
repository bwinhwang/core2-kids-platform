# magic_wand — 魔法萤火虫 v2.1(as-built,P1 · 构建记录 · 待实机)

> 🔴 v1「九法术隔空魔法棒」已被否决(2026-07-11 设计评审:幼儿无法从 UI 理解"需要用手做
> 手势",根因见 `SPEC.md` §0 / git 历史)。
> 🔴 v2「光标模式跟手」P1 **已实机验证否决**(2026-07-11):157s 游玩实测手在视野内占
> 空比仅 ~50%,>0.5s 跟踪中断 31 次(平均 2.5s、最长 15.7s),偶发单步满量程跳变——
> PAJ7620U2 光标模式是为指尖近距设计,手掌极易滑出/充满视野,连续位置信号对幼儿场景
> 不可用。v2 as-built 记录见 git 历史(commit 附近 `3a25739`)与 `SPEC.md` §0。
>
> 本文件描述 **v2.1「在场+手势」(Plan B)P1 阶段**的 as-built:PAJ7620U2 手势模式
> (9 手势,v1 已验证路径)+ 新增在场信号(0xB0 亮度)驱动"贴玻璃盘旋" + 九手势方向
> 翻滚 + 近距音阶 + 你好/再见。P2–P4(夜花园 5 目标/方向拜访/在场兜底/BLOOM/派对/
> 暮色/流星彩蛋/招手引导/魔法灯笼)**尚未实现**,规格见 `SPEC.md`。

> 占 ota_5(0xB90000,2MB)。外设 = Unit Gesture-I2C(PORT.A @0x73)。**Unit RGB
> (PORT.B,"魔法灯笼")是 P4,本阶段代码里完全没有接入**(`wand_fx.c/.h` 未建立,
> `CMakeLists.txt` 不 `REQUIRES unit_rgb`)。
> 烧录:`tools/flash_one.sh magic_wand`(= esptool write-flash 0xB90000)。

## P1 做了什么(SPEC.md §12)

只验证"在场+手势"这一套新因果链,其余一切的前提:

- `unit_gesture` 新增 `unit_gesture_read_presence(brightness, size)`(bank0 0xB0 亮度 +
  0xB1/0xB2 尺寸,两笔事务,寄存器存在性/位宽已核实,语义待标定);光标模式 API 保留
  不删(已实机否决,头注释加否决记录),9 手势路径(0x43/0x44)沿用 v1。
- `magic_wand.c` 初始化改回**默认手势模式**(`unit_gesture_init()` 之后不再调
  `unit_gesture_set_cursor_mode()`),DEEP 唤醒重接管同理。
- 30Hz `game_task`:每帧轮询在场(0xB0)+ 手势(0x43/0x44)两样,两者都读失败才累计
  `ERR_STREAK_LOST` 判拔线;在场信号走 EMA(30%)→ 迟滞 ON/OFF → 离场保持 1500ms →
  强度三档(迟滞防蹦档)。
- 状态机 `ST_NO_UNIT → ST_SEEK → ST_DANCE → ST_GOING_HOME → ST_SEEK`:SEEK 萤火虫停
  家花慢眨眼;DANCE 贴"玻璃"(屏幕中央偏上)8 方位查表离散步进盘旋(步速/半径随强度
  档),九手势触发方向翻滚(位移/缩放叠加层,~220ms,同方向 400ms 冷却,不同手势随时
  打断);GOING_HOME 复用既有回家动画。
- 近距音阶(档位切换单音,523/659/784 对应远/中/近)+「你好」(首次入场或离场
  ≥2s 后再入场:双音+`HAPTIC_HELLO`)+「再见」(离场保持耗尽:欢摆翻滚+降双音,无震)。
- 夜花园静态层、萤火虫精灵画法/眨眼/回家动画、无单元提示卡、拔线判定、省电挂载——
  全部原样复用 v2 P1 资产(`garden.c` 未改动)。

**不做的**(P2–P4,SPEC §12 明确分期,不堵死加入点):夜花园 5 目标方向拜访/在场
兜底/BLOOM、天亮派对、暮色重置、月亮彩蛋、SEEK 招手引导、魔法灯笼(unit_rgb)。

## 光标模式否决记录(核实的实机数据,AGENTS.md §1 铁律)

**2026-07-11 实机验证**(v2 P1,157s 游玩):
- 手在视野内(`in_view==true`)占空比 **~50%**。
- >0.5s 的跟踪中断 **31 次**,平均 **2.5s**、最长 **15.7s**。
- 偶发单帧位置从一端跳到另一端的满量程跳变。
- 光标原始坐标实测量程 **x/y ∈ [384..3455]**(留档备用,量程本身是真实观测值,不是
  编造;`tuning.h` 已删除 `CUR_RAW_*` 常量,这段量程数字只作历史记录)。

**根因**(SPEC.md §0):PAJ7620U2 光标模式是为**指尖**近距设计(60° FOV 在 10cm 处仅
~11cm 见方隐形窗口),手掌尺寸/幼儿手部稳定性使其极易滑出或充满整个视野,导致连续
位置信号断续到不可用——这是传感器工作原理层面的不匹配,不是滤波参数能调出来的。

`unit_gesture` 的光标模式 API(`unit_gesture_set_cursor_mode/set_gesture_mode/
read_cursor`)**保留不删**,寄存器事实（叠加写表/坐标寄存器/`in_view` 判据）继续
留档在 `unit_gesture.h` 头注释,只是 `apps/magic_wand` v2.1 起不再调用。

## 在场信号 API 寄存器事实(核实记录)

**Confirmed via acrandal/RevEng_PAJ7620(github.com/acrandal/RevEng_PAJ7620,库版本
1.5.0,2026-07-11 抓取 `src/RevEng_PAJ7620.h` + `.cpp` 源码原文,经
raw.githubusercontent.com)**:

1. **寄存器存在性 + bank + 位宽已核实**:`PAJ7620_ADDR_OBJECT_BRIGHTNESS`=0xB0(只读
   [7:0],`getObjectBrightness()` 单笔读无遮罩,"255 is max");`PAJ7620_ADDR_OBJECT_
   SIZE_LSB`=0xB1(只读 [7:0])/ `_MSB`=0xB2(只读 **[3:0]**,注意与光标 X/Y 的
   [11:8] 不同段);三者均在 **bank0**,与手势结果寄存器 0x43/0x44 同 bank——本驱动
   `init()` 结尾已 `select_bank(0)` 停泊,常态轮询(手势模式)无需再切 bank。
   `getObjectSize()` 合成为 `(data1<<8)|data0`,"900 is max (30x30 像素阵列)"。参考
   实现本身未对 MSB 字节遮罩(硬件保证高位为 0),本驱动仍按与光标 X/Y 一致的防御性
   写法遮罩 `&0x0F`(不改变数值,只是更保守)。
2. **读数语义(无手/远/近手对应的实际数值分布)两份来源(RevEng 源码 + PixArt
   datasheet)都未给标定曲线**——寄存器事实到"数值范围 0..255 / 0..4095"为止。
   `tuning.h` 的 `PRES_ON_TH`/`PRES_OFF_TH`/`PRES_LVL2_TH`/`PRES_LVL3_TH` 因此是
   **待实机标定的占位值**,与 v2 `CUR_RAW_X/Y_MIN/MAX` 同等地位处理,不是编造的
   寄存器事实。
3. **读事务形状不变**:0xB0 单字节一笔"写寄存器号(STOP)+ 单独发起读";0xB1/0xB2
   自增地址合并一笔读 2 字节——两笔独立事务惯例不变。

完整逐条记录见 `components/units/unit_gesture/include/unit_gesture.h` 头注释"在场
信号"大段与 `components/units/unit_gesture/README.md`。

## 状态机与常量现值(`apps/magic_wand/main/tuning.h`,SPEC §10)

**在场阈值已实机标定(2026-07-11,CALIB_LOG 实测)**:本底 raw=0(开机仅一帧杂散 36,
EMA 30% 滤除)、远 ~15cm ≈ 150~165、贴近 ~5cm 顶满 255(size 同步顶满 900);手在场
期间占空比 100%、零中断——在场信号质量远优于被否决的光标模式,无需启用 size 备用信号。

| 常量 | 现值 | 含义 |
|---|---|---|
| `PRES_POLL_MS` | 33(30Hz) | 在场/手势轮询周期,`core2_sleep_cfg_t.frame_ms` 直接设成它 |
| `PRES_EMA_PCT` | 30 | 在场亮度 EMA 新值权重 % |
| `PRES_ON_TH` / `PRES_OFF_TH` | 30 / 15 | 已标定:本底 0,单帧杂散靠 EMA 压 |
| `PRES_HOLD_MS` | 1500 | 离场保持(桥接断续) |
| `PRES_LVL2_TH` / `PRES_LVL3_TH` | 185 / 235 | 已标定:远≈160 归 L1,贴近顶满 255 归 L3 |
| `PRES_LVL_HYST` | 15 | 档位迟滞,防蹦档 |
| `DANCE_STEP_MS_L1/L2/L3` | 140/100/70 | 盘旋步速三档(远/中/近) |
| `TUMBLE_MS` | 220 | 方向翻滚单次时长 |
| `TUMBLE_COOLDOWN_MS` | 400 | 同方向手势冷却 |
| `HELLO_GAP_MS` | 2000 | 离场多久后再入场才算新「你好」 |
| `HOME_FLY_MS` | 1200 | 回家动画时长 |
| `CALIB_LOG` | 0 | 已回填关闭;再标定改回 1 重编 |

盘旋/翻滚几何常量(屏幕中心坐标、半径两档、翻滚位移/缩放量、欢摆摆幅)是纯视觉细节,
不在 SPEC §10 tuning.h 列表(同 v2 halo/core 尺寸的先例),留在 `firefly.c` 顶部。

## 实现取舍(与 SPEC.md 的落地差异)

- **翻滚用"位移/缩放叠加层"而非旋转精灵**:萤火虫外观是简单圆形(halo+core),旋转一
  个圆形视觉上没有差异,因此 LEFT/RIGHT/UP/DOWN 的"翻滚"实现为朝方向的位移-回弹
  (out-and-back),FORWARD/BACKWARD 为缩放-回弹(BACKWARD 用 `lv_anim_set_playback_
  delay` 在缩到最小时停顿一下,呼应"顿"),CW/CCW 复用盘旋的 8 方位查表做"快速整圈"
  (沿用 `DANCE_STEP_MS_L3` 最快步速),WAVE 是一段两周期正弦位移(平滑起止于 0,无需
  LVGL repeat/playback 拼接)。这是 SPEC 未指定具体实现方式、但明确要求"方向一致/
  瞬时/可打断"效果的地方,判断更简单也更贴合"扁平色块"审美(CLAUDE.md §6.3)。
- **翻滚打断统一走一个入口**:`firefly.c` 的 `tumble_cancel()` 在每个 `firefly_
  tumble_*()` 入口处统一调用(删正在播的叠加动画 + 叠加量归零 + 停掉筋斗云整圈),
  `magic_wand.c` 不需要关心"打断上一个动画"的细节,只管按冷却表决定"要不要触发"。
- **近距音阶"让位"用简单的时间窗口实现**:`audio_fx` 没有暴露"是否正在播放"的查询
  接口,因此"手势音签播放期间近距音阶让位"(SPEC §5.1)用一个本地时间戳
  `s_gesture_audio_yield_until_ms`(触发手势音签时设为 now+300ms)实现——不是新增
  audio_fx API,300ms 覆盖最长的 4 音签(~4×45ms)并留余量。
- **CW/CCW 筋斗云复用盘旋的 8 方位表,不是独立的旋转动画**:一整圈 = 8 步,方向取
  `+1`(CW)/`-1`(CCW),沿用 `DANCE_STEP_MS_L3`(70ms/步,~560ms 走完整圈)——净位移
  为零(8 步回到起点),与 SPEC "8 方位查表" 的字面要求一致,也复用了盘旋已有的步进
  代码路径,没有另写一套。
- **「再见」直接复用 WAVE 翻滚**:SPEC §5.1"回家前欢摆一下"与 GESTURE_WAVE 的反应
  (欢摆)是同一个动作,离场保持耗尽时直接调用 `firefly_tumble_wave()` + 播降双音
  (无 haptic),不必另写一个"告别摆动"函数;因为翻滚叠加层与回家飞行分属不同的权威
  状态(叠加位移 vs. 基准位置),两者可以同时播放,视觉上正是"边挥手边飞走"。
- **DANCE 内不再有"悬停慢眨眼"这个中间态**:v2 的 `firefly_hover_enter/exit()`(丢手
  宽容期切换眨眼节奏)在 v2.1 里没有对应物——SPEC §4.1 明确"保持期舞照跳,只降到
  最低档",即离场保持期内继续正常渲染盘旋(强制 level=1),不切到任何"等待"外观。
  因此这两个 API 已从 `firefly.h`/`firefly.c` 删除,`BLINK_HOVER_MS` 常量一并删除。
- **在场 EMA/迟滞/档位判定全部放在 `magic_wand.c`,不下沉进 `unit_gesture`**:与
  v2 光标模式的"滤波链在 app 层"是同一分工(驱动只管读寄存器,app 层管信号处理),
  `unit_gesture_read_presence()` 只做两笔事务读原始字节,不做任何平滑/判定。
- **省电/容错/唤醒策略与 v2 P1 完全一致**(唤醒统一回 SEEK、DEEP 唤醒重新
  `unit_gesture_init()`、拔线提示卡、`ERR_STREAK_LOST` 判据),因为这套设计与"光标
  vs 在场"这个信号来源无关,SPEC §3/§7 也明确"省电正交同 v2"。
- **灯带(ledstrip_fx)P1 仍只用常态氛围光**(`LED_BASE_AMBIENT`),没有为在场/手势
  新增灯带触发——SPEC.md 在 P1 范围(§12)与反馈冲突矩阵(§6)里都没有列出 P1 阶段
  的灯带事件编排(只有 §5.3 魔法灯笼明确写 P4),因此判断为可以留到后续阶段一起做,
  避免臆造 SPEC 未要求的灯带词汇表。

## 桌面玩法省电坑(沿用平台通用变体)

机身不动 ≠ 没人玩——在场信号 ON(迟滞后)或手势分类成功即 `core2_sleep_kick`(真实的
手,SPEC §7;离场保持期内**不** kick,因为"保持是宽容,不是证据")。NAP 时 5V 还在、
在场/手势可唤醒(唤醒帧不判定,仿 busy_knobs 先例);DEEP 切 5V → Gesture 单元断电
复位,唤醒后 `unit_attach()` 重新 `unit_gesture_init()`(默认落在手势模式,无需再调
任何模式切换)。容错:没插 Gesture = 无字提示卡 + `ATTACH_RETRY_MS`(2s)周期重试,
插上即接管;连续 `ERR_STREAK_LOST=20` 次(在场+手势**都**读失败)判拔线回提示卡。

## 待实机标定 / 验证(SPEC.md §13,P1 单独一轮,先烧先测)

- ~~`PRES_*` 真实阈值~~ **已完成**(2026-07-11 实机标定,分布见上"常量现值"一节,
  阈值已回填、`CALIB_LOG` 已关);再标定时改回 `CALIB_LOG=1` 重编,观察
  `CALIB pres raw=... ema=... size=... lvl=... state=...` 日志。
- **`PRES_HOLD_MS`(1500ms)保持期手感**:是否真的桥过了实测 2.5s 级的断续(若回家/
  你好的"抽搐感"仍在,考虑加大;若手真拿开后回家太慢,考虑减小)。
- **九手势方向映射**:四方向挥手翻滚方向是否与孩子视角一致(错了改
  `unit_gesture.c` 的 `unit_gesture_read()` switch,不要改初始化表,见驱动头注释
  既有预案);CW/CCW 筋斗云的旋转方向感是否符合直觉。
- **翻滚手感**:`TUMBLE_MS`(220ms)/`TUMBLE_OFFSET_PX`(34px,`firefly.c` 内)是否
  "看得清但不拖沓";`TUMBLE_COOLDOWN_MS`(400ms)连续挥手是否显得迟钝或过于灵敏。
- **「你好」/「再见」体验**:`HELLO_GAP_MS`(2000ms)阈值、降双音+欢摆的"再见"是否
  自然,不显得聒噪或突兀。
- **(本重设计成败的唯一标准,SPEC §13)3~4 岁孩子无人讲解,2 分钟内自发理解"手让
  它醒/手近让它疯/手挥让它翻"**——这一条比其余任何数值调参都重要。
- 无单元提示卡的手挥 pictogram 是否清楚传达"对着这个方块挥手";拔线/插回的提示卡
  切换是否符合预期。
- 打盹(12s)/深度省电(60s)进出,DEEP 唤醒后是否正确重新接管(默认手势模式,在场/
  手势轮询立即恢复)。

## P2–P4 待做(SPEC.md §12,不在本次范围)

- **P2**:夜花园 5 个沉睡目标(粉花苞/蓝铃花/打瞌睡小鸟/池塘青蛙/星星草)+ 方向拜访
  (DANCE 中翻滚经过某方向的沉睡目标即预亮,累计 `VISIT_WAKE_N` 次苏醒)+ 在场兜底
  (`PRES_WAKE_FALLBACK_MS`=45s 自动唤醒"最久没动静"的目标,保证零失败可达 5/5)+
  BLOOM 全通道反馈。
- **P3**:天亮派对(5/5 点亮)+ 暮色重置 + 月亮彩蛋(CW/CCW 筋斗云时随机小概率触发
  流星,不占进度)+ SEEK 招手引导(`ATTRACT_IDLE_MS`=8s)。
- **P4**:魔法灯笼(`unit_rgb` 新词汇 BREATH/ON/BLINK_COLOR/RAINBOW/OFF,`wand_fx.c/.h`
  需要重新建立)+ launcher 专属图标(萤火虫,当前显示通用笑脸)。

## launcher 图标

尚未加 magic_wand 专属图标分支(显示通用笑脸,不影响可玩性);待 P1 实机验收后再评估
(图标本身与本次 P1 在场+手势验证无关),**要重刷 launcher 才显示**。

## 状态

✅ **build 通过**(`idf.py -C apps/magic_wand build`,esp-idf v6.0.1 命令行,零警告):
标定回填版 `magic_wand.bin` 0xa21e0 字节 ≈ 648.5KB,ota_5 槽余 ~68%,**已烧录实机**。
✅ **在场信号标定通过**(2026-07-11):本底 0 / 远≈160 / 近顶满 255,占空比 100% 零中断,
阈值已回填(见"常量现值"一节)——传感层数据面是健康的,这部分结论可长期复用。
🔴 **P1 实机体感验证欠佳 → 项目暂停**(2026-07-11 用户实测反馈"效果不太好",具体症状
未详录)。P2–P4 不推进。若重启,先补一轮症状定位(舞蹈观感?手势翻滚命中率/方向?
节奏参数?还是玩法本身吸引力),再决定是调参、改表现层,还是回设计层重议——
不要跳过定位直接盖楼(v1/v2 两轮教训一致:先验地基再投入)。
