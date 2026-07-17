# ui_kit —— 评估台 UI 控件(状态栏 / 数值卡 / chart / 列表菜单)

给评估 app 提供一套统一的"深灰工程风"数值/图表 UI,全守 `CLAUDE.md` §6 渲染红线:
每个控件创建时画一次静态层(边框/标签),之后的 update/push 只改动态部分,天然只脏
自身矩形。v1 全 ASCII(CJK 字体暂不引入,见 `CLAUDE.md` §8 / `docs/ROADMAP.md` 风险
记录),数值卡核心数字用 `CONFIG_LV_FONT_MONTSERRAT_24`,其余文字用 `_16`(两个字体
宏已进 `sdkconfig.platform`)。

## 用法

```c
lv_obj_t *scr = lv_screen_active();

ui_status_bar_t *bar = ui_status_bar_create(scr, "unit_bench");
ui_status_bar_update(bar, uptime_s, batt_pct, vbus_present);

ui_value_card_t *lux = ui_value_card_create(scr, 8, 32, 150, 80, "Lux", "lx");
ui_value_card_set_value(lux, 123.4f, NULL);           // 不做阈值判定
ui_value_card_set_error(lux, "TIMEOUT");              // 拔线/超时时显式红字

ui_chart_t *trend = ui_chart_create(scr, 8, 120, 180, 100, 60, 0, 65535);
ui_chart_push(trend, (int32_t)(lux_value * 10));      // 需要小数精度自行按倍数放大

ui_list_menu_t *menu = ui_list_menu_create(scr, 8, 32, 300, 180);
int row0 = ui_list_menu_add_row(menu, "0x23 DLight", false);
int row1 = ui_list_menu_add_row(menu, "背光测试", true);   // with_switch=true
ui_list_menu_on_click(menu, on_row_click, NULL);
```

## 🔴 不内部加锁

**本组件不内部包 `bsp_display_lock/unlock`**——与本仓其它组件一致(`audio_fx`/`haptics`
不碰 LVGL,launcher 的 `ui_create()` 自己在外层包一次锁做一批创建):调用方负责在自己
的 LVGL 操作外包锁,可以一次锁里批量调用多个 `ui_kit` 函数,不会因为内部重复加锁而
死锁。

## 4 个控件

| 控件 | 静态层(画一次) | 动态层(每次 update/push) |
|---|---|---|
| `ui_status_bar` | 顶 24px 背景 + app 名 | uptime / 电量 / USB 标签文字 |
| `ui_value_card` | 边框 + 标签 + 单位 | 核心数字文字(阈值外变告警色) |
| `ui_chart`(`lv_chart` 封装) | 边框 + 坐标范围 | `LV_CHART_UPDATE_MODE_SHIFT` 环形推点 |
| `ui_list_menu` | 容器 + 各行标签/开关 | `ui_list_menu_set_row_text` 改某行文字 |

`ui_chart` 的纵轴值域是 `int32_t`(LVGL chart 内部值域即 int32),需要小数精度的物理量
(如 lux)由调用方预先按固定倍数放大(`lux*10`)再推入,自己的数值卡上还原显示。

## 阈值变色 / 错误显式呈现

`ui_value_card_set_value(card, value, &thresh)`:`thresh.enabled=true` 时,
`value < warn_lo || value > warn_hi` 触发告警色(`UI_KIT_COLOR_WARN`,离散状态切换,
不做渐变过渡)。`ui_value_card_set_error(card, "TIMEOUT")` 是 `CLAUDE.md` §2 原则 2
「错误显式呈现」的直接落地——init 失败/总线拉死/拔线,一律显式红字,不静默。

## 配色(深灰工程风,与幼儿掌机时期马卡龙色系区分)

`UI_KIT_COLOR_PANEL`(0x2A2E33 面板背景)、`UI_KIT_COLOR_VALUE`(0xE8E8E8 数值文字)、
`UI_KIT_COLOR_LABEL`(0x9AA0A6 标签/次要文字)、`UI_KIT_COLOR_WARN`(0xE05A4E 告警色)、
`UI_KIT_COLOR_ACCENT`(0x4FB0D8 chart 线条/强调色)。

## API 核实记录

`lv_chart`/`lv_switch`/`lv_label` 相关函数签名(`lv_chart_set_axis_range`、
`lv_chart_set_update_mode`+`LV_CHART_UPDATE_MODE_SHIFT`、`lv_switch_create`+
`lv_obj_has_state`+`LV_STATE_CHECKED`、`LV_PCT` 宏大小写)均直接读本仓
`launcher/managed_components/lvgl__lvgl/src/**/*.h` 源码核实(本会话 Espressif MCP
不可用,AGENTS.md §1 允许的退路),未凭记忆写。
