// busy_bus —— Chain Joystick 传输/绑定层(SPEC.md §1 红线:形状抄 chain_lab,收窄为
// joystick-only)。20Hz game_task + core2_sleep 集成 + 深度省电重扫在这里跑;真正的
// "游戏"在 bus_game.c(town_game_tick 由本层每帧调用)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief 建 UI、探测 Chain 节点(PORT.C)、起 20Hz 任务(core2_board_init 之后调)。 */
void bus_link_start(void);

// ── 游戏层输入来源(game_task 每帧经 poll_joy(false) 刷新)──────────────────
bool  bus_link_joy_attached(void);
float bus_link_joy_x(void);          // 归一化 -1..1(已过标定/死区/轴向裁剪)
float bus_link_joy_y(void);
bool  bus_link_joy_button(void);

// ── 节点 RGB 写入(节流,复用 node_rgb();未接该节点时静默忽略)──────────────
void  bus_link_joy_rgb(uint8_t r, uint8_t g, uint8_t b);

// ── 色相环(派对彩虹复用)─────────────────────────────────────────────
void  bus_link_hue2rgb(int h, uint8_t *r, uint8_t *g, uint8_t *b);
