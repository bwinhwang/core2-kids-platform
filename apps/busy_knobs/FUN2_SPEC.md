# FUN2_SPEC — busy_knobs 趣味增量第二批(实施规格)

> **本文件是给实现者(AI 协作者)的施工图**:5 项趣味增量的完整规格。实现前先读根
> `CLAUDE.md` §2(五原则)、§5(反馈通道)、§6(渲染红线)、§10(桌面玩法省电坑),以及
> `apps/busy_knobs/README.md`(现状 + 8Encoder 踩坑史)。涉及 LVGL API(尤其 `lv_anim`
> 自定义 exec 回调、对象裁剪行为)**先查 MCP 再写**(`AGENTS.md` §1)。
>
> 完工后:删除或归档本文件,把定案内容按惯例并入 `README.md` 的「趣味增量」小节
> (参照 2026-07-06 第一批的写法),新增调参项补进 README「待实机标定」表。

---

## 0. 范围与红线

**做 5 项**(编号沿用讨论稿):
1. 柱顶小脸活化(眨眼 / 看向 / 嘴随高度 / 到顶鼓腮)
2. 小鸟访客 + 图案叙事化(小鸟爬楼梯)
3. 图案彩蛋反馈差异化(五种图案五种"声/跳/灯"方向)
4. 夜晚声音世界(低音色 + 小音量)+ 星星微闪
5. 多键齐按 = 和弦彩蛋

**不做**:NVS 成长感(花园)——明确排除,勿顺手加。

**改动面**:
- `apps/busy_knobs/main/knobs_game.c` —— 主体
- `apps/busy_knobs/main/tuning.h` —— 新增调参常量(见 §7)
- `components/ledstrip_fx/`(`include/ledstrip_fx.h` + 实现)—— **唯一允许动的组件**,
  只追加枚举与新特效实现,**严禁改变现有 3 种特效(BUMP/COLLECT/WIN)与 4 种基础模式的行为**
  (tilt_maze 等其它 app 在用)。
- 其余组件、`partitions.csv`、`sdkconfig*` 一律不碰。

**纪律红线**(违反=返工):
- 所有 LVGL 调用包 `bsp_display_lock(0)…bsp_display_unlock()`;每帧尽量合并成一次加锁。
- 永不整屏重绘;本批全部新增视觉都是 ≤22×18px 级小对象的脏矩形,合计远在 §6.2 预算内,
  但**不许引入任何逐帧 alpha 混合**——一切脸/鸟/星都用不透明纯色块(现有 `plain()` 工具)。
- `audio_fx_play_notes` 上限 **8 音 / 总时长 ~400ms 截断**,amp 0~100;规格内所有音序已按此设计,勿超。
- 手动定高前先 `lv_anim_delete(obj, exec_cb)` 掐残留动画(现有代码惯例);小鸟的 x/y 动画用
  各自 exec 回调,删除时回调要配对。
- 省电语义不变:新增视觉 tick 只在 `stage == CORE2_SLEEP_AWAKE` 跑;小鸟/眨眼**不算**
  "有人玩",不得 `core2_sleep_kick`(否则永不打盹)。
- 随机数用 `esp_random()`(已 include)。
- `game_task` 栈现为 4096;若新增逻辑后启动时栈告警/溢出,提到 4608 并在 README 记一笔。

**构建**:`idf.py -C apps/busy_knobs build`(命令行,不用 MCP build)。因动了 `ledstrip_fx`,
**另跑 `idf.py -C apps/tilt_maze build` 验证组件改动不破坏其它 app**。烧录由用户手动
(`tools/flash_one.sh busy_knobs`),WSL 环境不烧录。

---

## 1. 增量 1:柱顶小脸活化

### 1.1 数据结构改造(前置)

`make_column()` 里眼/瞳/嘴目前是局部变量,需提升为静态数组供运行时改:

```c
static lv_obj_t *s_eye[KNOB_COUNT][2];     // 眼白(左/右,8×8 圆)
static lv_obj_t *s_pupil[KNOB_COUNT][2];   // 瞳孔(4×4,眼白的子对象)
static lv_obj_t *s_mouth[KNOB_COUNT];      // 嘴
static lv_obj_t *s_cheek[KNOB_COUNT][2];   // 鼓腮(新增,默认 HIDDEN)
```

⚠️ 瞳孔是眼白的子对象:LVGL 9 默认把子对象裁剪进父对象区域——眨眼把眼白压扁后瞳孔应被
自动裁掉。**实现时查 MCP 确认本版本 LVGL 的裁剪默认行为**;若不裁剪,眨眼时手动
`lv_obj_add_flag(pupil, LV_OBJ_FLAG_HIDDEN)`。

