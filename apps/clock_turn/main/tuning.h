// clock_turn 可调参数 —— M0(单元 bring-up)+ M1(静态钟面/两针联动)+ M2/M4(信息区三子区、
// MODE_FREE/MODE_QUIZ 两模式、按键揭晓、随机出题、渐进提示弧)
//
// 🔴 与 tools/preview.py 是同一套几何算式的两份拷贝(SPEC.md §6.1/§9/§11 平台坑):
//    改这里任何布局常量,先跑 preview.py 出图确认,再改这里;反之亦然。
// 2026-08-06 语音整章作废(SPEC §8),M3 里程碑随之删除;本文件不含任何语音/PCM 相关常量。
// M5(静止思考宽限/放弃演示/连续使用提醒)、M6(单元容错完整 UI)仍未开工,相关常量
// 暂不写"占位数值"制造假一致(REST_HINT_MIN 例外,§5.8 已声明保留占位)。
#pragma once

// ── 编码器标定(SPEC.md §10;preview.py 不含输入,这两条另加)───────────────
#define ENC_DEG_PER_STEP   15      // 事实值(apps/chain_lab/main/tuning.h 已验证),24 格/圈,仅存档
#define ENC_INVERT          0      // ★ 实机标定:顺时针转=时间前进;若反了改 1
#define MIN_PER_STEP_DEFAULT 15    // 核心手感常量:编码器 1 格 = 多少分钟(SPEC §5.1 推导);NVS 无值时用它
                                    // 🔴 2026-08-10 由 5 改 15(用户实机反馈:对幼儿难度偏高)。
                                    // 15 → t 恒为 15 的倍数 → 分针只落 12/3/6/9 四个位置。
                                    // 2026-09-23 起运行时可调:BtnB 长按在 MIN_PER_STEP_CHOICES 里循环,
                                    // 存 NVS(namespace NVS_NS / key NVS_KEY_STEP),重启保留。
#define MIN_PER_STEP_CHOICES(X) X(5) X(10) X(15) X(30)   // X-macro:main.c 据此建表 + 编译期断言
                                    // 🔴 每一档**必须**整除 QUIZ_GRAIN_MIN(main.c 有编译期断言):
                                    // 否则会出到永远转不到的题 —— 本卡带唯一能造出"死局"的方式,
                                    // 直接违反零失败。
#define NVS_NS          "clock_turn"
#define NVS_KEY_STEP    "min_step"
#define STEP_TOAST_MS   1500        // 切步长后子区②显示「+15」多久(家长确认用),之后回原面板

// ── 钟面几何(= tools/preview.py CLOCK_CX/CY/R)────────────────────────────
#define CLOCK_CX     101
#define CLOCK_CY     120
#define CLOCK_R       95

// ── 两针(= preview.py HAND_MIN_*/HAND_HOUR_*/HAND_EDGE/CAP_R)────────────
#define HAND_MIN_LEN   72          // 分针:细而长
#define HAND_MIN_W      5
#define HAND_HOUR_LEN  42          // 时针:粗而短;必须 < NUM_RING_R,否则整点遮数字(SPEC §6.1)
#define HAND_HOUR_W     8
#define HAND_EDGE       2          // 指针描边(钟面色):两针近重叠(如 6:30)时的分界(SPEC §5.3.1)
#define CAP_R           8          // 中心帽,随指针粗细同步收小

// ── 刻度(= preview.py TICK_OUT/TICK_MAJ_IN/TICK_MIN_IN;宽度抄自其内联字面量)─
#define TICK_OUT        89
#define TICK_MAJ_IN     79
#define TICK_MIN_IN     82
#define TICK_MAJ_W       5         // 整点刻度线宽(preview.py draw_face: width=5*SS → 实际 5px)
#define TICK_MIN_W       2         // 分刻度线宽(preview.py draw_face: width=2*SS → 实际 2px)
#define FACE_BORDER_W    5         // 钟面外框(preview.py draw_face: outline width=5*SS → 实际 5px)

