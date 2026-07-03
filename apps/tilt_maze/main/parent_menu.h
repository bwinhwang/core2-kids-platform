// 家长隐藏菜单 (CLAUDE.md §13)。
// 入口:屏幕底部长按 3s(幼儿不会无意触发)。
// 菜单项:亮度 / 音量 / 震动开关 / 难度档 / 重新校准 / 返回。
// 注:用英文标签 + LVGL 内置符号(默认字体无中文字形);中文字体可后续加。
#pragma once

/** @brief 装好底部长按入口 + 设 indev 长按时间。须在 bsp_display_start 之后。 */
void parent_menu_init(void);