### 1.2 眨眼

- 全局一个调度器(`face_tick()`,每帧从 `game_task` 调,仅 AWAKE + `ST_PLAY`):
  倒计时到 0 → 随机挑 1 根柱,把两只眼白 `lv_obj_set_height(…, 2)`(8×8→8×2,眼白是
  TOP_MID 对齐,压扁方向自然向下,似合眼);持续 `BLINK_FRAMES`(3 帧 ≈100ms)后恢复 8×8。
- 恢复后按 `BLINK_DOUBLE_PCT`(20%)概率紧接着再眨一次(双眨更像生物)。
- 下次眨眼间隔:`BLINK_MIN_S`~`BLINK_MAX_S`(2~6s)均匀随机。
- 状态量:`s_blink_col`(-1=无)、`s_blink_frames`、`s_blink_next`(帧倒计时)。
- 眨眼纯装饰:WIN/SINK 状态、打盹中不跑(定时器冻结即可,不必复位)。

### 1.3 看向(瞳孔追随)

- 任一旋钮 `inc != 0`(在 `apply_rotation(i,…)` 入口处,含顶/底清零转动)→
  `s_gaze_target = i; s_gaze_frames = GAZE_HOLD_MS/POLL_PERIOD_MS`。
- 生效时,每根柱 j 的两颗瞳孔重对齐:`lv_obj_align(pupil, LV_ALIGN_CENTER, dx, 0)`,
  `dx = (i > j) ? +GAZE_DX : (i < j) ? -GAZE_DX : 0`(GAZE_DX=2,4px 瞳孔在 8px 眼白内不出界)。
- **只在 target 变化或超时归位时批量重对齐一次**,不要每帧对齐 16 颗瞳孔。
- 超时(`s_gaze_frames` 减到 0)→ 全部瞳孔回中。持续转动会不断刷新倒计时,天然"边转边看"。
- 眨眼与看向共存无需特判(眨眼只动眼白高度)。

### 1.4 嘴随高度(3 档)

- 档位阈值:`0 ≤ lv < MOUTH_SMILE_LV`(9)= **平嘴**(现状:10×4,r2,`MOUTH_COLOR`);
  `MOUTH_SMILE_LV ≤ lv < MOUTH_OPEN_LV`(18)= **微笑**(14×5,r3,同色);
  `lv ≥ MOUTH_OPEN_LV` = **张嘴笑**(10×8,r4,深一号橙 `0xD46A1A`)。
- 实现 `face_set_mouth(i)`:按 `s_level[i]` 设尺寸/圆角/颜色 + 重新 `lv_obj_align`
  (TOP_MID, 0, 15)。**只在跨阈值时调**(在 `apply_rotation` 档位落定后判断新旧档所属区间),
  另外这些整体重摆点也要全量刷一遍:`unit_attach`、`trigger_shuffle`、WIN 收场落回 0 后。

### 1.5 到顶鼓腮

- 两颗 6×6 圆(粉 `0xFFAFA3`),对齐嘴两侧(TOP_MID 偏移 ±11, 15),默认 HIDDEN。
- 规则:`s_level[i] == LEVEL_MAX` 期间显示,跌下来即隐藏(在档位变化处同步,与 1.4 同点维护)。
- 与现有"咔哒到顶"音效/震动叠加,无需新音。

---

## 2. 增量 2:小鸟访客 + 爬楼梯叙事

### 2.1 精灵

- 容器:`lv_obj_create(scr)` + `lv_obj_remove_style_all`(**无背景,透明容器**),22×18,
  `LV_OBJ_FLAG_CLICKABLE` 移除;只移动容器。
- 子块(全部 `plain()` 不透明):身体 16×13 圆角 6(白天 `0xFFB35C` / 夜 `0xB8B4E0`,
  昼夜切换在 `scene_apply()` 里换色);肚皮 8×6 圆(`0xFFF1D6`);眼 3×3(`EYE_PUPIL`);
  喙 5×4 r1(`0xFB8B24`);翅 7×5 r2(比身体深一号)。摆位实现者自定,像"圆鸟"即可。
- 栖息坐标:落在柱 i 顶 = `x = col_x(i) + (COL_W-22)/2`,`y = SCREEN_H - level_h(s_level[i]) - 18`
  (鸟脚贴柱顶,不遮小脸——脸在柱顶内侧下方)。
