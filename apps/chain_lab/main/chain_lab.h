// chain_lab —— Chain Encoder + Chain Joystick 驱动验证台(UI + 20Hz 轮询任务)
#pragma once

/** @brief 建 UI、探测 Chain 节点(PORT.C)、起 20Hz 验证任务(core2_board_init 之后调)。 */
void chain_lab_start(void);
