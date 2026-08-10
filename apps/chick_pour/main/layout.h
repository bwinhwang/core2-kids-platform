// 后院版面几何的**唯一真源**:图纸(blueprint)、家/门/灌木的坐标派生、加载校验。
// 🔴 本文件与 layout.c 刻意不依赖 LVGL / BSP —— 只吃 tuning.h 和 esp_log.h,
// 于是主机上能直接编译真身跑校验(tools/verify_host.sh),不必烧板。这条约束是
// 2026-08-13 那个 bug 换来的:preview.py 的 Python 版 check_layout() 一直是过的,
// 而 C 版 scene_verify() 三张图纸全判失败、静默回退图纸 A,只有实机才看得见。
//
// 画它的是 scene.c/scene.h(LVGL);读它做碰撞/门判定的是 flock.c —— 两边共用
// 下面这组全局量,"画的和碰的"天然对得上。
#pragma once

#include <stdbool.h>

#include "tuning.h"   // ANIMAL_R / GATE_W / CORNER_BUSH_R / PLAY_W / PLAY_H

typedef struct { float x0, y0, x1, y1; } rect_t;
typedef struct { float x, y, r; } circ_t;

// ── 版面尺寸(SPEC §9 未列具体像素,按 §2 布局描述取值)─────────────────
#define FENCE_THICK   10.0f   // 四周木栅栏厚度(视觉边框 = 碰撞边界)
#define HOUSE_W       46.0f   // 鸡窝外墙宽(=家的进深,沿进深轴,三张图纸共用同一进深)
#define HOUSE_H       70.0f   // 鸡窝外墙高(沿边轴,= 13 顶带 + 44 墙身[=GATE_W] + 13 底带)
#define POND_W        46.0f   // 池塘外墙宽(同 HOUSE_W)
#define POND_H        70.0f
#define BAND          13.0f   // 顶带/底带厚(屋顶/池塘沙沿,沿边轴)
#define BUSH_INSET    26.0f   // 四角灌木圆心相对屏幕角的内缩(默认档,图纸可整只覆盖坐标/半径)
#define GATE_DEPTH    13.0f   // 门区从家墙面向场地伸出的深度。🔴 2026-08-10 美术批 ANIMAL_R
                              // 8→11 后同步从 10 上调:必须 > ANIMAL_R,否则动物中心还没进
                              // 门区判定区、圆身已经先蹭到家墙外沿,门判定就不再"天然先于
                              // 墙碰撞"(1px 级窗口,tools/preview.py check_layout() 有断言)。
#define GATE_INSET    6.0f    // 门区向墙内嵌(软分离把动物挤进墙面线内侧时仍在门区豁免范围)
#define DOOR_FRAME_D  17.0f   // 门框进深(嵌墙深度,draw_home_layer 定值)
#define SIGN_R        12.0f   // 招牌脸半径 → φ24(原 10→φ20)。🔴 46 进深(HOUSE_W/POND_W)
                              // 下的硬上限 = (进深 - 门框深 DOOR_FRAME_D - 5px 留白) / 2 = 12,
                              // 再大会压住门洞(tools/preview.py check_sign_fit());加宽家的
                              // 方案本批不做,见 tools/preview.py ui_wide 参考帧。

// ── 图纸(blueprint):家的位置/朝向 + 灌木布局,运行时三选一(§5.4,依次轮换)────
// 沿边轴恒为 y、进深轴恒为 x(tools/preview.py 文件头「零美术改动」约束:家的三段式
// 立面与门三件套原样复用,门只朝左或朝右)。
typedef enum {
    HOME_FACE_RIGHT = 0,   // 家在 anchor 左侧,门朝右(开口伸向 +x)
    HOME_FACE_LEFT  = 1,   // 家在 anchor 右侧,门朝左(开口伸向 -x)
} home_face_t;