- 移动一律 `lv_anim`(x、y 两条,exec 回调分别包 `lv_obj_set_x/set_y` 的静态函数);
  动画完成判定用**帧倒计时**(时长/POLL_PERIOD_MS),不用 anim 回调(避免跨任务踩状态)。

### 2.2 状态机(`bird_tick()`,每帧从 `game_task` 调)

```
BIRD_ABSENT ──(拜访计时到 & AWAKE & ST_PLAY & s_unit_ok)──► BIRD_FLY_IN(~700ms)
BIRD_FLY_IN ──► BIRD_PERCHED(栖息 8~15s)──► BIRD_FLY_OUT(~600ms)──► BIRD_ABSENT
BIRD_PERCHED/ABSENT ──(图案彩蛋命中,见 §3)──► BIRD_RIDE(逐柱蹦跳)──► BIRD_PERCHED
```

- **ABSENT**:`s_bird_visit` 帧倒计时(`BIRD_VISIT_MIN_S`~`MAX_S` 随机,20~45s),
  只在 AWAKE + ST_PLAY + `s_unit_ok` 时递减。到 0 → FLY_IN。
- **FLY_IN**:从随机一侧屏外(x = -24 或 320+2)、y≈40 飞到**当前最高柱**(并列取最左)
  柱顶,x 线性 + y `ease_out`,`BIRD_FLY_MS`(700ms);起飞时两音啁啾
  `{{1568,40,35},{2093,60,35}}`。落定 → PERCHED,记 `s_bird_col`。
- **PERCHED**:
  - 栖息倒计时 `BIRD_PERCH_MIN_S`~`MAX_S`(8~15s)随机,到时 FLY_OUT。
  - 所栖柱档位变化 → 受惊小跳:y 动画 -8px overshoot 一下,并重贴新柱顶(y 重新计算);
    配一声 30ms/1760Hz/amp30 的短啾。孩子会发现"转小鸟站的那根它会跳"——这是彩蛋,别删。
  - PAT_EQUAL 命中且鸟在场 → 原地开心双跳(两次 -8px 小跳),不 RIDE。
- **RIDE**(核心叙事,见 §3 表):沿指定方向逐柱蹦跳,每跳 `BIRD_HOP_MS`(110ms):
  x 线性到下一柱中心、y overshoot 到下一柱顶;**每次落地播 1 音**
  `{ level_hz(s_level[col]), BIRD_NOTE_MS, BIRD_NOTE_AMP }`(单音一次 `play_notes`,
  逐跳发,天然音画同步且绕开 400ms 上限;夜晚 amp 用夜值)。跳完停在末柱 → PERCHED,
  栖息计时重置。RIDE 进行中忽略新的图案命中(音/跳/灯照发,只是不再叠加 RIDE)。
- **FLY_OUT**:飞向较近一侧屏外,y 渐升,~600ms → ABSENT(重摇拜访计时)。

### 2.3 打断与边界(必须全覆盖)

| 事件 | 小鸟处置 |
|---|---|
| `enter_win()` | 立即 FLY_OUT(动画照飞,庆祝不等它) |
| `trigger_shuffle()` | **受惊 FLY_OUT**(被"哗啦"吓走——刻意的笑点,孩子会为了吓小鸟再摇) |
| `unit_lost()`(拔线) | FLY_OUT;ABSENT 期间 `s_unit_ok==false` 不再拜访 |
| 进 NAP / DEEP | **瞬时隐藏**(`HIDDEN` + 状态置 ABSENT,不放飞行动画——省电态不许跑动画) |
| 唤醒 | 保持 ABSENT,拜访计时重新随机 |
| DEEP 恢复后 `unit_attach` | 无特殊处理(鸟状态已是 ABSENT) |

实现建议:`bird_tick()` 每帧最先检查"当前是否允许小鸟存在"(AWAKE+ST_PLAY+unit_ok),
不允许且非 ABSENT → 按上表收场;允许才走状态机。所有 lv_anim 删除用配对 exec 回调。

---

## 3. 增量 3:图案彩蛋反馈差异化

### 3.1 现有函数改造

- `pattern_reward(void)` → `pattern_reward(pattern_t p)`(调用点在 `apply_rotation` 尾部)。
- `wave_bounce(int lift, int step_ms)` → `wave_bounce(int lift, int step_ms, wave_dir_t dir)`:

