# magic_wand SPEC —— 隔空魔法棒(新卡带·设计规格)

> **本文件是给实现者(AI 协作者)的施工图**:占 **ota_5**(平台目前唯一空闲槽位)。核心外设
> **Unit Gesture(PAJ7620U2,PORT.A I2C 0x73)**,配套 **Unit RGB(PORT.B,3×SK6812,G26)**
> 作贴身"魔法棒"补光灯。用户已买到两颗单元实物,可实机验证。
>
> **本文只是设计稿,尚未开工**——先给用户比较/裁剪范围(见 §13 分阶段优先级),确认后再实现。
> 实现前先读根 `CLAUDE.md` §2(五原则)、§5(反馈通道)、§6(渲染红线)、§7(电源·休眠)、
> §10(做新 App 指南 / 桌面玩法省电坑)、§11(必查 MCP 清单),以及 `docs/units/Unit_Gesture.md`
> `docs/units/Unit_RGB.md` `docs/units/_MCU_Firmware_I2C_Units.md`。**PAJ7620U2 的 bank 切换
> 初始化字节序列、读事务形状(能否 repeated-start)、有无"在场未分类"信号——这三件事平台文档都
> 没给到寄存器级细节,实现前必须先查 MCP / datasheet,不许照猜写死(AGENTS.md §1 铁律)。**
>
> 本文体例仿 `apps/peekaboo/SPEC.md`(访客池/相册/庆祝的骨架已实机验证有效,但本卡带**刻意不
> 直接照搬那套骨架**,见 §0/§2 的设计取舍说明)。完工后把定案数值/落地差异按惯例写进
> `README.md`(as-built),更新根 `CLAUDE.md` §1/§12 索引状态列,**本文档保留**(不删)。

---

## 0. 为什么做这张新卡带(而不是改造旧卡带)

平台现有 5 张卡带的输入全部是**接触式**(转旋钮/捏靠近/倾斜机身/推摇杆),`peekaboo` v2 与
`feed_monster` v2(草稿)已经把"从连续传感信号里多挖信息 + 访客池 + 相册收集 + 游行庆祝"这套
打法用了两遍。`docs/units/` 里躺着三份已查好硬件事实但从未落地的外设文档
(Gesture / RGB / PIR),其中 **Gesture(PAJ7620U2)是平台第一个非接触输入**——手不用碰任何
东西,隔空一挥屏幕就有反应,这种"隔空施法"的因果关系对 3~4 岁孩子而言比"转了一下旋钮亮了一下"
新奇得多,值得独立成一张新卡带,而不是塞进已有卡带的一个彩蛋。

配套的 **Unit RGB** 补光灯单元不当"第二条底座灯带"用,而是当孩子**手里握着的道具**:主线创意
是"你手上的灯会跟屏幕里的魔法同步亮"——反馈第一次发生在孩子手里而不只是屏幕/机身上。

## 1. 核心概念:「隔空魔法棒」

屏幕里有一位常驻的小魔法师(圆滚滚造型,呼应平台"chunky 可爱"审美)。孩子举着发光的 RGB
魔法棒单元,在 Gesture 传感器前(5~15cm)挥动手势——**九种内置手势 = 九种法术**,每种法术
都有独立的画面/音效/震动/魔法棒配色。任何挥手动作,哪怕没被精确分类,魔法师身边也会有一丝
"微光回应",绝不会让孩子觉得"我挥了但什么都没发生"。九种法术全部解锁过一次 → 法术书集满 →
"魔法大派对"(九个法术依次回放的小型烟花秀)→ 法术书清空、开新一轮,重复但每次派对回放的
细节(星星数量/位置等)略有变化,保持"重复但不同"。

零失败不变:没有"挥错"这件事,九种手势都对应正向反馈,唯一的区分只是"分类成功"与"没分类出来
但依然有反应"。全程无文字:法术种类靠画面造型+音签+灯色记,进度靠法术书页靠图标记。

## 2. 范围与红线

