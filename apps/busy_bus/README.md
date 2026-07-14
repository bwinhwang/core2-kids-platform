# busy_bus — 小小巴士(as-built)

> 本文件是 **busy_bus 应用的竣工记录**;玩法规格见 `SPEC.md`(施工图,完工后保留)。
> 跨应用平台事实(Chain 链路/桌面玩法省电坑)见根平台手册 `CLAUDE.md`(§10 做新 App 指南、
> §7 电源·休眠、§11 坑位)与 `apps/chain_lab/README.md`(Chain 链路 as-built 源头)。
> **占 ota_3(0x790000,原 feed_monster 槽,2026-07-12 回收)**。
> 烧录:`tools/flash_one.sh busy_bus`(= esptool write-flash 0x790000)。

## 现状(2026-07-13)

一次性写完 SPEC 全量(P1 车感底盘 + P2 接送闭环 + P3 打磨),`idf.py -C apps/busy_bus build`
**全新 fullclean 编译零警告零错误**通过;launcher 已加专属图标分支(白底面板:暖橙车身 + 两
车窗 + 两车轮)并本地验证 `idf.py -C launcher build` 通过。**本机当前没有连接设备(无串口),
尚未单刷上机,§12 的实机点检清单全部待办**——尤其 P1 车感(`BUS_SPEED_MAX`/`JOY_EMA_PCT`/
`BUS_DRIVE_MODE` A/B)是全 SPEC 最大未知,必须先上机验证。

## 架构:传输层 / 静态层 / 精灵 / 游戏层 / 反馈(仿 chain_lab 拆分)

```
bus_link.c/.h   传输/绑定层:scan_bus/poll_joy/node_rgb/hue2rgb 形状照抄 chain_lab.c,
                收窄为 joystick-only(encoder 若挂在链上会被扫到但直接跳过,不冲突)。
                20Hz game_task 里跑 core2_sleep_feed + 深度省电重扫,AWAKE 时调 bus_game_tick()。
town.c/.h       静态层:小镇全景(马路装饰/三栋彩色房子两态/树+喷泉圆形障碍/5 个站牌点/
                车库)进场画一次,只有房子"亮灯"两态切换会重绘(渲染红线合规)。
sprites.c/.h    巴士 8 朝向精灵(init 时按 tilt_maze 五角星的路子,反向旋转采样点做 4×4
                超采样,烘成 8 张 28×28 ARGB8888)+ 乘客造型表(仿 chain_lab PRIZE_LOOK:
                熊/小鸡/青蛙/兔,身体圆+特征件)+ 房形图泡(方身+45°旋转小方块当屋顶)。
bus_game.c/.h   状态机 RS_PLAY⇄RS_PARTY + 乘客子状态机 WAITING→BOARDING→RIDING→
                ALIGHTING→HOME + 巴士速度积分/8朝向迟滞/障碍滑行 + 接送/错门判定 + 喇叭。
                逻辑坐标存影子变量(p->x/y),LVGL 只在 render_all() 一处统一落(单把锁,
                busy_knobs 小鸟先例:不跨任务碰 LVGL 状态)。
feedback.c/.h   六类事件 → 音频/触觉/灯带/摇杆节点 RGB 四条共享通道(仿 tilt_maze 队列+
                后台任务形状);屏幕这条由 bus_game/town/sprites 直接管,不经本模块。
```

## 关键实现取舍(SPEC 未列出精确值的地方)

- **小镇几何手工编排**(非随机生成):RED/BLUE 两栋房在上排(门朝下),GREEN 在下排居中
  (门朝上),车库在左下角;喷泉+两棵树在中央偏心位置;5 个站牌点散布四角+边缘,每轮随机
  选 3 个。全部硬编码在 `town.c` 的静态表里,坐标经手工核对不重叠(门垫/障碍/站牌/车库),
  但**实机对着真设备看一眼再微调是必经步骤**(§9 ★ 标的都是数值,几何布局同样待验证)。
- **障碍碰撞**:通用圆(树/喷泉)与轴对齐矩形(房子)两套最近点投影 + 法向速度分量清零的
  滑行解算(`bus_game.c: resolve_collisions`),屏边界(树篱)按同样思路钳位;tilt_maze 的
  `maze_resolve_collision` 是网格墙专用,本作障碍是自由摆放的圆/矩形,所以是新写的通用版,
  不是照搬,SPEC §1 提到的"抄 tilt_maze 撞墙滑行"取的是**手感原则**(滑走不粘住),不是代码。
- **BOARDING/ALIGHTING** 用 game_task 侧帧计数手动插值(from→to 两点 + 抛物线小跳弧线),
  未额外套 `lv_anim`——效果与"lv_anim + 影子变量"等价(帧计数判完成、影子坐标存于
  `passenger_t.x/y`),但少一层 lv_anim 簿记,渲染仍统一收在 `render_all()`。