// ── 12 刻度数字(= preview.py NUM_RING_R/NUM_H;装饰级,不受 §8 64px 约束)───
#define NUM_RING_R      64
#define NUM_H           13         // preview.py 用桌面 TTF 按此像素高渲染;真机取最接近的内置位图
                                    // 字体 montserrat_18(数字实高 ≈13px)。⚠️ 不是 LV_FONT_DEFAULT
                                    // (=montserrat_14,数字实高仅 ≈10px,矮 ~23%)。字体档位在
                                    // sdkconfig.defaults 启用,用处在 clock_ui.c::create_static_face

// ── 🔴 当前小时牌(HOUR_CHIP,2026-08-24 读表方向修复批,SPEC §5.9)──────────
// 实机症状:整点孩子念得对(7:00 → "7点"),半点一律念成"6点" —— 因为 7:30 时**长针正好
// 穿过数字 6**,而短针 42px 根本够不到数字圈(内沿 57.5),屏上"指着某个数字"的针**只有
// 分针一根**。孩子的规则是"念被针指着的那个数",在整点上恰好蒙对。修法 = 给短针所在的
// 那一格挂一块砖红名牌(数字反白),把"该念哪个数"变成屏上看得见的东西。
// 🔴 名牌**只在按键揭晓/答对那几秒露面**,不常显:常显 = 屏上一直摆着半个答案,孩子念牌子
//    就够了、再不必看短针,而"他会不会读短针"正是要验的那件事。露面时机跟读数同一道闸门
//    (§5.4),流程 = 先看针猜 → 按键 → 牌子与读数小时段同时亮,猜测的含金量才保得住。
#define HOUR_CHIP_EN     1         // ★ 脚手架总开关:孩子熟练后置 0 → 回到真挂钟的干净盘面。
                                    // 与时针砖红(§5.3.1)是同一类"本机有、真表没有"的线索,
                                    // 迁移验证方式相同:拿家里挂钟问一次"现在几点"。
#define HOUR_CHIP_W     27         // 名牌宽:要装得下两位数「12」(montserrat_18 约 20px)+ 留白;
                                    // 🔴 上限由刻度内沿倒推 —— NUM_RING_R + W/2 必须 < TICK_MAJ_IN
#define HOUR_CHIP_H     20
#define HOUR_CHIP_RAD   10         // = H/2 → 胶囊形(圆角矩形退化成两端半圆)
#define HOUR_CHIP_EDGE_W 3         // 露面期间再描一圈亮边(向内画,不撑大几何)
#define C_HOUR_CHIP     0xC4453A   // = C_HAND_HOUR 砖红:名牌与时针同色 = "这块牌子是短针的"
#define C_HOUR_CHIP_FG  0xFFF6EC   // 名牌上的数字(暖白,砖红底上对比度 4.64,小字 4.5 线之上)
#define C_HOUR_CHIP_ED  0xFFD9C8   // 亮边色:揭晓/答对时钟面这个数与读数里的小时段同时亮

// ── 配色(= preview.py 对应 RGB 元组的十六进制值,仅列 M0+M1 用到的几个)───
#define C_BG        0x2C303E       // 背景(preview.py C_BG = (44,48,62))
#define C_FACE      0xF5E8CD       // 钟面底色(preview.py C_FACE)
#define C_RIM       0xC69E62       // 钟面外框(preview.py C_RIM)
#define C_TICK_MAJ  0x8C643C       // 整点刻度(preview.py C_TICK_MAJ)
#define C_TICK_MIN  0xC4AC8A       // 分刻度(preview.py C_TICK_MIN)
#define C_NUM       0x8A4038       // 刻度数字。★ 2026-08-24 由暖棕 0x705030 移到**红色系**
                                    // (亮度不变,对比度仍 5.97,只换色相):12 个数字是**时针的
                                    // 刻度**,不是中性装饰 —— 让它们和砖红时针同族,是"数字归短针
                                    // 管"这条规则的最便宜的一条冗余(SPEC §5.9)。
#define C_HAND      0x3A322C       // 中心帽 + 反馈脸五官,暖黑(preview.py C_HAND/C_CAP)。
                                    // 中心帽是两针交汇处,**刻意保持中性**:染成任一针的颜色都会
                                    // 让那根针在根部"长出去一截",破坏长短这条几何线索。