**新增组件(平台首次)**:
- `components/units/unit_gesture` —— PAJ7620U2 最小驱动。
- `components/units/unit_rgb` —— PORT.B 3×SK6812 补光灯驱动(独立小组件,**不扩展
  `ledstrip_fx`**:`ledstrip_fx` 是底座 10 灯环专属通道,`FUN2_SPEC.md` 已明确把它列为
  "现有 3 特效 + 4 基础模式一个字节不许改"的共享组件;魔法棒拓扑不同(PORT.B/G26,3 像素)、
  语义也不同(贴身道具 vs 机身氛围),混在一起会让两条通道的职责说不清楚。

**组件层零改动**:`audio_fx`(`play_notes` ≤8 音/~400ms)/`haptics`(只用现有
`HAPTIC_HELLO/WAKE/BUMP_LIGHT/BUMP_MED/BUMP_HARD/COLLECT/WIN` 七个既有值,不新增)/
`ledstrip_fx`(底座环境灯只用现有 `LED_BASE_*` + `LED_FX_BUMP/COLLECT/WIN/SWEEP_L2R/
SWEEP_R2L/GATHER/SPREAD/FLASH` 现有全集,不新增);`partitions.csv`/`sdkconfig*` 不碰
(ota_5 偏移 `0xB90000` 已在 `partitions.csv`/`tools/flash_map.md` 里预留,新增槽位登记留到
实现阶段)。

**纪律红线**(违反=返工,与其它 SPEC 一致):
- 所有 LVGL 调用包 `bsp_display_lock(0)…bsp_display_unlock()`。
- **整屏级重绘只发生在开局 / 集满法术书的大派对开场**;单次施法是魔法师周边小容器的动画,
  永不整屏重绘。
- 全部视觉用不透明纯色块(`plain()` 工具),体积变化(如冲天咒的放大/躲猫咒的缩小)走
  `lv_obj_set_size` 这类**不透明矩形改尺寸**(busy_knobs 柱子长高、peekaboo 梦泡泡三档已验证
  这类操作零 alpha 成本,合规),**不做真正的旋转变换**(LVGL 矩阵旋转会带抗锯齿边缘 =
  变相 alpha 混合,红线禁止)——"左/右旋咒"用**挤压式假旋转**(宽度快速收窄再放开,类似
  tilt_maze 的撞墙挤压)而不是真的转个圈。
- `audio_fx_play_notes` 单次 ≤8 音/总时长 ~400ms;本文所有音序已按此设计,勿超。
- 动画打断先 `lv_anim_delete(obj, exec_cb)` 配对掐残留,再瞬移到终态(全平台惯例)。
- 省电语义:手势事件 / 微光兜底都要 `core2_sleep_kick`(§6);法术书装饰动画只在 AWAKE 跑。
- 随机数 `esp_random()`。`game_task` 栈建议从 4096 起,施法状态/法术书/魔法棒特效变多,
  实现时若栈告警可提到 5120,README 记一笔。

**构建**:`idf.py -C apps/magic_wand build`(命令行)。因新增 `components/units/unit_gesture`
与 `components/units/unit_rgb` 是全新组件、不改任何既有组件,无需交叉编译其它 app 验证。烧录
由用户手动(`tools/flash_one.sh magic_wand`,需先在 `tools/flash_map.md` 登记 ota_5 行),
WSL 不烧录。

⚠️ **实现期程序性提醒**:本 SPEC.md 先于代码写成,`apps/magic_wand/` 目录目前只有这一个文件。
`tools/new_app.sh` 遇到目标目录已存在会直接报错退出,实现阶段需要:①把本文件临时移出、跑
`tools/new_app.sh magic_wand` 生成骨架、再把 SPEC.md 移回;或 ②手动照 `tools/new_app.sh`
模板(参照 `apps/feed_monster/CMakeLists.txt` 等既有工程)补齐 `CMakeLists.txt`/`main/`
骨架。两种都行,选省事的一种,不是设计决策。

---

## 3. 状态机

```
[NO_UNIT] ──(Gesture + RGB 至少 Gesture 一者 init 成功)──► [IDLE_READY]
   │(拔线/持续读失败)◄────────────────────────────────────────┘

[IDLE_READY] ──(手势分类成功)──► [CASTING](单次施法编排,~500~800ms 依法术而定)──► [IDLE_READY]
            ──(感应到动静但未分类)──► [SHIMMER](~200ms 微光)──► [IDLE_READY]

跨状态:集满 9 页(任一施法收尾时判定)──► [PARTY](~4s 九法术接力回放 + 大庆祝)──► 法术书清零 → [IDLE_READY]
```

- **CASTING 期间来新的手势事件**:允许打断(零失败=永远响应最新动作),`lv_anim_delete` 配对
  掐掉上一个法术的残留动画,新法术立即起播;**同一种法术**在冷却窗口内(`RECAST_COOLDOWN_MS`)
  重复触发只播音效/震动/魔法棒的**轻量重复**版(不重放整套大动画),避免孩子对着同一手势反复
  挥导致视觉闪烁式重复。
- **PARTY 期间**:手势输入被忽略(不判定、不 kick,派对本身在跑),派对结束才回 `IDLE_READY`
  按当前感应状态重新判定。
- **省电三态**(`core2_sleep`)与此正交:非 AWAKE 冻结施法/微光/法术书装饰计时;NAP 中若持续
  收到 Gesture 事件仍可唤醒(见 §6);DEEP 断 5V 后 Gesture/RGB 单元复位,唤醒 `unit_attach`
  重发初始化。

## 4. 传感层:9 种手势 + 「微光回应」兜底

PAJ7620U2 只吐**已分类**的离散手势事件(上/下/左/右/前/后/顺时针/逆时针/挥手共 9 种),**不是**
连续测距(勿类比超声波/DLight 那种可以挖速度/时长的连续信号)。5~15cm 的有效识别窗口对
3~4 岁孩子的手部控制来说偏窄,**真实命中率未知,是本卡带最大的实机风险**(见 §13)。

零失败对策——**任何手在感应范围内的动静都要有反应**,不能让"挥了但没识别出来"变成沉默:

1. **分类成功** → 直接进对应法术(§5 表),<100ms 内起播。
2. **感应到物体/动静但分类不出手势**(需要 datasheet 确认 PAJ7620U2 是否有独立的"物体存在"
   中断/状态位,不同于九种手势结果寄存器;`Unit_Gesture.md` 未写到这层细节)→ 触发
   **SHIMMER**:魔法师手边一粒微光点单闪一下(6×6px 不透明块,opa 二值切换,非渐变)+ 魔法棒
   单像素轻闪一下,**不播大动画、不占音频队列大头**(极短一声,~20ms/低音量),让孩子感觉
   "它看到我了"。
   - **若 datasheet 确认没有这类中间信号**(PAJ7620U2 只吐 9 种手势 + 无手势):SHIMMER
     退化为固定低频"存活性 ping"——不管有没有手势,每 `SHIMMER_IDLE_MS`(如 2~3s)播一次极轻的
     魔法师"待机眨眼+棒尖微光",营造"魔法一直在待命"的氛围,但不再与真实手部动作强绑定(弱化
     方案,SPEC 里先记这个退路,具体选哪个等 datasheet 核实后定)。
3. **去抖**:同一手势短时间(`RECAST_COOLDOWN_MS`,建议 400~600ms,覆盖一次 CASTING 动画时长)
   内重复识别到,不重放整套动画,只给轻量反馈(§3);不同手势随时可以打断当前动画。

## 5. 九种法术表(核心内容表,逐项落地)

| 手势 | 法术名 | 画面(魔法师/场景) | 音签(hz,ms,amp,≤8 音) | 触觉 | 魔法棒 RGB(3px) | 底座灯带 |
|---|---|---|---|---|---|---|
| UP | 长高咒 Grow | 魔法师脚边"豆茎"3 级离散长高(矮→中→高,不透明矩形改高度,每级 ~120ms)+ 顶端一朵花瞬时显示 | {392,60,50}{523,60,55}{659,90,60} 上行 | `HAPTIC_COLLECT` | 绿色从棒根向棒尖 3 级渐显(离散,非渐变) | `LED_FX_GATHER` |
| DOWN | 星雨咒 Star-rain | 5~6 颗星从顶部落下,落地各弹一下(复用 peekaboo `BURST_COUNT`/`STAR_COUNT` 同量级) | {880,50,45}{784,50,40}{659,50,40}{523,60,45} 下行 | `HAPTIC_BUMP_LIGHT` | 蓝白由棒尖向棒根依次单闪(3 步) | `LED_FX_SPREAD` |
| LEFT | 左旋咒 Spin-L | 魔法师身体宽度快速收窄再放开(挤压假旋转,~200ms)+ 斗篷角向左甩一下 | {659,45,50}{523,45,45} | `HAPTIC_BUMP_LIGHT` | 3 像素从右向左单向扫过一次 | `LED_FX_SWEEP_R2L` |
| RIGHT | 右旋咒 Spin-R | 同 LEFT 镜像,斗篷向右甩 | {523,45,50}{659,45,45} | `HAPTIC_BUMP_LIGHT` | 3 像素从左向右单向扫过一次 | `LED_FX_SWEEP_L2R` |
| FORWARD | 冲天咒 Zoom-burst | 魔法师整体放大一档(~130%,`lv_obj_set_size` 快进快出,~150ms)+ 四条放射短线一次性闪现 | {1046,50,60}{1318,70,65} | `HAPTIC_BUMP_MED` | 3 像素同时炸亮白一下 | `LED_FX_FLASH` |
| BACKWARD | 躲猫咒 Shrink-pop | 魔法师缩小一档并停顿 `PEEK_HOLD_MS`(~300ms),随后弹回原尺寸(呼应 peekaboo 揭晓手感) | 停顿后 {330,60,40}{659,90,55}"啵" | `HAPTIC_HELLO` | 先暗后瞬间回亮(呼吸感,离散两态非渐变) | `LED_FX_BUMP` |
| CLOCKWISE | 旋风咒(顺) Whirl-CW | 4~6 颗小光点在魔法师周围离散预设位之间按顺时针步进(每步 ~80ms,非连续三角函数逐帧算,固定 8 方位查表) | {392,35,40}{440,35,40}{523,35,45}{587,35,45} 短促上行琶音 | `HAPTIC_COLLECT` | 3 像素顺时针方向快速 chase 一圈 | `LED_FX_GATHER` |
| COUNTER-CLOCKWISE | 旋风咒(逆) Whirl-CCW | 同 CW 反方向 | 同 CW 音序倒序 | `HAPTIC_COLLECT` | 3 像素逆时针 chase 一圈 | `LED_FX_SPREAD` |
| WAVE | 你好咒 Hello-confetti | 彩纸迸发(复用现有 burst 手法,限量 6~8 片)+ 魔法师挥手回应(手臂小方块左右摆 2 次) | {659,50,55}{784,50,55}{880,60,60}{1047,80,65} 上行琶音 | `HAPTIC_WIN` | 3 像素轮流跑一次彩虹(hue 分段,离散 3 色非渐变) | `LED_FX_WIN` |
| (无手势但有动静) | 微光回应 Shimmer | 魔法师手边 6×6 微光点单闪 | {1400,20,25} 极轻单音 | 无 | 单像素轻闪 1 次 | 无(不打断底座常态) |

**首遇解锁**额外叠加:该法术在本轮法术书里首次点亮时,在音序尾部追加一声 `SND_COLLECT` 风格
清脆音(复用现有 `SND_COLLECT`)+ `HAPTIC_COLLECT`,并点亮对应书页图标(§6),与"已点亮过的
法术再次施放"区分开,靠音频队列自然串行(<8 音/次限制下可能截断次要音,可接受,同其它 SPEC 惯例)。

## 6. 法术书(进度系统)——刻意不用「访客池」的设计说明

`peekaboo`/`feed_monster` 的输入信号本身是二值/近似连续的(捂/松、近/远),"这次来的是谁"是
人为引入的新鲜感变量,所以配一个**轮换访客池**制造变化。Gesture 传感器不同:硬件本身自带 **9
类离散分类**,新鲜感天然就藏在"我发现了第几种法术"这个轴上——如果照搬访客池,等于在一个已经
有内在多样性的输入上再叠一层人为多样性,会显得臃肿且分散注意力(孩子分不清"这次的反应不同是
因为我做的手势不同,还是运气抽到了不同角色")。因此采用**法术书**:

- 顶部一排 **9 枚** 18×18 图标位(仿 peekaboo 相册排布,`x=8` 起、间距 4、`y=6`),每枚对应
  §5 表一种法术,未解锁 = 灰底剪影,首次施放成功即点亮为该法术的主题色/图标。
- 集齐 9 页 → `PARTY`:9 个法术按 §5 表顺序**快速接力回放**(每个 ~250~350ms 压缩版,略去
  停顿类细节如躲猫咒的等待)+ 魔法师原地欢跳 + 彩纸迸发 + `SND_WIN` + `HAPTIC_WIN` +
  `LED_FX_WIN`;收场法术书清空,回 `IDLE_READY`。
- **第 10 页「隐藏法术」(P3,彩蛋,不占书页格子、不参与集齐判定)**:`COMBO_WINDOW_MS`
  (如 2.5s)内连续识别到 **3 次不同的**手势(连击)→ 触发"大魔法"——魔法师头顶炸开双层
  烟花(复用 §5 WAVE 的迸发框架放大规模)+ 五音琶音 + `HAPTIC_WIN` + `LED_FX_WIN`,纯 jackpot
  惊喜,不影响法术书进度,仿 peekaboo `RAINBOW`/feed_monster `RAINBOWFISH` 的稀客彩蛋定位。
- **重复施放已解锁法术**:效果照常播(§5 表全套反馈),只是不再触发"首遇"追加音效/书页动画,
  保证"重复但不完全一样"——每次施放的粒子数量/位置在小范围内随机(`esp_random()`),不是
  逐帧完全相同的回放。

## 7. 反馈优先级 / 冲突矩阵(结算顺序)

1. **PARTY 最高**:期间手势输入不判定(§3);进入 PARTY 时若正在 CASTING/SHIMMER,直接
   `lv_anim_delete` 掐掉转场。
2. **连击彩蛋(§6 第 10 页)**判定在**手势分类成功**之后、**法术书首遇点亮**之前:连击达成时,
   本次识别到的第 3 个手势仍正常触发自己的 §5 法术(不吞掉),彩蛋是**额外叠加**的一层反馈,
   不是替代。
3. **微光兜底(SHIMMER)**只在没有分类成功、且判定"确有动静"时触发;若 datasheet 确认无此
   中间信号,退化为§4 的固定低频 ping,不与手势判定抢帧。
4. **省电**:CASTING/SHIMMER/法术书装饰动画只在 AWAKE 跑;NAP/DEEP 期间手势不判定,唤醒后
   状态回 `IDLE_READY` 重新开始判定(不续用休眠前的冷却计时,仿 busy_knobs 小鸟先例)。
5. **拔线**(Gesture 或 RGB 任一 I2C/总线连续读失败达阈值):对应能力静默降级
   ——Gesture 掉线整卡回 `NO_UNIT` 提示卡;RGB 掉线只损失魔法棒这一条反馈通道,屏幕/音/震
   三通道照常(§2 已允许 v1 不强依赖 RGB,见 §13)。

## 8. 电源 / 省电集成

- **kick 时机**:手势分类成功 **或** SHIMMER 触发(感应到动静但未分类)都算"有人在玩",
  `core2_sleep_kick`(仿 §10 桌面玩法省电坑,与 busy_knobs 转旋钮 / feed_monster 挥手 / chain_lab
  推摇杆同一套路)。
- IMU 仍每帧喂 `core2_sleep_feed`,走机身静止判据的三级休眠(NAP/DEEP)与手势 kick **相互独立
  正交**——孩子可能整台机器搁桌上不动、只对着感应器隔空挥手玩,此时机身层面的"静止"判据不该
  单独把游戏打盹掉,靠手势 kick 顶住。
- **NAP**:5V 仍在,Gesture/RGB 继续工作,施法可直接唤醒。
- **DEEP**:切 M-Bus 5V → Gesture/RGB 单元断电复位 → 唤醒后需要重新走一遍 Gesture 的 bank
  初始化序列(仿 `unit_dlight_init` 的"可重复调用、每次重发上电+选模式命令"写法)+ RGB
  单元重新 attach(`unit_rgb` 组件同样设计成可重复 init)。
- **无单元容错**:仿 `unit_dlight`/`unit_ultrasonic` 先例——没插 Gesture = 无字提示卡(魔法棒
  图标+插头)+ `ATTACH_RETRY_MS` 周期重试;插上即"你好"接管(`SND_HELLO`+`HAPTIC_HELLO`)。
  RGB 单元缺席不阻塞整卡启动(见 §13 P1 范围)。

## 9. 新组件设计 + MCP/datasheet 核查清单

### 9.1 `components/units/unit_gesture`

结构仿 `components/units/unit_dlight`(最小驱动 + 可重复 init 支持热插拔)：

```c
// unit_gesture.h(草案,函数签名待实现时按实际情况微调)
esp_err_t unit_gesture_init(i2c_master_bus_handle_t bus, uint8_t addr);  // addr=0 → 默认 0x73
esp_err_t unit_gesture_read(gesture_event_t *out);  // 无手势时返回何种语义待定(见下)
```

**实现前必须查证、不许照猜写死的三件事(逐条列出留痕)**：

1. **bank 切换初始化字节序列**:`docs/units/Unit_Gesture.md` 只写了"寄存器分 bank 0/1,上电需
   做初始化序列(选 bank、写手势检测寄存器组)",没有给出具体寄存器地址/数值。必须查
   PAJ7620U2 datasheet 或核实过的厂商参考驱动(如 Sparkfun/Seeed/M5Stack 官方库源码,同
   `chain_lab` 当年"协议从官方库逐字节核实"的先例)取得逐字节序列,写代码前先在实现记录里
   写下 "Confirmed via ...",严禁编造寄存器值。
2. **读事务形状(能否 repeated-start 组合读)**:PAJ7620U2 是专用手势识别 ASIC(片上有 IR 光学
   + 手势分类算法,不是像 8Encoder 那样跑通用应用固件的 STM32),按 `docs/units/
   _MCU_Firmware_I2C_Units.md` §5 的划分标准(硬件寄存器芯片 vs MCU 固件从机),倾向于前者、
   可安全组合读,但 `Unit_Gesture.md` 没有明确写。**实现前查 datasheet 的 I2C 时序图确认**;
   拿不准就按该文档规则 1 的保守写法(写寄存器号+STOP,再单独发起读,两笔独立事务)——零
   成本、不会踩组合读钳死总线的坑。
3. **有无"物体存在但未分类"的中间信号**(供 §4 SHIMMER 兜底用):`Unit_Gesture.md` 未提及。
   查 datasheet 的中断/状态寄存器区(PAJ7620U2 有中断输出能力,可能存在"检测到物体"与"手势
   已识别"两级状态位)。若确认存在,SHIMMER 直接挂这个信号;若确认不存在,按 §4 退化方案
   (固定低频 ping)实现,并在 README as-built 里记一笔"已确认无此信号,采用退化方案"。

**读写协议红线复用**:即使确认可组合读,`unit_ultrasonic`/`unit_dlight` 的先例是"分开写更稳、
与官方库一致",本组件遇拿不准的地方一律走保守两笔事务写法,不为省一次 I2C 事务冒风险。

### 9.2 `components/units/unit_rgb`

结构类比"迷你版 `ledstrip_fx`",但**独立实现、不共享代码**(职责边界见 §2)：

```c
// unit_rgb.h(草案)
esp_err_t unit_rgb_init(void);               // G26(BSP_PIN_PORTB_DAC),led_strip+RMT,长度=3×单元数
void      unit_rgb_set_max_brightness(uint8_t max);
void      unit_rgb_trigger(wand_fx_t fx);     // 小词汇表:PULSE_LEVEL(n) / SWEEP_DIR / CHASE_DIR / FLASH / DIM_POP / RAINBOW_STEP / OFF
```

**实现前查证**:RMT 配置(时钟源/分辨率/内存块数)虽然 `docs/units/Unit_RGB.md` 已给出
SK6812/GRB/800kHz 量级的高层事实,具体 `led_strip_config_t`/RMT backend 参数仍按 CLAUDE.md
§11 惯例查 `espressif/led_strip` MCP 组件文档确认(**不能直接照抄 `ledstrip_fx` 那份现成配置
——G26 是不同 GPIO、需要独立 RMT 通道,不是简单复制粘贴能保证正确的**)。

**供电**:PORT.B 与 PORT.A 同为 M-Bus 5V(EXTEN)供电(§7 事实),`core2_board_init` 理论上
一次性使能覆盖两个口,但这是平台头一次**两个外接单元同口不同总线同时在线**,实现前在
`docs/platform/M5GO_Bottom2.md` 里确认一下 EXTEN 是否真的同时覆盖 PORT.A 与 PORT.B(大概率
是同一路 M-Bus 5V 分裂给两个口,但别想当然,查一遍成本很低)。

## 10. 文件布局

```
apps/magic_wand/
  main/
    app_main.c        app_slot_return_to_factory() 首行(平台惯例)
    magic_wand.c/.h    主任务:轮询 Gesture、分派法术、省电挂载、单元容错(仿其它 app 的
                       game_task 30~60Hz 结构)
    spellbook.c/.h     9(+1 隐藏)法术数据表(§5)、书页解锁状态、集齐判定、派对编排(§6)
    wizard.c/.h        魔法师精灵(idle/施法各态)、场景静态层、旋风咒/星雨咒等特效精灵
    wand_fx.c/.h       法术事件 → unit_rgb 特效调用的薄封装(职责与 wizard.c 的屏幕逻辑分开,
                       仿 peekaboo `scene.c`/`visitor.c` 的拆分方式)
    tuning.h           §11 新增常量集中一处

components/units/unit_gesture/
  include/unit_gesture.h
  unit_gesture.c        PAJ7620U2 驱动(§9.1)

components/units/unit_rgb/
  include/unit_rgb.h
  unit_rgb.c             PORT.B 3×SK6812 驱动(§9.2)
```

## 11. `tuning.h` 新增常量(集中一处,默认值待实机标定)

```c
// ── 手势判定 / 微光兜底 ──────────────────────────────────────────────
#define GESTURE_POLL_MS        30     // 轮询周期(需查证 PAJ7620U2 建议轮询频率,120Hz 上限内取够用值)
#define RECAST_COOLDOWN_MS     500    // 同一手势重复触发的冷却(覆盖一次 CASTING 动画)
#define SHIMMER_IDLE_MS        2500   // 退化方案:固定低频存活性 ping 间隔(若无"在场未分类"信号)
#define ATTACH_RETRY_MS        2000   // 无单元时的重试周期(仿其它 app 惯例)
#define ERR_STREAK_LOST        20     // 连续读失败判定拔线(仿 feed_monster/chain_lab 惯例)

// ── 法术编排 ─────────────────────────────────────────────────────────
#define PEEK_HOLD_MS           300    // 躲猫咒:缩小停顿时长
#define WHIRL_STEP_MS           80    // 旋风咒:粒子步进间隔(8 方位查表)
#define ZOOM_SCALE_PCT          130   // 冲天咒:放大比例

// ── 法术书 / 派对 ────────────────────────────────────────────────────
#define SPELLBOOK_SIZE          9
#define PARTY_STEP_MS           300   // 派对接力回放:每个法术压缩时长
#define COMBO_WINDOW_MS         2500  // 隐藏法术:连击判定窗口
#define COMBO_NEEDED             3     // 连击判定:窗口内需要几种不同手势

// ── 魔法棒 RGB ───────────────────────────────────────────────────────
#define WAND_MAX_BRIGHTNESS      60    // 贴身道具,亮度上限可比底座(48)略高但仍克制
```

## 12. 渲染预算合规(根 `CLAUDE.md` §6)

- **整屏重绘时刻**:开局、集满法术书大派对开场——事件级,永不每帧。
- **单次施法常态**:魔法师本体(~40×50px)尺寸/位置变化 + 少量特效精灵(星雨 5~6 颗
  ~8×8px、旋风粒子 4~6 颗 ~6×6px、彩纸 6~8 片同量级)——单次施法动画峰值像素量与
  peekaboo 访客出场(~1,760px 移动)同量级,远低于 §6.2 的 ~15,000px/帧预算。
- **法术书装饰**:9 枚 18×18 图标只在解锁瞬间切换(灰→主题色,`plain()` 换色非动画,零
  持续开销);未解锁/已解锁之间无逐帧过渡。
- **派对**:走 §6.5 庆祝档(15~20fps 可接受),9 个法术接力回放但单个已压缩到 ~300ms、
  彩纸量已限量,合计不超其它 app 派对场景的既有规模。
- 全部不透明色块;体积变化(缩放)走 `lv_obj_set_size` 而非矩阵旋转/alpha,无 banding
  风险(§6.3);魔法棒特效完全在屏外(物理 RGB 单元),不占用任何 SPI/渲染预算。

## 13. 分阶段优先级(裁剪范围用)

- **P1(核心,建议必做,也是风险最大的一步)**:§4/§5 基础 9 手势映射 + 微光兜底(**仅屏幕+
  音+震三通道,魔法棒 RGB 暂不接**)。此阶段唯一目的是**实机验证 PAJ7620U2 对 3~4 岁孩子
  隔空手势的真实命中率**——命中率不理想的话,后续法术书/派对投入需要重新评估手势判定阈值
  甚至范围引导设计(比如加一个"举到这个距离"的视觉提示),不建议跳过这一步直接做全量。
- **P2**:§6 法术书(9 页解锁+集齐判定+派对)。
- **P3**:§6 第 10 页隐藏法术连击彩蛋。
- **P4**:接入 `unit_rgb` 魔法棒联动(§5 表的 RGB 列全部生效)——独立组件、独立失败面,
  作为在屏幕+音+震已经验证好玩之后的加分项,不阻塞前面的验收。

## 14. 验收清单

**构建**:
- [ ] `idf.py -C apps/magic_wand build` 通过;记录 bin 大小与槽内余量(ota_5 = 2MB 同其它槽)。

**实机点检**(用户烧录后逐条,P1 先做,不要求一次性全量):
- [ ] 九种手势各自能触发对应画面/音/震,无长文字、无频闪。
- [ ] 挥手但未被分类时,能看到/听到"微光回应"(不是沉默)。
- [ ] 同一手势连续挥不会不断重放整套大动画(冷却生效),换手势能立刻打断。
- [ ] (P2)法术书 9 页随施法逐个点亮;集齐后派对回放 9 个法术并清零重开一轮。
- [ ] (P3)短时间挥出 3 种不同手势触发隐藏法术彩蛋,不占书页格。
- [ ] (P4)魔法棒 RGB 与屏幕/音效同步,亮度舒适不刺眼。
- [ ] 打盹/深度省电如常进出;DEEP 唤醒后 Gesture/RGB 正确重新初始化并接管。
- [ ] 拔线(Gesture)→ 提示卡如常,插回即恢复;RGB 单独拔线不影响其余三通道。

**收尾**:
- [ ] README 写 as-built(定案数值 + 落地差异,尤其 §9 三项 datasheet 核查的实际结论)。
- [ ] 根 `CLAUDE.md` §1/§12 索引新增 magic_wand 一行;`tools/flash_map.md` 补 ota_5 行。
- [ ] 本 SPEC.md 保留(不删)。