```c
typedef enum { WAVE_L2R, WAVE_R2L, WAVE_IN, WAVE_OUT, WAVE_ALL } wave_dir_t;
// 每柱延迟:L2R = i*step;R2L = (7-i)*step;IN = min(i,7-i)*step(两端先跳);
// OUT = (3 - min(i,7-i))*step(中间先跳);ALL = 0(齐跳)
```

- `play_row_arp(uint8_t amp)` → `play_row_arp(uint8_t amp, bool reverse)`(reverse=右→左取音)。

### 3.2 五种图案的反馈表(逐项照做)

| 图案 | 音频 | 柱跳 | 灯带(§6 新特效) | 震动 | 小鸟 RIDE 方向 |
|---|---|---|---|---|---|
| `PAT_UP` 上楼梯 | arp 左→右(自然上行) | `WAVE_L2R` | `LED_FX_SWEEP_L2R` | `HAPTIC_COLLECT` | 0→7(爬上去) |
| `PAT_DOWN` 下楼梯 | arp **右→左**(听感仍上行,方向与灯/跳一致) | `WAVE_R2L` | `LED_FX_SWEEP_R2L` | `HAPTIC_COLLECT` | 7→0(爬上去) |
| `PAT_EQUAL` 一条线 | 全体齐唱一个长音 `{level_hz(共同档), EQUAL_NOTE_MS, EQUAL_NOTE_AMP}` | `WAVE_ALL`(齐跳) | `LED_FX_FLASH`(整条暖白柔亮一下) | `HAPTIC_BUMP_MED` | 不 RIDE;在场则原地双跳 |
| `PAT_MOUNTAIN` 山 | arp 左→右(自然升降,"听出山形") | `WAVE_IN`(两端→中心) | `LED_FX_GATHER` | `HAPTIC_COLLECT` | 0→7(翻山) |
| `PAT_VALLEY` 谷 | arp 左→右(自然降升) | `WAVE_OUT`(中心→两端) | `LED_FX_SPREAD` | `HAPTIC_COLLECT` | 0→7 |

- 音频(arp/长音)在**命中当帧立刻发**(<100ms 即时因果),小鸟 RIDE 是后到的加演:
  鸟在场(PERCHED)→ 直接 RIDE;鸟 ABSENT 且图案 ∈ {UP, DOWN, MOUNTAIN, VALLEY} →
  **快速飞入**(FLY_IN 加速为 ~500ms,目标 = RIDE 起点柱)然后 RIDE。RIDE 落地音 amp 较低
  (BIRD_NOTE_AMP=45),不与 arp 打架。
- `s_last_pattern` 防重贺机制**原样保留**;`trigger_shuffle` 尾部的
  `s_last_pattern = detect_pattern()` 也保留(摇出来的队形不贺、鸟已被吓飞)。
- "全 8 满 → `enter_win`"优先级最高,先于图案判定 return(现状already如此,勿动)。

---

## 4. 增量 4:夜晚声音世界 + 星星微闪

### 4.1 夜晚音色

- 新增夜用音阶表(整体下移纯四度,**不是降八度**——NS4168 小喇叭 200Hz 以下还原差):

```c
static const uint16_t PENTA_HZ_NIGHT[] =
    { 196, 220, 262, 294, 330, 392, 440, 523, 587, 659, 784 };  // 与 PENTA_HZ 等长
```

- `level_hz()` 按 `s_night` 选表(表长相同,索引公式不变)。
- 响度/时长换夜值:转动"叮"用 `TICK_MS_NIGHT/TICK_AMP_NIGHT`(60ms/30),按下唱歌用
  `SING_AMP_NIGHT`(55),图案 arp 用 `ARP_AMP_NIGHT`(40),小鸟落地音 `BIRD_NOTE_AMP_NIGHT`(32)。
  建议加内联小工具(如 `fx_tick_amp()`)集中取值,别在各处写三目。
- 昼夜切换音(`scene_toggle_feedback`)、win/shuffle 音效维持现状不动。

### 4.2 星星微闪

- 仅 `s_night && AWAKE` 时跑(并入 `face_tick()` 或独立 `stars_tick()`)。
- 3 颗星各自独立相位:每颗一个帧倒计时,随机 `TWINKLE_MIN_F`~`TWINKLE_MAX_F`(24~60 帧,
  即 0.8~2s)到点切换状态:**亮态** 6×6 `STAR_COLOR` ↔ **暗态** 4×4 `0xC9B67A`
  (切暗时 `set_pos` 原坐标 +1,+1 保持视觉中心;切亮时还原)。
