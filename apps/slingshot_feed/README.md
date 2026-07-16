# slingshot_feed — 弹弓喂喂(as-built)

> 本文件是 **slingshot_feed 应用的竣工记录**;玩法规格见 `SPEC.md`(施工图,完工后保留)。
> 跨应用平台事实(Chain 链路/桌面玩法省电坑)见根平台手册 `CLAUDE.md`(§10 做新 App 指南、
> §7 电源·休眠、§11 坑位)与 `apps/chain_lab/README.md` + `apps/busy_bus/README.md`
> (Chain 摇杆链路 as-built 源头)。
> **槽位候选竞 ota_5,尚未立项确认** —— 本轮**未**改 `partitions.csv` / `tools/flash_map.md` /
> `tools/flash_one.sh` / launcher 图标 / 根 `CLAUDE.md` §1/§12 索引,按任务要求留到立项确认后再登记。

## 现状(2026-07-15)

P1+P2+P3 一次性写完(busy_bus 先例),`idf.py -C apps/slingshot_feed build` **全新 fullclean
编译零警告零错误**通过(见文末编译记录)。**本机当前没有连接设备,尚未单刷上机,SPEC §12
的实机点检清单全部待办**——尤其"拉-放弹射"手感(§5.1 三变体)是全 SPEC 最大未知,必须先
上机验证,不进一步打磨。

## 改进批次(2026-07-16):居中弹弓 + 会期待的动物 + 好朋友聚集 + 有性格物种

实机确认"能玩且好玩"后的第一轮加深度(用户定的方向:**加深度、别加规则**)。四项一起做、
编译通过(`0xa2000` 字节,58% 余量),**全部待单刷实机点检**:

1. **弹弓移到屏幕左右正中(地基)**:`SLING_ANCHOR_X` 42→160、对称 Y 叉,现在**左上/右上两个
   方向都能发射**(拉杆朝哪、果子朝反向飞)。瞄准从"右上单扇区"扩成左右一整个半圆——SPEC §5.5
   已指出力度轴会塌缩成常数、真技能只剩角度,这一步把"单轴指方向"救成"半圆扫瞄",变化量翻倍。
   `meadow.c` 位置表重排为左右对称 7 点(近/低→中→高 两侧各一套 + 顶心),`validate_spots` 用
   `fabsf(dx)` 天生对称、判据未动,7 点全通过(最紧 top_center 余量 21.6px);装饰树/云跟着重摆。
2. **改进 A — 会期待的动物**(`sling_game.c` render_all + update_life/update_aim_lock,`feedback` 加
   EV_LOCK/EV_CALL):眼睛跟随兜/飞行果子、身体朝瞄准方向微倾、眨眼、AIM 呼吸起伏;**预览弧任一点
   靠近嘴 = "瞄准锁定" → 眼睛放大 + 嘴上落点光圈 + 一声节流的"来嘛~"**(把 SPEC §11.2 预授权的
   落点光圈+自教从兜底升级成核心:孩子扫过去动物就有反应,瞄准=逗一只馋动物);久等无人喂"还要~"
   催促 + 小跳。逐帧偏移全在 render_all 一处落 LVGL(单锁,不新增失效区)。
3. **改进 B — 看得见的好朋友聚集**(`meadow.c` 朋友池 + `sling_game.c`):喂饱的动物蹦到草地边攒成
   一排(身体色=所喂物种),1→2→3→4 到齐才**派对群跳**(补回 README 旧"偏差 3"砍掉的群跳),然后清
   排换新批。把隐形的 `s_animal_fed_count` 配额变成看得见、留得下的收集——此前只有失败(miss 小花)
   留痕,成功头一回有持久痕迹。朋友池仿 miss 小花:有界(ANIMAL_QUOTA)、自带锁(不重蹈漏锁看门狗)。