#define C_HAND_MIN  0x1E6E78       // ★ 分针:深青(2026-08-10)。与时针红配对,构成孩子的两条规则
                                    // 「红=时针/小时、青=分针/分钟」。钟面上对比度 4.87,与时针
                                    // 4.06 视觉分量相当 —— 谁都不许压过谁,否则等于暗示主次。
#define C_HAND_HOUR 0xC4453A       // ★ 时针:砖红。**2026-08-10 用户改判**,推翻 §5.3.1 原「两针同色」
                                    // 硬规矩(理由:实机对幼儿难度偏高)。代价见 SPEC §5.3.1 修订段:
                                    // 颜色是**真挂钟上不存在的线索**,有迁移失败风险。
                                    // 🔴 长短(42:72)+ 粗细(8:5)这两条原生区分**一条都不许撤**——
                                    // 颜色是加上去的第三条冗余,不是替代品(preview.py check_layout
                                    // 的两条断言仍在守这件事)。
                                    // 要整套回退成真表配色:C_HAND_HOUR / C_HAND_MIN 都改回 0x3A322C,
                                    // C_DIGIT_HOUR / C_DIGIT_MIN 都改回 0xEEE8DC(原 C_DIGIT),四行。

// ── M0 施工用:单元探测/重试节奏(SPEC §10 表未列,属本里程碑的工程常量)───
#define ATTACH_RETRY_MS   2000     // 没探到 Chain Encoder 时的重扫周期(SPEC §1 通用容错形态)
#define ERR_STREAK_LOST      8     // 连续读失败多少次判"拔线/断电"(同 chain_lab ERR_STREAK_LOST)
#define NODE_RGB_BRIGHTNESS 40     // 节点板载 RGB 亮度档 0~100(护眼压低,同 chain_lab)

// ═══════════════════════════════════════════════════════════════════════
// M2/M4 新增:信息区三子区 + 两模式 + 按键揭晓 + 随机出题 + 渐进提示弧
// (= tools/preview.py 对应常量;preview.py 用小写变量名,这里按平台惯例转 UPPER_SNAKE)
// ═══════════════════════════════════════════════════════════════════════

// ── 信息区总体(= preview.py INFO_X0/X1;三子区纵向分带,SPEC §5.3)────────
#define INFO_X0        201         // 信息区左沿(钟面最宽处 x=196,留 5px)
#define INFO_X1        315         // 信息区右沿

#define Z_STAT_Y0        6         // ① 状态条(家长向):两位模式开关 + 连接点 + 电量
#define Z_STAT_Y1       40
#define Z_PANEL_Y0      46         // ② 数字钟(孩子向):读数,信息区主角
#define Z_PANEL_Y1     148
#define Z_FACE_Y0      154         // ③ 反馈脸(孩子向):idle/👍/👎
#define Z_FACE_Y1      234

#define CARD_R           8         // 子区圆角(= preview.py CARD_R)

// ── ① 状态条:两位模式开关(= preview.py MODE_*/LINK_*/BATT_*/HOLD_BAR_*,SPEC §5.3.5)──
#define MODE_SLOT_W     27
#define MODE_SLOT_H     20
#define MODE_SLOT_Y0    11         // 槽 y 11..31
#define MODE_A_X0      207         // 槽A(人形=MODE_FREE)207..234
#define MODE_B_X0      237         // 槽B(屏形=MODE_QUIZ)237..264
#define LINK_CX        277         // 旋钮连接点(绿=在/红=拔了)
#define LINK_CY         21
#define LINK_R           4
#define BATT_X0        288         // 电量壳(填充宽度/颜色由 power_monitor 每 10s 刷)
#define BATT_Y0         15
#define BATT_W          21
#define BATT_H          12
#define HOLD_BAR_X0    205         // 长按进度条:宽度随进度从 X0 长到 X1
#define HOLD_BAR_X1    311
#define HOLD_BAR_Y0     33
#define HOLD_BAR_H       4
#define MODE_HOLD_MS  1500         // 长按切模式门槛(比家长菜单 3s 短,§5.3.5 理由)