- 无 alpha、无逐帧动画——只是低频状态切换,单颗脏矩形 6×6px,预算可忽略。
- 切回白天(`scene_apply`)时把 3 颗星复位为亮态尺寸/坐标(它们本来就会被 HIDDEN)。

### 4.3 小鸟夜色

- `scene_apply()` 里同步换小鸟身体色(白天 `0xFFB35C` / 夜 `0xB8B4E0`),其余子块不换。

---

## 5. 增量 5:多键齐按 = 和弦彩蛋

### 5.1 收集窗口(解决"孩子的'同时'≈±50ms")

- `poll_unit()` 按键边沿处理改为**两帧收集窗口**:
  - 本帧出现新按下(0→1)时,不立刻 `sing()`,置 `s_chord_pending[i]=true`;若窗口未开,
    开窗 `s_chord_wait = CHORD_WINDOW_FRAMES`(2)。
  - 每帧递减 `s_chord_wait`,归 0 时结算:pending 数量 `n==1` → 原 `sing(i)`;
    `n>=2` → `chord_burst(pending)`(见下)。
  - 窗口引入最多 ~66ms 延迟,仍在 <100ms 红线内;**只对按键生效,转动路径不动**。
- 结算发生在 `awake_play` 条件下;若窗口开着时状态跑进 WIN/打盹,直接丢弃 pending(不结算)。

### 5.2 `chord_burst(被按下的柱集合)`

- **音**:把被按柱按 `s_level` 升序排,组一次 `play_notes`:每音
  `{ level_hz(lv), CHORD_NOTE_MS, CHORD_NOTE_AMP }`(35ms/60;≤8 音 ≤280ms,合规)——
  快速琶音,听感即"和弦"。
- **画**:被按各柱同时做 `sing()` 里的弹跳动画。为此把 `sing()` 拆成
  `sing_anim(i)`(纯动画)+ 音/震/灯部分,单按路径行为不变。
- **震**:一次 `HAPTIC_BUMP_MED`(不逐柱叠加)。
- **灯**:被按各柱就地灯闪白(复用 `s_led_flash[i]=5` 机制);底座 `LED_FX_FLASH`。
- 不参与图案判定/不改档位;连打不设冷却(音频队列满了自然丢,可接受)。

---

## 6. `ledstrip_fx` 组件扩展(唯一组件改动)

`led_fx_t` **尾部追加**(不得改动既有值序):

```c
    LED_FX_SWEEP_L2R,   // 定向扫:COLLECT 同款遍历序正向
    LED_FX_SWEEP_R2L,   // 定向扫:反向
    LED_FX_GATHER,      // 两端 → 中间聚拢
    LED_FX_SPREAD,      // 中间 → 两端散开
    LED_FX_FLASH,       // 整条暖白柔亮一下(≈250ms 起落,无频闪感)
```

实现要点:
- **先读现有 `ledstrip_fx.c` 里 `LED_FX_COLLECT`("一圈高亮扫过")的遍历序**,它已隐含
  10 颗灯的物理环序;`SWEEP_L2R` 直接复用该序,`R2L` 反向,`GATHER/SPREAD` 以该序的
  两端/中点为起点对称推进。灯的物理左右若与屏幕左右存在镜像疑义,查
  `docs/platform/M5GO_Bottom2.md`;查不到就按 COLLECT 序落地并在 README 标注
  "扫向与屏幕左右的对应关系待实机确认(不符则序表取反,一行改)"。
- 颜色统一暖金(与 COLLECT 风格一致);时长 400~500ms;结束自动回基础模式(沿用现有机制)。
- 亮度走现有 `max_brightness` 缩放;`LED_FX_FLASH` 是**单次缓亮缓灭**,不是闪烁
  (§8 光敏安全:无快速频闪)。
- 现有 3 特效 + 4 基础模式的代码路径**一个字节都不改**。

---

## 7. `tuning.h` 新增常量(集中一处,默认值如下)