4. **改进 C — 有性格的物种**(`sprites.c` LOOKS + `feedback.c` VOICE):3→5 种(熊/鸡/蛙/兔/猫),
   每种**独立体型/五官/特征件/配色**(不再共享同一套眼嘴),吃/长大各自嗓音基频。给"下一只会是谁"以
   新鲜感。

> 派对期当前动物仍在原位小跳、同时它的朋友化身在底排群跳(单动物模型下的取舍,读作"大家一起庆祝",
> 未引入多实例精灵管理)。**新增待实机点检**见文末清单末尾。

## 架构:传输层 / 静态层 / 精灵 / 游戏层 / 反馈(仿 busy_bus/chain_lab 拆分)

```
sling_link.c/.h   传输/绑定层:scan_bus/poll_joy/node_rgb/hue2rgb 形状照抄 bus_link.c,
                  收窄为 joystick-only(encoder 若挂在链上会被扫到但直接跳过,不冲突)。
                  50Hz(★非 20Hz 惯例,见下"关键偏差")game_task 里跑 core2_sleep_feed +
                  深度省电重扫,AWAKE 时调 sling_game_tick()。
meadow.c/.h       静态层:天空/草坡两块扁平色带 + 云×2 树×2 篱笆装饰 + 弹弓 Y 叉(一次性
                  摆角度,运行时不再旋转)。动物位置表(6 个手工坐标)+ 加载校验
                  (SPEC §5.5:真实可达包络 safety parabola,y ≤ v²/(2G) − G·x²/(2v²) 留
                  ~10% 距离余量,不满足自动剔除并 ESP_LOGW)。
                  miss 小花对象池(MISS_FLOWER_MAX=6)落定后一次性显示,零每帧成本。
sprites.c/.h      皮筋(兜 + 两股各 3 个示意小圆点,不做运行时旋转贴图)+ 果子(色相着色,
                  每次重装填换一次颜色)+ 动物造型表(熊/小鸡/青蛙,仿 chain_lab PRIZE_LOOK
                  的身体圆+特征件+双眼+嘴组合式打法,张嘴/闭嘴两态)。
sling_game.c/.h   状态机 AIM⇄FLIGHT⇄EAT/MISS⇄GROW⇄PARTY(SPEC §4)+ 拉-放检测(§5.1,
                  历史窗峰值取力度、松手前最后 1~2 稳定采样取角度)+ 弹道积分/预览共用
                  compute_v0()(§5.2 不变式)+ 命中/喂饱/长大/miss/派对(§5.3~§6)。
                  逻辑坐标存影子变量,LVGL 只在 render_all() 一处统一落(单把锁,busy_bus
                  bus_game.c 同款动画所有权纪律);dress/mouth/cheeks 等低频装扮调用各自
                  自带锁(chain_lab held_prize_show 一类先例),不嵌套在 render_all 内。
feedback.c/.h     六类事件 → 音频/触觉/灯带/摇杆节点 RGB 四条共享通道(仿 busy_bus/tilt_maze
                  队列+后台任务形状);屏幕这条由 sling_game/meadow/sprites 直接管。
tuning.h          SPEC §9 全部常量原样落地 + 链路/几何/节奏等实现细节常量(逐条注释区分)。
```

## 与 SPEC 的偏差 + 理由

1. **`POLL_HZ=50`,非 busy_bus/chain_lab 的 20Hz 惯例**。SPEC §4/§5.2 明确要求果子弹道
   "40~60fps"才够顺滑,20ms 周期(50Hz)落在区间内,且与轮询/渲染共用同一 game_task(与
   busy_bus/chain_lab 相同的单任务模型,没有另起独立渲染任务)。chain_bus 单次请求超时
   40ms(`unit_chain_joystick.c: REQ_TIMEOUT_MS`)、成功往返通常远小于此,20ms 帧预算内一次
   ADC 请求 + 一次按键请求预期够用,但**吞吐是否真扛得住 50Hz 未经实机验证**,若实机发现
   摇杆读数偶发超时/卡顿,先把 `POLL_HZ` 调回 20~30 排查,再看是否要拆分"轮询"与"渲染"
   两个频率(SPEC 没要求这么做,按最简单方案先试)。
