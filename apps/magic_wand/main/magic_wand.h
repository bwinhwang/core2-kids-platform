// magic_wand —— 主任务:轮询手势、派发法术、省电挂载、单元容错
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 建场景 + 起 game_task。调用方(app_main.c)在 core2_board_init 之后调一次。 */
void magic_wand_start(void);

#ifdef __cplusplus
}
#endif
