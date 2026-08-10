// 后院版面的**绘制侧**(SPEC §2/§7/§13):静态层一次画完 + 换图纸整屏重画 +
// 探头计数 + 派对视觉。几何真源在 layout.h(不依赖 LVGL、主机可编译校验),
// 这里画的就是那份几何,flock.c 碰的也是那份几何 —— "画的和碰的"对得上。
//
// 2026-08-10「后院图纸批」:家的位置/朝向从写死常量改成运行时选定的「图纸」
// (blueprint_t,数值与 tools/preview.py 的 BLUEPRINTS 一一对应,改一边就改另一边)。
#pragma once

#include "layout.h"   // rect_t/circ_t/blueprint_t + HOUSE_RECT 等几何全局量

/** @brief 画静态层一次:地面/栅栏(图纸无关,恒定)+ 图纸自检 + 应用图纸 A
 *         (家/门/灌木/预建 5+5 个探头小脸)。须在 bsp_display_start 之后、
 *         critters_init() 之前调(动物精灵要叠在场景上面)。
 *         全程不透明色块、零运行时 alpha。 */
void scene_init(void);

/** @brief 图纸总数(= layout_count()),供 game_state 的轮换取模。 */
int scene_blueprint_count(void);

/** @brief 当前生效的图纸下标(已计入校验失败的回退)。game_state 用它作为派对后
 *         +1 轮换的起点。 */
int scene_current_blueprint(void);

/** @brief 切换到第 idx 张图纸:layout_apply()(校验 + 提交几何,失败回退图纸 A)
 *         → 清掉旧静态层 LVGL 对象、整屏重画一次(§6.1 允许的"进关/换场景"整屏重绘)。
 *         须在 PARTY 倒计时结束、flock_scatter() 之前调(§5.4)。 */
void scene_apply_blueprint(int idx);

/** @brief 家门口探头小脸计数(SPEC §5.3):kind 0=鸡窝窗口冒鸡头 / 1=池塘水面冒鸭头,
 *         显示前 n 个(0..5)。归家/重散时各调一次,不逐帧。 */
void scene_set_home_count(int kind, int n);

/** @brief 派对(SPEC §6):两家一起上下弹跳(lv_anim translate_y,重复数次后自然停回
 *         原位,总时长 ≈ PARTY_HOLD_MS)。由 feedback 任务调用(LVGL 加锁,跨任务安全)。
 *         须在 scene_apply_blueprint() 切下一张图纸之前调完(同一轮内两家对象才有效,
 *         PARTY_HOLD_MS 足够盖过弹跳动画时长,见 game_state party_tick 时序)。 */
void scene_party_bounce(void);

/** @brief 派对限量彩纸:CONFETTI_N(≤8)片轻柔飘落后自删(§6.5 庆祝档)。 */
void scene_confetti(void);