2. **(已解决,2026-07-15)动物位置校验公式:SPEC 已修正 + 代码已跟改为真实包络**。
   最初实现时发现 SPEC §5.5 给的"距锚点 ≤0.9×LAUNCH_POWER²/GRAVITY"是各方向同一半径的
   简化圆形判据,但固定初速抛物线的真实可达域(safety parabola)在偏离水平方向(尤其偏
   上方)时半径明显更小——用 `LAUNCH_POWER=420 GRAVITY=520` 实算过,一个"雲朵"候选点
   (距锚点 259px,满足简化公式)在真实物理下其实**够不到**(该方向理论 x_max≈157px,
   候选点需要 158~208px),对高处目标会高估约一倍可达性。该发现已反馈进 SPEC,**§5.5
   现已改判据为真实可达包络**(以弹弓锚点为原点、y 向上:`y ≤ v²/(2G) − G·x²/(2v²)`,
   v=LAUNCH_POWER,留 ~10% 距离余量),并明令禁止简化圆形判据。`meadow.c: validate_spots`
   已同步改用该真实包络公式(不再是"代码按 SPEC 原话实现简化公式"的过渡状态),偏差已
   消除。全新 fullclean 编译复核通过,原有 6 个手工位置全部仍通过校验(`cloud_right`
   余量最紧,约 2px;其余 25~180px 不等)。
3. **PARTY"动物群跳"简化为单只动物的欢庆小跳**。SPEC §6 反馈矩阵写"全场彩纸 + 动物群跳",
   但本作是单动物在场模型(喂饱→长大→换下一只,不保留已喂饱动物的精灵实例),没有"一群"
   动物可跳。派对态改为:当前动物持续小跳(复用 GROW 的 sin 弹跳曲线)+ 彩纸 + 摇杆节点
   彩虹,零成本延续单动物模型,不引入多实例管理的复杂度(P3 打磨范围内的取舍,若要真正的
   "群跳"需要保留一批动物精灵不销毁,超出本轮 SPEC 优先级)。
4. **皮筋(rubber band)用两股各 3 个示意小圆点表达,不是一条连续的线**。SPEC §5/§8 用词
   "皮筋"未强制具体画法;考虑到 LVGL 在这个 BSP/版本组合下没有被本仓库任何既有代码验证过
   的"运行时旋转细线"画法(`lv_line` 无先例,而对象旋转仅在 tilt_maze/busy_bus 里用于**一次性**
   静态摆角度,SPEC §6 渲染红线明确"不做运行时旋转贴图"),改用与预览弧一致的"小圆点沿两
   条直线插值"画法——皮筋兜(pouch)+ 两股各 `BAND_DOTS=3` 个圆点,拉动时逐帧重算位置
   (纯坐标插值,零旋转/零 alpha),视觉上仍读作"皮筋被拉伸"。
5. **动物造型表比 SPEC 提到的 `PRIZE_LOOK` 更简化**:熊/小鸡/青蛙三种共享同一套眼睛/嘴巴
   几何(`sprites.c: FACE_ELX/ERX/EY/MX/MY/MW`),只有耳朵/冠/斑纹特征件与配色区分物种,
   不是每种都独立走查一遍五官坐标。这是给定时间预算内的实用主义简化,P3"打磨"阶段如果
   实机观察到某个物种五官别扭,再单独调那个物种的坐标。

## `tuning.h` 里 SPEC §9 之外新增的实现常量(非偏差,纯粹是编译需要)

`FLIGHT_MAX_MS`(飞行时间封顶)、`GROUND_Y_PX`(落地判定线)、`EAT_MS`/`MISS_MS`(短状态
时长)、`SLING_ANCHOR_*`/`FORK_TIP_*`/`FORK_HANDLE_BOTTOM_Y`/`PULL_VISUAL_PX`/`BAND_DOTS`
(弹弓几何)、`ANIMAL_*`/`MOUTH_*`(动物造型尺寸)、`FRUIT_SZ`、`CONFETTI_N`——busy_bus
`tuning.h` 同款纪律(§9 骨架逐条标注"非 SPEC §9",其余是几何/节奏细节)。

