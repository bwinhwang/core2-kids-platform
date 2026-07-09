# magic_wand — 隔空魔法棒(as-built,构建记录 · 待实机)

> 本文件是 **magic_wand 应用的竣工记录**。规格见同目录 `SPEC.md`(P1–P4 全量已实现)。
> 跨应用平台事实(单元接入/桌面玩法省电坑)见根平台手册 `CLAUDE.md`(§10/§7/§11)。
> **占 ota_5(0xB90000)**,外设 = Unit Gesture-I2C(PORT.A @0x73)+ Unit RGB(PORT.B @G26,P4)。
> 烧录:`tools/flash_one.sh magic_wand`(= esptool write-flash 0xB90000)。

## 玩法

屏幕里的小魔法师对孩子举着的隔空手势(PAJ7620U2,9 种内置手势)逐一起反应,九种法术
各有独立画面/音签/触觉/底座灯/魔法棒灯配色;感应到动静但未分类时走固定低频"存活性
ping"兜底(§4 见下)。九种法术全解锁一次 → 法术书集满 → 派对(9 法术接力回放 + 收场
庆祝)→ 法术书清零开新一轮。第 10 页隐藏彩蛋:`COMBO_WINDOW_MS`(2.5s)内挥出 3 种不同
手势 → 头顶双层烟花,不占书页格。详见 `SPEC.md` §1/§5/§6。

## 平台新增(可复用)

- `components/units/unit_gesture` —— PAJ7620U2 最小驱动。寄存器级事实(bank 切换
  0xEF、PART ID 0x00/0x01、结果寄存器 0x43/0x44、219 组出厂初始化表)**逐条核实自
  两份厂商参考驱动源码**(Seeed_Studio/Grove_Gesture + DFRobot/DFRobot_PAJ7620U2),
  不是编造。
  🔴 **手势→比特位映射两份参考驱动互不一致**(不同物理封装/丝印方向的已知分歧):
  最终采用 DFRobot 映射,依据是 **M5Stack 官方 Unit Gesture(本平台购买的确切型号
  U127)示例代码明确依赖 DFRobot_PAJ7620U2 库**。**待实机确认**:若方向感觉"反了"
  (如挥右识别成挥左),改 `unit_gesture.c` 的 `unit_gesture_read()` switch 分支,
  不用碰初始化表。
  **无独立"物体存在但未分类"信号**:两份参考驱动的手势读取路径都只读 0x43/0x44 这
  两个"已分类手势"结果寄存器;芯片另有 PS_APPROACH_STATE(0x6B)等接近感应寄存器,
  但那是需要额外校准的另一套模式,默认手势初始化表不保证其语义有效,**故未使用**。
- `components/units/unit_rgb` —— PORT.B 3× SK6812 驱动,独立于 `ledstrip_fx`(不同
  GPIO/像素数/语义,职责边界见 SPEC.md §2)。供电确认:M-Bus 5V 单路分裂给灯带/
  PORT.A/B/C 共用(`core2_power` 头注释已载明"及一切吃 M-Bus 5V 的外设"),
  `core2_board_init(enable_leds=true)` 一次性覆盖,PORT.B 不需要额外开电。

## 实现取舍(与 SPEC.md 的落地差异,均为工程判断,非偷工)