// ★ 长按热区:**刻意大于状态条卡片**(卡片 201..315 × 6..40),不与视觉边界对齐。
//   LVGL 在手指滑出对象时发 PRESS_LOST、长按进度清零重来;卡片只有 34px 高(≈4mm),
//   按住 1.5s 期间指腹的自然位移足以出界。上下左右各留余量,下沿吃进子区②卡片 6px
//   —— 子区②不接触摸,无损失。实机若仍难触发,先加大这四个数,再考虑放宽 MODE_HOLD_MS。
//   通用做法见根 CLAUDE.md §8「触摸靶画得比视觉靶大一圈」。
#define MODE_HOTSPOT_X0  196
#define MODE_HOTSPOT_X1  320
#define MODE_HOTSPOT_Y0    0
#define MODE_HOTSPOT_Y1   52

// ── ② 数字钟读数(= preview.py PANEL_*/READOUT_H,SPEC §5.3.2/§5.4)────────
#define PANEL_CX       258         // = 子区②中心
#define PANEL_CY        97
#define READOUT_H       25         // 读数字高;上限由最宽读数「12:55」反推,见 SPEC §6.1
                                    // 🔴 须用 montserrat_34(数字实高≈0.72×字号≈24.5px),
                                    // 不是 montserrat_18(那档给 12 刻度数字用,字高仅 13px)
#define PANEL_HW        57         // = 子区②半宽
#define PANEL_HH        51         // = 子区②半高
#define PANEL_BORDER     3         // MODE_QUIZ 态橙描边宽度(占内腔)
#define READOUT_HOLD_MS 4000       // ★ MODE_FREE 按键揭晓后停留多久再淡回占位点(§5.4);
                                    // 留太久 = 又变回实时读数
#define REVEAL_STAGGER_MS 700      // ★ 揭晓分两拍:先只亮小时段、分钟段压暗,过 700ms 才转亮
                                    // (SPEC §5.9)。教的是**读表顺序**:先看短针念钟点,
                                    // 再看长针念分钟 —— 孩子当前的错法正是跳过第一步。
                                    // 🔴 只压颜色不改字串,读数整串宽度/居中位置全程不变,
                                    // 否则第二拍会整体"跳"一下,比不分拍更糟。

// ── ③ 反馈脸(= preview.py FACE_R,原"说话脸"改名,SPEC §5.3.4)─────────────
#define FACE_R          34         // 半径→φ68,压着根 CLAUDE.md §8 的 64px 线,不许再小
// 答对/按错换成拇指(2026-09-23):圆盘颜色 + 拇指朝向两条线索各自独立可读。
// 👎 刻意用暖橙不用红 —— 红读作"错/危险",这里要传达的只是"还没到"。
#define C_THUMB_UP_BG   0x6EC878   // = C_GREEN,与答对时读数变绿同一个"对了"
#define C_THUMB_DOWN_BG 0xE8803C
#define C_THUMB         0xFFF6EC   // 拇指(暖白)
#define C_THUMB_CUFF    0x3A322C   // 袖口 = C_HAND

// ── 渐进提示弧(= preview.py HINT_R/HINT_ARC_W1/W2,SPEC §5.6/§6.1)────────
// 🔴 走时针角不是分针角:分针角 60min 一个周期会把跨小时的差值算错,时针角 720min 内
//    单调、唯一编码 Δt。半径卡在时针尖(42+8/2=46)与数字圈内沿(64-13/2=57.5)之间。
#define HINT_R          50
#define HINT_ARC_W1      4         // 第 1 次按错:细而暗
#define HINT_ARC_W2      7         // 第 2 次及以后:粗而亮

// ── 配色(信息区/两模式/提示弧,= preview.py 对应 RGB 元组;M0+M1 已有的色见上)──
#define C_DIGIT_MIN 0x7FD8E0       // ★ 读数的**分钟**段:浅青 = C_HAND_MIN 的提亮档(底卡 6.49/6.94)
#define C_DIGIT_MIN_DIM 0x4E8C96   // ★ 揭晓第一拍的分钟段(底卡 2.80,明显退后但仍在):
                                    // 不是隐藏 —— 隐藏会让整串重新居中,位置一跳(见 REVEAL_STAGGER_MS)