```c
// ── 小脸活化 ──────────────────────────────────────────────────────────
#define BLINK_MIN_S          2      // 眨眼间隔下限(s)
#define BLINK_MAX_S          6
#define BLINK_FRAMES         3      // 合眼持续帧数(~100ms)
#define BLINK_DOUBLE_PCT     20     // 双眨概率(%)
#define GAZE_HOLD_MS         1000   // 看向保持时长
#define GAZE_DX              2      // 瞳孔偏移(px)
#define MOUTH_SMILE_LV       9      // ≥此档 = 微笑
#define MOUTH_OPEN_LV        18     // ≥此档 = 张嘴笑

// ── 小鸟 ─────────────────────────────────────────────────────────────
#define BIRD_VISIT_MIN_S     20     // 自发拜访间隔(s,随机区间)
#define BIRD_VISIT_MAX_S     45
#define BIRD_PERCH_MIN_S     8      // 栖息时长(s,随机区间)
#define BIRD_PERCH_MAX_S     15
#define BIRD_FLY_MS          700    // 飞入/飞出时长(图案召唤时飞入用 500)
#define BIRD_HOP_MS          110    // RIDE 每跳时长
#define BIRD_NOTE_MS         45     // 落地音时长
#define BIRD_NOTE_AMP        45     // 落地音响度(白天)
#define BIRD_NOTE_AMP_NIGHT  32

// ── 图案差异化 ────────────────────────────────────────────────────────
#define WAVE_STEP_MS         45     // 波浪错峰步进(原 pattern_reward 硬编码 45 移进来)
#define EQUAL_NOTE_MS        280    // 一条线齐唱长音
#define EQUAL_NOTE_AMP       60
#define ARP_AMP_NIGHT        40

// ── 夜晚 ─────────────────────────────────────────────────────────────
#define TICK_MS_NIGHT        60
#define TICK_AMP_NIGHT       30
#define SING_AMP_NIGHT       55
#define TWINKLE_MIN_F        24     // 星星切换间隔(帧,0.8~2s)
#define TWINKLE_MAX_F        60

// ── 和弦 ─────────────────────────────────────────────────────────────
#define CHORD_WINDOW_FRAMES  2      // 齐按收集窗口(帧;2 帧≈66ms,仍<100ms 红线)
#define CHORD_NOTE_MS        35
#define CHORD_NOTE_AMP       60
```

---

## 8. 交互优先级 / 冲突矩阵(结算顺序)

1. **全 8 满 `enter_win`** 最高,命中即 return(现状,勿动);小鸟 FLY_OUT。
2. **摇一摇 `trigger_shuffle`**:吓飞小鸟;洗出的队形不触发图案彩蛋(现有 `s_last_pattern` 机制)。
3. **图案彩蛋**:音/跳/灯/震当帧发;小鸟 RIDE 为加演,RIDE 中不叠加新 RIDE。
4. **和弦窗口**:仅按键路径;窗口期跨入非 `awake_play` 则丢弃。
5. **省电**:小脸/星星/小鸟 tick 全部只在 AWAKE 跑;小鸟进 NAP/DEEP 瞬时隐藏;
   这些装饰活动**不 kick 休眠**。DEEP 恢复的 `unit_attach` 重摆(§1.4)要包含嘴型/鼓腮全量刷新。

---

## 9. 验收清单

**构建**:
- [ ] `idf.py -C apps/busy_knobs build` 通过;槽内体积仍有余量(记录 bin 大小)。
- [ ] `idf.py -C apps/tilt_maze build` 通过(ledstrip_fx 改动不破坏它)。

**实机点检**(用户烧录后逐条):
- [ ] 随机眨眼可见;转某旋钮时全排瞳孔看向它,~1s 后回中。
- [ ] 柱升到 9/18 档嘴型变;到顶出鼓腮,跌下即收。
- [ ] 干等 20~45s 小鸟飞入落最高柱;转它站的柱它受惊小跳;8~15s 后飞走。
- [ ] 摆出上楼梯:arp+波浪+灯带全部左→右,小鸟(飞来)从矮到高逐柱蹦、每跳一音;
      下楼梯全套右→左;一条线=齐唱长音+齐跳+整条柔亮;山=向中聚拢;谷=向外散开。
- [ ] 同一图案不重复贺(现有机制未破坏);全 8 满大庆祝优先且正常;摇一摇吓飞小鸟。
- [ ] 拨到夜晚:音整体变低变柔;星星各自慢闪;小鸟换夜色;拨回白天全部还原。
- [ ] 双手拍 ≥2 个按键:快速琶音 + 被按柱齐跳 + 灯闪;单按行为与改前一致。
- [ ] 打盹/深度省电如常进出;小鸟在打盹瞬间消失不残留;DEEP 唤醒后嘴型/鼓腮/就地灯正确重建。
- [ ] 拔线 → 提示卡如常,小鸟飞走;插回即恢复。

**收尾**:
- [ ] README「趣味增量」补第二批条目(定案数值),新调参项并入「待实机标定」。
- [ ] 删除/归档本 FUN2_SPEC.md。
