# fish_pond — 大鱼池塘(as-built)

> 本文件是 **fish_pond 应用的竣工记录**;玩法规格见 `SPEC.md`(施工图,完工后保留)。
> 跨应用平台事实(桌面玩法省电坑/大对象护眼约束等)见根平台手册 `CLAUDE.md`。
> **占 ota_5(0xB90000)**。烧录:`tools/flash_one.sh fish_pond`(= esptool write-flash 0xB90000)。

产出:P1(概念可达性:船+饵+单鱼咬钩即钓到)+ P2(收线拉扯 + 双层双鱼 + 木桶集 3 + 放生派对)
**一次性实现完毕**,`idf.py -C apps/fish_pond build` 通过(fullclean 重建复验,0 警告)。
外设、传输层、摇杆回中修复、桌面省电坑处理全部照抄 `apps/chain_lab` 定案,零协议层改动
(SPEC §1 红线)。**P3 趣味批(金鱼/鲸鱼彩蛋/摇杆按键泡泡喇叭/launcher 专属图标)未做**——
SPEC 自己写明"按实机反应定,不预先立项",留到实机验证后再评估。

## 架构:文件布局与 SPEC §8 的落地关系

```
chain_link.c/.h   Chain 传输/绑定层(scan_bus/poll_enc/poll_joy/joy_calibrate_center,
                  抄 chain_lab.c 去掉诊断 UI 与节点 RGB 写入——SPEC §7 反馈矩阵没有节点灯
                  这一列)。**同时是整个场景的编排入口**:chain_link_start() 依次调
                  pond_create/boat_create/fish_create/bucket_create 建场景、起 30Hz
                  game_task。game_task 每帧顺序调用 boat_tick → fish_tick → bucket_tick,
                  这三者之间没有反向依赖 chain_link——即 SPEC §8 说 chain_link.c"起
                  30Hz game_task"这句话在本仓库里天然includes"编排调用顺序"的含义
                  (对照 chain_lab.c 起 game_task 调用 crane_game_tick() 的先例)。
pond.c/.h         静态层,进场景画一次(天空/太阳/双层水带/水线泡沫/层分界线/水草石头)。
boat.c/.h         船(速度控制,死区+撞边缘滑停)+ 线(lv_obj 细矩形按线长缩放)+ 饵
                  (双轴指数缓动,τ=BAIT_EASE_TAU_MS)。曲柄增量→线长的换算与"收线比例"
                  (鱼种相关)都在这里,fish.c 只调 boat_set_reel_ratio() 影响它。
fish.c/.h         两条鱼的 AI + HOOKED 收线。**PATROL→CHASE→CHOMP→HOOKED→(出水交给
                  bucket 后原地立即重生)**;SPEC §6.2 的 NOTICE/APPROACH 合并成一个
                  AI_CHASE 状态,瞪眼/两级张嘴/普通造型按"鱼心到饵心的距离"逐帧连续
                  算出(vis_cache),不单独建状态机分支——效果等价,状态更少更好维护。
bucket.c/.h       木桶 3 态(空/有鱼/鼓胀)+ 计数圆点 + 入桶抛物线动画 + 放生派对(6 颗
                  彩色圆点从桶心飞散到池塘各处模拟"三条鱼跳回水里",派对结束回调
                  fish_round_setup() 开新回合)。
sprites.c/.h      §6.6"预烘"精灵构件的具体落地手法(见下节)。
feedback.c/.h     SPEC §7 反馈矩阵的九个事件 → audio_fx/haptics/ledstrip_fx 三通道的
                  语义封装;三个组件本身已是"投队列非阻塞",这层不必再起独立任务/队列。
tuning.h          SPEC §9 全部常量 + 本轮补充的实现级常量(见下节"新增常量")。
```

## "预烘"手法的具体落地(重要:偏离 SPEC 字面表述的地方)

SPEC §6.6 写"每鱼种 6 态 × 左右镜像 = 12 帧……全部预烘"。字面理解容易联想成 lv_canvas/
图片资源式的"烘好 12 张位图"。但翻遍现仓库(`crane_game.c` 的 `PRIZE_LOOK` 造型表、
`chain_lab.c` 的诊断台),**平台既有的"预烘"手法从来都是"用扁平色块 lv_obj 搭一次骨架,
状态切换时只离散地改子对象的尺寸/位置/颜色"**,不依赖 `lv_canvas`/图片资源,也不做运行时
alpha 混合——满足的是"状态帧切换、零运行时 alpha、零整屏重绘"这句**精神**,不是字面的"12 张
位图"。本作照此手法实现:每只鱼是一个透明容器 + 尾鳍/身体/腹斑/双眼/嘴 6 个子对象,
`fpond_fish_sprite_set_state()` 按 `fish_vis_t`(PATROL_A/B、NOTICE、APPROACH_SMALL/BIG、
CHOMP)离散重设这些子对象——效果与"12 帧切换"等价,实现量小得多、也是本仓库的一致做法。

