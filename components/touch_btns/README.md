# touch_btns —— 屏下三个圆圈键区(BtnA/BtnB/BtnC)

Core2 屏幕**下方那三个丝印圆圈**做成的一套**跨 app 一致的全局三键**。所有 app(含四张
as-built 游戏)经 `core2_board_init` 白拿,**app 侧无需任何代码**。

## 为什么要单独一个组件(不是 LVGL 按钮)

Core2 的 FT6336U 触摸面板物理尺寸 **320×280**,比 LCD(320×240)向下多 40px,三个圆圈落在
**y≥240 的屏外扩展区**。BSP 只把触摸挂给 LVGL indev(画布仅 240 高),圆圈的触点落在所有
LVGL 对象之外、走不到点击回调。所以本组件**旁路 LVGL**,自己低频轮询 FT6336U(内部 I2C
`0x38`)原始坐标,按 y 阈值 + x 三分区把圆圈识别成 BtnA/B/C。

- 与 BSP 的 lvgl_port 触摸读**并存无副作用**:两边都只读当前触点、不清状态,I2C 总线自带互斥。
- INT 脚(G39)已被 BSP touch handle 独占 → 本组件走**轮询**(33Hz),不碰 INT。
- 驱动侧已核实 `esp_lcd_touch` **不把 y 夹到 y_max**(mirror 关时 `y_max` 不参与),故 y>240 原样透传。

## 三键默认功能(分级防误触)

| 键 | 坐标区 | 默认动作 | 触发 | 为什么这样 |
|---|---|---|---|---|
| **BtnA** 左 | x<148, y≥240 | 回 launcher | **长按 ~800ms** | 破坏性→长按防误触;填补"进 app 出不来"的空白 |
| **BtnB** 中 | 148≤x<228 | 截屏 | **短按** | 非破坏性→短按顺手 |
| **BtnC** 右 | x≥228 | 关机(AXP192 0x32 bit7)| **长按 ~1500ms** | 最不可逆→门槛最高 |

反馈:每次按下先一下轻震(HELLO)="按到了";A 触发欢庆震(WIN)后回 launcher,C 触发
报警震(BUMP_HARD)后断电;B 一下 COLLECT 震=**已触发**。app 绑的回调自带反馈由 app 决定。

## 重绑(app 覆盖任意键)

每个键的**短按 / 长按都是可覆盖的槽位**(共 3×2=6 个槽)。`touch_btns_bind()` 绑回调即覆盖该槽,
`cb=NULL` 恢复内置默认;未绑的槽走上表默认,其余槽(A短按 / B长按 / C短按)默认无动作、绑了才有。
零代码 app 仍白拿全局三键。

```c
#include "touch_btns.h"

static void my_page_flip(void *user) { /* 翻页 */ }

void app_main(void) {
    core2_board_init(NULL);                    // 三键已就绪(默认 Home/截屏/关机)
    // 把 BtnB 短按从"截屏"改成"翻页":
    touch_btns_bind(TOUCH_BTN_B, TOUCH_BTN_SHORT, my_page_flip, ctx);
    // 想屏蔽某默认动作:绑一个空回调(如续航测试防误退)
    touch_btns_bind(TOUCH_BTN_A, TOUCH_BTN_LONG, my_confirm_then_home, NULL);
}
```

⚠️ 回调在**轮询任务**上下文里被调用(非 LVGL 任务)——碰 LVGL 请自行 `bsp_display_lock/unlock`。
建议在 app init(UI 交互开始前)绑定;绑定表单指针读写、不加锁(平台不过度加锁纪律)。

## 🔧 唯一要实机标定的旋钮

各方对"圆圈报什么 y"数据打架(社区说 y240–280,M5 `M5Button.h` 却把矩形定在 y218)——**具体
阈值只能在真机上量**。标定常量集中在 `touch_btns.c` 顶部:

```c
#define BTN_Y_MIN   240   // y≥此值才算屏下键区
#define X_SPLIT_AB  148   // BtnA / BtnB 分界(本机实测圆圈中心 109/187/269,取相邻中点)
#define X_SPLIT_BC  228   // BtnB / BtnC 分界
```

**怎么标**:首刷后开 `idf.py monitor`,按三个圆圈,组件会节流打印
`raw touch x=.. y=.. → zone ..`(键区附近 y≥200 的触点才打,不刷屏)。读出三个圆圈真实的
x/y,回填上面三个常量即可。**已按本机实测标定**(2026-07-21:圆圈中心 x≈109/187/269、
y≈271~279,整体比理论右偏 ~28px);换机复现请重新按圆圈读日志回填。

## 截屏怎么"发"(BtnB)

BtnB 调 `screenshot_dump_now()`,图**从日志串口(UART0)吐出**(平台无联网通道)。所以要有主机
在听:

```bash
python3 tools/screenshot.py --watch [PORT] [存图目录]   # 常驻监听,按 BtnB 就存一张(时间戳命名)
```

⚠️ **没有主机在听时,BtnB 照样把 Base64 吐进虚空、设备端无从知道有没有被接住**(无回传通道)。
所以按下只给"已触发"震动,给不了"已保存"。脱机(拔 USB)存盘要等 power_lab 的 SPIFFS 录制设施
就绪后复用,本组件不单开分区(`partitions.csv` 冻结)。

## 待实机点检

- [ ] 三个圆圈真实 x/y 范围,回填 `BTN_Y_MIN`/`X_SPLIT_*`(见上「怎么标」)。
- [ ] BtnA 长按 800ms 是否顺手、会不会误触;BtnC 关机 1500ms 门槛是否合适。
- [ ] BtnB + `screenshot.py --watch` 端到端存出 PNG。
- [ ] 屏内底部若有 app 画了可点对象,确认与 y≥240 键区不冲突(默认阈值 240 已避开屏内)。