## 实现中发现的 SPEC 疑点(供后续参考)

- **(已解决)§5.5 校验公式的方向性问题**(详见上面"偏差 2"):简化圆形判据在不同方向的
  实际可达性差异很大,尤其目标点偏离锚点正上方时误差最明显,已实算发现误判点并反馈进
  SPEC——§5.5 现已改判据为真实包络公式,`meadow.c: validate_spots` 已跟改,不再是待办疑点。
- **§8 文件布局里"弹弓"两次出现**(meadow.c 的"静态层...弹弓"与 sprites.c 的"弹弓/皮筋"):
  已按"meadow.c 画静态的 Y 叉、sprites.c 提供动态的兜/皮筋"分工实现,理解为描述性重叠而非
  真正的职责冲突,供确认。
- **§4 状态机图里的 `ROUND_SETUP` 未单独建模为一个 tick 态**,而是实现为一次性调用的
  `reload_same_animal()`/`reload_new_animal()` 函数(busy_bus `start_round()` 同款处理,
  该函数执行完立即把状态设回 `AIM`,不占一帧)。行为上与 SPEC 状态图等价,只是代码组织
  上没有一个真正会被 `sling_game_tick()` switch 到的 `SG_ROUND_SETUP` 分支。

## SPEC §12 实机点检清单(原样列入,全部待单刷实机验证)

- [ ] 拉弓有预览弧、拉多远弧多远;松手发射跟手(或 Z 发射),节点 RGB 蓄力亮。
- [ ] **预览弧与实际弹道一致**:满拉后横扫瞄准再松手,飞行方向 = 松手前预览方向(§5.1 不变式)。
- [ ] 命中容差够宽,吃掉"呀姆"+ 音阶;miss 落地变小花 + 动物"还要",无负面音色;小花到上限/换批清场。
- [ ] 喂饱度到阈值动物长大、换下一只;喂够批触发派对后换新一批。
- [ ] 一次一颗在飞,飞行中门控发射不二次触发;发射后无幻影蓄力/预览闪烁(`FIRE_LOCKOUT_MS`);
      门控期拉动空皮筋仍跟手。
- [ ] 打盹/深度省电进出正常;DEEP 唤醒重扫接管、弹弓复位、不吞已喂饱进度。
- [ ] 拔线提示卡/插回恢复;仅插 joystick(无 encoder)链上也正常玩。

**新增待观察项**(实现引入,SPEC 原文未列):
- [ ] `POLL_HZ=50` 下 chain_bus 轮询是否稳定(无异常超时/掉帧),若不稳按上面"偏差 1"降频排查。
- [ ] 动物位置表(2026-07-16 改成居中弹弓的 7 点左右对称:近/低→中→高 两侧各一 + 顶心)真机手感
      是否合理。真实包络复核最紧 top_center 余量 21.6px,其余 33~132px。若某点上机打不到,优先把
      该点往锚点方向挪几像素,而非放宽判据。

**2026-07-16 改进批次待实机点检**:
- [ ] 弹弓居中后**左右两个方向都能发射**;拉杆朝一侧、果子朝反向飞;两侧动物都够得到、瞄准都跟手。
- [ ] 改进 A:动物眼睛跟兜/飞行果子、身体微倾、会眨眼/呼吸;**瞄准弧对准嘴时眼睛放大 + 落点光圈亮 +
      一声"来嘛~"**(节流不刷屏);久等"还要~"催促 + 小跳。确认"落点光圈"确实帮孩子更快学会瞄准。