## 帧预算:比 SPEC §6.5 表格额外做的两处优化

SPEC §6.5 明确要求"两鱼错帧更新是硬要求(同帧齐动峰值~23k超预算)"。**如果不做限流,
两条鱼每帧都会重绘**(`fish_tick()` 每 33ms 调一次,天然没有"只 20fps 更新"这回事)。
本轮补了两处优化,不做的话会违反 §6.2 硬规矩:

1. **两鱼 20fps 渲染节流 + 错帧**(`fish.c` `render_accum_ms`,新增常量 `FISH_RENDER_MS=50`):
   AI/位置推进仍是每帧(30Hz,数学便宜),但只有 `AI_PATROL`(常态巡游)的实际 LVGL 重绘
   被节流到 20fps,且两条鱼的节流相位错开半周期(胖胖鱼 0ms 起、大懒鱼错开 25ms)。
   CHASE/CHOMP/HOOKED/GIGGLE 这些短促交互高光时刻仍每帧渲染保跟手。
2. **船/线/饵静止时跳过重绘**(`boat.c`):取整后的渲染坐标与上一帧完全相同就不调
   `lv_obj_set_pos/set_size`——SPEC 表格没把船算进预算,但船同样是每帧被无条件调用,
   松杆停船后如果还在无差别写同样的坐标,仍会被 LVGL 判脏重绘,白白吃像素预算。

## 与 SPEC 字面表述的其余出入(均为实现细节,玩法效果不变)

- **HOOKED 期间"曲柄是否在动"的判据**:fish.c 不直接持有 `enc_delta`(那是 chain_link.c
  的内部状态),改用"线长本帧是否变化"反推(`boat_line_len()` 逐帧比较)——线长只可能
  由曲柄改变,等价且不用新开一条跨模块的输入通路。
- **GIGGLE 的"持续时长"与"音效冷却"复用同一个常量** `GIGGLE_COOLDOWN_MS`(800ms):
  SPEC 原文本就只给了一个 800/0.8s 的数,两个概念数值相同,不另开常量。
- **"进桶但桶仍忙"边界**:`bucket_catch_start()` 在 `bucket_is_busy()` 时静默忽略(双保险,
  `fish.c` 的咬钩判定已经用 `!bucket_is_busy()` 门控,理论上不会真的触发)。副作用是
  刚钓完一条鱼后 ~1s(`SPLASH_HOLD_MS+CATCH_FLIGHT_MS`)内即使饵怼到另一条鱼嘴边也不会
  咬钩(鱼会张大嘴悬在那,视觉上读作"就要咬了"而非卡死)——P1/P2 实机验收时留意这个
  间隔手感是否需要缩短。
- **摇杆轴向标定(`JOY_INVERT_X/Y`/`JOY_SWAP_XY`)与死区/回中参数照抄 `chain_lab` 定案值**
  作为起步值——两者是同一颗 Chain Joystick 节点、同一套装配,理应通用,但 SPEC 自己标了
  ★,实机如果方向不对翻 `tuning.h` 里的宏即可,不用改代码。
- **`CRANK_PX_PER_DET` 符号**(曲柄方向)沿用 SPEC 给的正值起步,实机标定"顺时针=放线"
  是否成立,反了在 `tuning.h` 翻负号。

## 家长菜单

未接入(SPEC §1 已注明:等 `docs/ROADMAP.md` R2 `components/parent_menu` 组件化后再接,
不自己写)。

## 实机点检清单

见 `SPEC.md` §13(P1/P2 验收 + 实机点检清单逐条);本仓库暂无法在此环境内烧录/上机
(WSL 环境限制,见平台惯例),**待用户拿到实机后按该清单逐条点检**,尤其:

- [ ] 摇杆/曲柄方向是否符合直觉(上面两条"照抄 chain_lab 起步值"的地方最可能要翻)。
- [ ] 两鱼 20fps 错帧渲染节流后,肉眼是否还流畅(§6.5 预算已过静态审查,实机复验手感)。
- [ ] 刚钓完一条鱼后 ~1s 内另一条鱼咬不了钩的间隔,孩子是否会误以为"卡住了"。
- [ ] SPEC §13 其余逐条(尺寸/咬钩四通道 <100ms/零失败手感/打盹省电/拔线提示卡等)。
