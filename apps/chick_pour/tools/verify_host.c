// 主机侧图纸校验回归测试:直接链 main/layout.c(真身,非翻译版),逐张跑
// layout_verify() 并断言全过。WSL 烧不了板,布局/校验类改动一律先过这关再谈烧录。
//
//   apps/chick_pour/tools/verify_host.sh     # 编译 + 运行,退出码 0 = 全过
//
// 与 tools/preview.py 的分工:preview.py 出图看**好不好看/挤不挤**(几何真值 + 效果图),
// 本测试验**校验器自己对不对**(它一旦误判,游戏会静默退化成"永远只有一张图纸")。
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "layout.h"
#include "flock.h"

// 布点合法性:独立复算一遍(不复用 flock.c 自己的 scatter_validate —— 校验器和被校验者
// 是同一个人写的就等于没校验,这次的 bug 正是这么漏过去的)。
static int scatter_illegal(const animal_t a[], int n, const char *bp_name, int seed)
{
    int bad = 0;
    const rect_t *gates[2] = { &HOUSE_GATE, &POND_GATE };
    const rect_t *homes[2] = { &HOUSE_RECT, &POND_RECT };
    for (int i = 0; i < n; i++) {
        float x = a[i].x, y = a[i].y;
        if (x < PLAY_BOUNDS.x0 || x > PLAY_BOUNDS.x1 || y < PLAY_BOUNDS.y0 || y > PLAY_BOUNDS.y1) {
            printf("🔴 [%s seed=%d] 第%d只出栅栏 (%.0f,%.0f)\n", bp_name, seed, i, x, y); bad++;
        }
        for (int h = 0; h < 2; h++) {
            if (x >= homes[h]->x0 && x <= homes[h]->x1 && y >= homes[h]->y0 && y <= homes[h]->y1) {
                printf("🔴 [%s seed=%d] 第%d只生在家%d的墙里 (%.0f,%.0f)\n", bp_name, seed, i, h, x, y); bad++;
            }
            if (x >= gates[h]->x0 && x <= gates[h]->x1 && y >= gates[h]->y0 && y <= gates[h]->y1) {
                printf("🔴 [%s seed=%d] 第%d只生在家%d的门判定区里 (%.0f,%.0f)\n", bp_name, seed, i, h, x, y); bad++;
            }
        }
        for (int b = 0; b < 4; b++) {
            float dx = x - CORNER_BUSH[b].x, dy = y - CORNER_BUSH[b].y;
            if (sqrtf(dx * dx + dy * dy) < CORNER_BUSH[b].r) {
                printf("🔴 [%s seed=%d] 第%d只生在灌木%d里 (%.0f,%.0f)\n", bp_name, seed, i, b, x, y); bad++;
            }
        }
        for (int j = i + 1; j < n; j++) {
            float dx = x - a[j].x, dy = y - a[j].y;
            if (sqrtf(dx * dx + dy * dy) < 2 * ANIMAL_R) {
                printf("🔴 [%s seed=%d] 第%d/%d只叠在一起\n", bp_name, seed, i, j); bad++;
            }
        }
    }
    return bad;
}

// 三张图纸 × 多个随机种子跑重散,布点必须永远合法(§5.4"永不失败":约束随机不行就
// 走网格兜底,但兜底摆出来的点也得合法 —— 图纸 B/C 是 2026-08-13 才真正跑起来的路径)。
static int check_scatter(void)
{
    static animal_t animals[ANIMAL_COUNT];
    int bad = 0;
    for (int i = 0; i < layout_count(); i++) {
        const blueprint_t *bp = layout_get(layout_apply(i));
        for (int seed = 1; seed <= 40; seed++) {
            srand(seed);
            flock_init(animals, ANIMAL_COUNT);
            flock_scatter(animals, ANIMAL_COUNT);
            bad += scatter_illegal(animals, ANIMAL_COUNT, bp->name, seed);
        }
    }
    printf("重散布点:3 张图纸 × 40 个随机种子 → %s\n",
           bad ? "**有非法布点(见上)**" : "全部合法(不出栅栏/不进墙/不进门区/不压灌木/不重叠)");
    return bad;
}

int main(void)
{
    int n = layout_count(), pass = 0;
    printf("== chick_pour 图纸校验(主机跑 main/layout.c 真身)==\n");
    for (int i = 0; i < n; i++) {
        const blueprint_t *bp = layout_get(i);
        bool ok = layout_verify(bp);
        printf("图纸 #%d [%s] → %s\n", i, bp->name, ok ? "PASS" : "**FAIL**");
        if (ok) pass++;
    }

    // 提交路径也验一遍:layout_apply 必须真的切到目标图纸,而不是悄悄回退到 A。
    for (int i = 0; i < n; i++) {
        int got = layout_apply(i);
        if (got != i) {
            printf("🔴 layout_apply(%d) 实际生效的是 #%d —— 校验失败被回退了\n", i, got);
            pass = -1;
        }
    }

    if (pass != n) {
        printf("\n🔴 %d/%d 通过。校验器判失败的图纸在实机上会静默回退图纸 A,\n"
               "   屏幕表现 = \"玩多久都只有一张后院\",与\"设计如此\"分辨不出来。\n", pass, n);
        return 1;
    }
    if (check_scatter() != 0) return 1;

    printf("\n✅ 图纸校验 %d/%d + 重散布点 全过。\n", pass, n);
    return 0;
}
