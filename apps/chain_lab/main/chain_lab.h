// chain_lab —— Chain Encoder + Chain Joystick 传输/绑定层(SPEC.md §1 红线:协议层不改)
//
// 收窄为纯"传输/绑定层":scan_bus/poll_enc/poll_joy/node_rgb/hue2rgb/joy_calibrate_center
// 原样保留;20Hz game_task + core2_sleep 集成 + 深度省电重扫原样保留。真正的"游戏"在
// crane_game.c(CHAIN_LAB_DIAG_MODE=0,默认)或隐藏诊断表盘/光点 UI(=1,tuning.h 编译期
// 开关,SPEC §7 方案B)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief 建 UI、探测 Chain 节点(PORT.C)、起 20Hz 任务(core2_board_init 之后调)。 */
void chain_lab_start(void);

// ── 游戏层输入来源(game_task 每帧经 poll_enc(false)/poll_joy(false) 刷新)───────────
bool    chain_lab_enc_attached(void);
int16_t chain_lab_enc_value(void);      // 绝对计数(帧间 delta 由调用方自算,见 SPEC §2)
bool    chain_lab_enc_button(void);     // true = 按住

bool    chain_lab_joy_attached(void);
float   chain_lab_joy_x(void);          // 归一化 -1..1(已过标定/死区/轴向裁剪)
float   chain_lab_joy_y(void);
bool    chain_lab_joy_button(void);

// ── 节点 RGB 写入(节流,复用 node_rgb();未接该节点时静默忽略)──────────────
void    chain_lab_enc_rgb(uint8_t r, uint8_t g, uint8_t b);
void    chain_lab_joy_rgb(uint8_t r, uint8_t g, uint8_t b);

// ── 色相环(派对彩虹复用,SPEC §1)─────────────────────────────────────────
void    chain_lab_hue2rgb(int h, uint8_t *r, uint8_t *g, uint8_t *b);