- **SHIMMER 微光回应**:采用 SPEC §4 点 2 的**退化方案**——固定 `SHIMMER_IDLE_MS`
  (2.5s)低频 ping,不与真实手部动作强绑定(依据见上"无独立在场信号"）。ping **不**
  调 `core2_sleep_kick`(它是装饰性"魔法待命"提示,不代表真的有人在玩;踢一下会让
  打盹永不发生)。
- **躲猫咒(BACKWARD)延迟揭晓**:视觉(缩小→停顿 `PEEK_HOLD_MS`→回弹)与音/震/底座
  灯/魔法棒灯的整套反馈**都推迟到揭晓瞬间统一打出**(呼应 peekaboo 揭晓手感),由
  `magic_wand.c` 的 `pending_reveal_tick()` 接管计时,`wizard.c` 只管画面本身的
  停顿动画。九种法术里唯一有这个延迟结算的特例,其余八种手势分类成功后全通道立即
  同时触发。
- **派对回放未做"压缩版"独立动画**:`wizard_party_step()` 直接复用 `wizard_cast()`
  的完整动画(而非另建一套精简版)。`magic_wand.c` 的步进定时器按 `PARTY_STEP_MS`
  (300ms)推进,若某步动画(如躲猫咒的 300ms 停顿)比 300ms 长,该步会自然多占用一点
  时间而不是被截断——用"步进节奏可能略慢于 300ms"换取"零截断闪烁",工程判断。
- **旋风咒(顺/逆时针)**:8 方位查表(`OCT_DX/DY`,固定表非逐帧三角函数)+ 8 次
  `delay=n*WHIRL_STEP_MS、duration=1` 的离散跳变实现(而非单个长时程 anim 配
  `lv_anim_path_step` ——后者只在动画结束瞬间跳一次,不适合"沿途每步都要停一下"的
  查表步进,实现中已验证过这个坑并改用离散多段方案)。
- **连击彩蛋判定**:用固定 3 槽环形历史(`COMBO_NEEDED=3`)判"最近连续 3 次手势互不
  相同且都落在 `COMBO_WINDOW_MS` 内",边沿触发(判定一次即清空,不重复触发同一组合)。

## 🔴 桌面玩法省电坑(单元玩法通用变体)

机身不动 ≠ 没人玩——手势分类成功即 `core2_sleep_kick`(退化 ping 不算,见上)。NAP 时
5V 还在、Gesture/RGB 继续工作,施法手势可直接唤醒(但唤醒帧本身**不判定**为法术,仿
busy_knobs 小鸟先例,唤醒后重新开始判定、不续用休眠前的冷却计时)。DEEP 切 5V → 两个
单元断电复位,唤醒后 `unit_attach` + `wand_fx_start()` 重接管。容错:没插 Gesture =
无字提示卡 + `ATTACH_RETRY_MS`(2s)周期重试,插上即"你好"接管;连续
`ERR_STREAK_LOST=20` 次 I2C 读失败判拔线回提示卡。RGB 单元缺席不阻塞整卡启动
(`wand_fx_start()` 容错,失败只记日志)。

## 待实机标定 / 验证(SPEC.md §14,P1 优先)

- **命中率**(风险最大项):9 种手势对 3~4 岁孩子隔空挥动的真实识别率;方向映射
  (见上"待实机确认"段)。
- 挥手未分类时"微光回应"ping 的观感是否够用(节奏 `SHIMMER_IDLE_MS` 待调)。
- 同手势连冷却(`RECAST_COOLDOWN_MS=500ms`)是否够/太长;换手势打断是否够灵敏。
- P2 法术书 9 页解锁节奏、派对回放总时长手感。
- P3 连击彩蛋窗口(`COMBO_WINDOW_MS=2.5s`)是否好触发。
- P4 魔法棒 RGB 与屏幕/音效的同步观感、亮度(`WAND_MAX_BRIGHTNESS=60`)是否舒适。
- 打盹/深度省电进出、DEEP 唤醒双单元重初始化、拔线提示卡。

## launcher 图标

尚未加 magic_wand 专属图标分支(显示通用笑脸,不影响可玩性);待实机验收后补,
**要重刷 launcher 才显示**。

## 状态

✅ **build 通过**(`idf.py -C apps/magic_wand build`,esp-idf v6.0.1 命令行;
app 0x9e460 ≈ 634KB,ota_5 槽 0x200000 内余 ~59%);
⏳ **待烧录实机**:P1 手势命中率是第一优先验证项(SPEC.md §13 明确不建议跳过直接做
全量调优),其余见上"待实机标定"清单。