#define C_DIGIT_HOUR 0xFF8A7A      // ★ 读数的**小时**部分:珊瑚红 = C_HAND_HOUR 的提亮档,
                                    // 让孩子把"红色的数字"和"红色的针"连起来(2026-08-10 用户要求)。
                                    // 🔴 为什么不直接用 C_HAND_HOUR(0xC4453A):指针画在**浅色钟面**上、
                                    // 数字画在**深色底卡**上,同一个色号不可能两边都读得清 —— 这是
                                    // 算出来的死结,不是调色偏好:
                                    //   对钟面(L≈0.80)要 ≥3:1 → 亮度 ≤0.233
                                    //   对底卡(L≈0.05)要 ≥3:1 → 亮度 ≥0.250   两区间不相交。
                                    // 砖红搬到底卡上只有 2.16/2.31(大字下限 3.0),会把最该看清的
                                    // 小时数字变成全屏最难读的东西 —— 正好和"帮助关联"的目的相反。
                                    // 本值 4.65(普通卡)/4.97(答题卡);色相与砖红同为 ~6°,同属红色系。
#define C_DIGIT_SEP 0x8F98AB       // ★ 读数的**冒号**:中性灰蓝,刻意既不是红也不是青 ——
                                    // 冒号染成任一段的颜色,就会被读成那一段的一部分
                                    // (「:30」看着像一整块青),把"红=小时/青=分钟"这条
                                    // 规则的分界线搞模糊。它是标点不是信息,故取比两段都暗
                                    // 一档的中性色让它退后(底卡 3.67 / 答题卡 3.93,过大字
                                    // 3.0 线)。🔴 别直接用 C_MUTED(2.77,不够)。
#define C_QUIZ      0xFF9A3C       // 暖橙:MODE_QUIZ 的统一强调色
#define C_HINT      0xFFC448       // 提示弧-第1次按错(暗)
#define C_HINT2     0xFFE08C       // 提示弧-第2次及以后(更亮)
#define C_GREEN     0x6EC878       // MODE_QUIZ 答对:数字变绿
#define C_CARD      0x383E50       // 子区底卡(比背景略亮)
#define C_CARD_Q    0x4A3624       // MODE_QUIZ 态子区②(暖橙暗调)
#define C_CARD_HI   0x4E566C       // 状态条①选中槽底(比 C_CARD 更亮一档)
#define C_MUTED     0x7A8296       // 家长向图标,刻意压暗
#define C_LINK_OK   0x78BE78
#define C_LINK_BAD  0xCE6054
// 电量壳配色:**与 launcher 逐值对齐**,同一块电池在两个界面上不能是两种颜色语言
#define C_BATT_USB  0x4FB0D8       // 充电中(整条画满,见 clock_ui_set_battery)
#define C_BATT_LOW  0xD9483A       // <15%
#define C_BATT_MID  0xFFC75F       // <40%
#define C_BATT_OK   0xA7C957

// ── 出题(= preview.py 无对应,纯逻辑常量;SPEC §5.5)────────────────────────
#define QUIZ_GRAIN_MIN  30         // ★ 出题粒度(分钟),决定题池大小(24 格,esp_random 抽取);
                                    // 🔴 **必须是每一档步长的整数倍**:t 只能落在步长
                                    // 的倍数上,更细的题目永远转不到 = 死局(§5.5)。
                                    // 2026-08-10 随 MIN_PER_STEP 5→15 同步改(题池 144→48,
                                    // 平均转动量 ~36 格→~12 格);2026-08-18 再提到 30(题池 48→24,
                                    // 只收窄出的题、不改 MIN_PER_STEP,分针仍走 4 个位置,知识点不丢,
                                    // 只是不再考「一刻/三刻」,见 SPEC §5.5)。
#define WIN_HOLD_MS   2000         // 答对后庆祝停留多久,之后自动出下一题(SPEC §4)

// ── 旋钮中心键(SPEC §5.4)────────────────────────────────────────────────
#define BTN_DEBOUNCE_MS 150        // 边沿去抖窗口:去抖期内的按下不算新的一次

// ── 保留占位(本批未接入,见 SPEC §5.8/§13 M5)───────────────────────────
#define REST_HINT_MIN   15         // 连续使用提醒(分钟);常量占位,逻辑未实现