- **摇杆节点 RGB 常态基准**每帧同步一次(空车=暖白、载客=乘客目的地色),顺带让 `EV_HONK`
  异步写的"闪白一下"在下一帧(~50ms)自然被带回常态,不需要单独的"延时归位"定时器。
- **BUS_DRIVE_MODE 默认 0**(速度型:推多少走多快);SPEC §11① 的恒速降级预案已实现在
  `update_drive()` 里,改 `tuning.h` 一行 `BUS_DRIVE_MODE` 0→1 切换,无需改状态机代码。
- **喇叭"附近有人"只播一次应答音**,不逐个乘客叠加(§6 表格"挥手+短应答音"读作一次性
  群体回应,不是机枪音)。

## 实机排障(2026-07-14)

- **绿房子送不进乘客**:`town.c` 原先窗/门统一画在房身底沿,但 GREEN 的送达判定区
  `HOUSE_DOOR[2]` 在房子上方(喷泉一侧,巴士够得到)——房身下沿到屏幕下树篱只剩 16px,
  巴士碰撞半径 14px 根本挤不进那条缝,玩家对着一扇视觉门却永远送不到。改
  `HOUSE_DOOR_UP[]` 按每栋房子实际判定区所在侧画窗/门(`make_house()`),RED/BLUE 门朝下、
  GREEN 门朝上,与各自 `HOUSE_DOOR` 同侧。截图逐像素核对过门已挪到正确一侧。
- **摇杆没推、小车也会缓慢漂移**:根因不是死区太小,是这颗 Chain 摇杆**松手后的物理静止点
  本身偏离开机校准中心**——实机加日志实测过:校准中心 `(31555,33561)`,静置几秒后原始读数
  稳定在 `(31170,33564)`,换算偏移 `nx≈0.25`,远超原来的 `JOY_DEADZONE=0.12`,固定死区盖不住
  (调大死区又会吃掉小推杆的可玩范围)。改 `bus_link.c: poll_joy()` 做**自适应回中**:每帧只要
  当前偏移还在 `JOY_RECENTER_BAND=0.35` 内(不算"明显在推"),就把校准中心慢慢往当前读数拉
  (`JOY_RECENTER_PCT=3`/帧,20Hz 下时间常数≈1.7s);推杆超出这个带时该轴暂停校正,避免长按
  摇杆开车被拉成新中心、按住反而停下来。截图验证:12s 内两张截图车库区域逐像素零差异
  (漂移彻底消失)。`BUS_DRIVE_MODE=0`(速度型)另外也补了死区(`bus_game.c: update_drive`),
  之前该分支完全没过死区,只是这次真正的病根是回中,不是死区本身。

## 待验证 / 已知风险(SPEC §11 原样保留,逐条对应实现位置)

1. 车感(`BUS_SPEED_MAX=90` `JOY_EMA_PCT=25`)——`tuning.h`,首要实机项。
2. 图泡"房形+颜色"可读性——`sprites.c: sprites_bubble_create/set_color`。
3. 容量 1 是否够玩——`BUS_CAPACITY` 常量,当前只是文档性(状态机本身用 `s_carry` 单变量,
   升级到 2 需要改 `s_carry` 为数组,非一行改动)。
4. 8 朝向是否显"跳"——`sprites.c: sprites_bake_bus`,迟滞已按 `HEADING_HYST_DEG=10` 实现。
5. encoder 节点闲置——本作只认领 joystick,链上 encoder 不处理(`bus_link.c: scan_bus` 里
   `if (s_joy_id == id) continue;` 之外没有 encoder 分支,天然跳过)。
6. 链路强度——沿用 chain_lab 结论,未内置诊断模式(`chain_bus_sniff` 走 chain_lab 卡带排障)。

## 里程碑(SPEC §12)

- [x] P1 车感底盘(小镇静态层 + 巴士 8 朝向行驶 + 障碍滑行 + 喇叭)—— 代码完成,**实机验收未做**。
- [x] P2 接送闭环(乘客生成/图泡/接客/送达/错门/计数 + 派对 + 换轮)—— 代码完成。
- [x] P3 打磨(喇叭社交挥手、乘客待机小晃、launcher 专属图标)—— 代码完成。
- [ ] `flash_one.sh`/`flash_map.md` 登记 —— **已完成**(本轮随实现一并登记,见文首)。
- [ ] 根 `CLAUDE.md` §1/§12 索引更新 —— 见下一条提交。
- [ ] 实机点检清单(SPEC §12 六项)—— 全部待办,需接上 Core2+Bottom2+Chain Joystick 上机。
