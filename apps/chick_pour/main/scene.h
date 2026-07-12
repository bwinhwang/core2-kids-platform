// 后院版面(SPEC §2/§7)—— 静态层几何 + 一次性绘制 + 探头计数 + 派对视觉。
// 几何常量与渲染共用同一份源(scene.c 里算出的 rect/circle 既用来画、也直接喂给
// flock.c 做碰撞/门判定),避免"画的和碰的对不上"。本头文件不依赖 LVGL,flock.c
// (纯物理,不碰 LVGL)可以放心 #include。
#pragma once

// ── 版面尺寸(SPEC §9 未列具体像素,按 §2 布局描述取值)─────────────────
typedef struct { float x0, y0, x1, y1; } rect_t;
typedef struct { float x, y, r; } circ_t;

#define FENCE_THICK   10.0f   // 四周木栅栏厚度(视觉边框 = 碰撞边界)
#define HOUSE_W       46.0f   // 鸡窝(左边缘中段)外墙宽
#define HOUSE_H       70.0f   // 鸡窝外墙高(高于门区 GATE_W=44,门上下留墙肩)
#define POND_W        46.0f   // 池塘(右边缘中段)外墙宽
#define POND_H        70.0f
#define BUSH_INSET    26.0f   // 四角灌木圆心相对屏幕角的内缩(圆允许部分越界到画面外)
#define GATE_DEPTH    10.0f   // 门区从家墙面向场地伸出的深度:> ANIMAL_R=8,保证动物中心
                              // 先进门区(判定接管)、再才可能碰家墙 —— 门判定天然先于墙碰撞

/** @brief 动物中心活动边界(已扣 ANIMAL_R,栅栏内墙)。 */
extern const rect_t PLAY_BOUNDS;
/** @brief 鸡窝外墙碰撞矩形(门区段豁免,见 HOUSE_GATE)。 */
extern const rect_t HOUSE_RECT;
/** @brief 池塘外墙碰撞矩形(同上)。 */
extern const rect_t POND_RECT;
/** @brief 鸡窝门区(门口朝右,SPEC §5.2):动物中心进入即判定,种类匹配捕获/不匹配弹出。
 *         跨骑墙面:向场内伸 GATE_DEPTH、向墙内嵌少许(动物中心在门区内时家墙碰撞豁免)。 */
extern const rect_t HOUSE_GATE;
/** @brief 池塘门区(缺口朝左),同上。 */
extern const rect_t POND_GATE;
/** @brief 四角灌木碰撞圆(把直角变斜坡,防动物堆死在角落)。 */
extern const circ_t CORNER_BUSH[4];

/** @brief 画静态层一次:地面/栅栏/鸡窝(带门洞)/池塘(带浅滩缺口)/四角灌木 +
 *         预建 5+5 个探头小脸(hidden)。须在 bsp_display_start 之后、critters_init()
 *         之前调(动物精灵要叠在场景上面)。全程不透明色块、零运行时 alpha。 */
void scene_init(void);

/** @brief 家门口探头小脸计数(SPEC §5.3):kind 0=鸡窝窗口冒鸡头 / 1=池塘水面冒鸭头,
 *         显示前 n 个(0..5)。归家/重散时各调一次,不逐帧。 */
void scene_set_home_count(int kind, int n);

/** @brief 派对(SPEC §6):两家一起上下弹跳(lv_anim translate_y,重复数次后自然停回
 *         原位,总时长 ≈ PARTY_HOLD_MS)。由 feedback 任务调用(LVGL 加锁,跨任务安全)。 */
void scene_party_bounce(void);

/** @brief 派对限量彩纸:CONFETTI_N(≤8)片轻柔飘落后自删(§6.5 庆祝档)。 */
void scene_confetti(void);