- [ ] 改进 B:喂饱一只 → 草地边攒一个好朋友(同色);凑 4 只 → **派对群跳**(整排 + 当前动物一起跳)→
      清排换新批。省电唤醒后好朋友进度不清(SPEC §10 不吞进度)。
- [ ] 改进 C:5 种动物(熊/鸡/蛙/兔/猫)轮廓/配色各不同、叫声各异,"下一只是谁"有新鲜感。
- [ ] 回归:改进都没破坏零失败/发射/看门狗(尤其新加的好朋友池、落点光圈都自带/在锁内动 LVGL)。

## 里程碑(SPEC §12)

- [x] P1 弹射手感底盘(草地静态层 + 弹弓 + 固定动物位置表 + 拉-放发射 + 抛物线预览 + 弹道
      飞行 + 命中吃/落地 miss)—— 代码完成,**实机验收未做**。
- [x] P2 喂饱 + 收集闭环(喂饱度/音阶 + 长大 + 换动物 + 派对 + 换批 + miss 小花)—— 代码完成。
- [x] P3 打磨(果子/动物造型、节点 RGB 蓄力/命中、`screenshot` 截图自查草地/动物/预览弧布局)
      —— 造型/节点反馈代码完成;`screenshot` 组件已随 `core2_board_init` 代调,上机后可直接用
      `tools/screenshot.py` 自查,本轮无设备未做。
- [ ] launcher 专属图标 —— **未做**,槽位未定案,任务要求本轮不碰 launcher。
- [ ] `flash_one.sh`/`flash_map.md` 登记 —— **未做**,槽位竞 ota_5 待立项确认。
- [ ] 根 `CLAUDE.md` §1/§12 索引更新 —— **未做**,同上,留到立项确认后。
- [ ] 实机点检清单(SPEC §12 七项 + 本轮新增 2 项)—— 全部待办,需接上 Core2+Bottom2+Chain
      Joystick 上机。

## 编译记录(2026-07-15,本机 WSL2,ESP-IDF v6.0)

```
$ source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh
$ idf.py -C apps/slingshot_feed build     # 全新 fullclean(先删 build/)

...
[1798/1798] cd .../apps/slingshot_feed/build && python .../check_sizes.py --offset 0x8000 \
    partition --type app .../partition-table.bin .../slingshot_feed.bin
slingshot_feed.bin binary size 0xa16c0 bytes. Smallest app partition is 0x180000 bytes.
0xde940 bytes (58%) free.

Project build complete. To flash, run:
 idf.py flash
 ...
```

零警告、零错误(`grep -i warning` 命中 0 处;两处 `error` 命中均为源文件名
`esp_tls_error_capture.c`/`error.c`,非编译诊断)。**严禁 `idf.py flash`**(会覆盖 launcher),
单刷等立项确认槽位后按 `tools/flash_map.md` 流程执行。

### 跟进:§5.5 校验公式改真实包络后复核(2026-07-15,同日)

`meadow.c: validate_spots` 从简化圆形判据改为 SPEC 已修正的真实可达包络公式(见上面
"偏差 2")。全新 fullclean 重新编译:

```
$ source ~/.espressif/tools/activate_idf_v6.0.sh && source $IDF_PATH/export.sh
$ rm -rf apps/slingshot_feed/build && idf.py -C apps/slingshot_feed build

...
[1798/1798] cd .../apps/slingshot_feed/build && python .../check_sizes.py --offset 0x8000 \
    partition --type app .../partition-table.bin .../slingshot_feed.bin
slingshot_feed.bin binary size 0xa1710 bytes. Smallest app partition is 0x180000 bytes.
0xde8f0 bytes (58%) free.

Project build complete.
```

零警告、零错误(同上核实方式)。6 个手工位置全部仍通过新校验(Python 复算,公式与
`validate_spots` 一致):`grass_low` 余量 178px、`grass_far` 86px、`tree_mid` 83px、
`tree_high` 25px、`cloud_left` 37px、`cloud_right` 仅 2px(最紧,见上"新增待观察项")。