typedef struct {
    home_face_t face;
    float       anchor_x;  // 门面所在的 x(= tools/preview.py Home.anchor)
    float       cy;        // 沿边轴中心 y
} home_spec_t;

// homes[] 的下标 = flock.h 的 animal_kind_t(scene.c 有 _Static_assert 卡住两者一致):
// [0]=ANIMAL_CHICK 恒画鸡窝外观、[1]=ANIMAL_DUCK 恒画池塘外观(哪只动物配哪种家不随
// 图纸变,变的只是家摆在哪、门朝哪 —— SPEC §2)。
enum { LAYOUT_HOME_CHICK = 0, LAYOUT_HOME_DUCK = 1 };

typedef struct {
    const char *name;      // 日志用(图纸切换/校验失败时打印)
    home_spec_t homes[2];
    circ_t      bushes[4];
} blueprint_t;

#define SCENE_BLUEPRINT_COUNT 3   // 本批落地 A/C/B 三张(§5.4);E 漏斗/D 中央岛留在
                                  // tools/preview.py 候选池,本批不落地(评审结论)

// ── 当前生效几何(layout_apply 写入,scene.c 照着画、flock.c 照着碰)──────────
/** @brief 动物中心活动边界(已扣 ANIMAL_R,栅栏内墙;图纸无关,恒定)。 */
extern const rect_t PLAY_BOUNDS;
/** @brief 鸡窝外墙碰撞矩形(门区段豁免,见 HOUSE_GATE)。随当前图纸变。 */
extern rect_t HOUSE_RECT;
/** @brief 池塘外墙碰撞矩形(同上)。 */
extern rect_t POND_RECT;
/** @brief 鸡窝门区(SPEC §5.2):动物中心进入即判定,种类匹配捕获/不匹配弹出。
 *         跨骑墙面:向场内伸 GATE_DEPTH、向墙内嵌 GATE_INSET(动物中心在门区内时
 *         家墙碰撞豁免)。随图纸变。 */
extern rect_t HOUSE_GATE;
/** @brief 池塘门区,同上。随图纸变。 */
extern rect_t POND_GATE;
/** @brief 鸡窝门朝向(弹出方向由此驱动,不再写死,见 flock.c gate_check)。随图纸变。 */
extern home_face_t HOUSE_FACE;
/** @brief 池塘门朝向,同上。随图纸变。 */
extern home_face_t POND_FACE;
/** @brief 四角灌木碰撞圆(把直角变斜坡,防动物堆死在角落)。随图纸变。 */
extern circ_t CORNER_BUSH[4];

// ── API ───────────────────────────────────────────────────────────────
/** @brief 图纸总数(= SCENE_BLUEPRINT_COUNT)。 */
int layout_count(void);

/** @brief 取第 idx 张图纸(越界钳到 0)。 */
const blueprint_t *layout_get(int idx);

/** @brief 当前生效的图纸下标(已计入校验失败的回退)。 */
int layout_current(void);

/** @brief 加载校验(仿 tools/preview.py check_layout):家不出栅栏 / 两家外墙不重叠 /
 *         门朝场地 / 灌木不压家不堵门 / 粗网格 flood fill 两个门口互相走得到。
 *         失败只 ESP_LOGE、由调用方回退,永不 abort(§2 原则 1)。 */
bool layout_verify(const blueprint_t *bp);

/** @brief 校验并提交第 idx 张图纸的几何到上面那组全局量。校验失败回退图纸 0(A)。
 *  @return 真正生效的图纸下标(调用方据此重画 + 记录轮换起点)。 */
int layout_apply(int idx);

/** @brief 开机自检:把三张图纸全跑一遍校验,结果打一行日志。
 *         🔴 存在的理由:校验器一旦自己写错(2026-08-13 就发生过),表现是"图纸永远
 *         只有一张",在屏幕上和"设计如此"长得一模一样,只有这行日志能当场戳穿。
 *  @return 通过的图纸张数(== layout_count() 才算全好)。 */
int layout_selftest(void);
