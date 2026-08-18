#!/usr/bin/env bash
# 把 render.c 里真正在跑的 bake_home_sprite()/house_sample() 抠出来在主机上烘一遍,
# 和 tools/preview_home.py(=拍板时看的那张图)逐像素比。退出码 0 = 两边一致。
#
# 为什么值得单独跑一遍:精灵缓冲是 **B,G,R,A** 字节序,颜色常量写成 R,G,B 的话屋子会
# 整个变蓝 —— 编译器不会说话,烧上板才看得见,而 WSL 烧不了板,每轮实机都要人插线。
set -euo pipefail
cd "$(dirname "$0")/.."
T="$(mktemp -d)"
# 抠出真身:HM_* 几何常量 → mix3 → house_sample → bake_home_sprite(到 make_box 前收尾)
sed -n '/^#define HM_R_BODY/,/^static lv_obj_t \*make_box/p' main/render.c \
    | sed '$d' > "$T/home_bake.inc"
grep -q 'bake_home_sprite' "$T/home_bake.inc" || { echo "抠函数失败:render.c 结构变了"; exit 1; }
cat > "$T/main.c" <<'C'
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { struct { int magic, cf, w, h, stride; } header;
                 int data_size; uint8_t *data; } lv_image_dsc_t;
#define LV_IMAGE_HEADER_MAGIC 0
#define LV_COLOR_FORMAT_ARGB8888 0
#define HM_IMG 32
static uint8_t s_home_img[HM_IMG * HM_IMG * 4], s_home_img_lit[HM_IMG * HM_IMG * 4];
static lv_image_dsc_t s_home_dsc, s_home_dsc_lit;
static void img_desc(lv_image_dsc_t *d, uint8_t *b, int w, int h)
{ d->header.w = w; d->header.h = h; d->data = b; d->data_size = w * h * 4; }
#include "home_bake.inc"
int main(void) {   /* 常态 + 亮灯态各烘一张,连着吐出去 */
    bake_home_sprite(s_home_img, &s_home_dsc, 0);
    bake_home_sprite(s_home_img_lit, &s_home_dsc_lit, 1);
    return fwrite(s_home_img, 1, sizeof s_home_img, stdout) != sizeof s_home_img
        || fwrite(s_home_img_lit, 1, sizeof s_home_img_lit, stdout) != sizeof s_home_img_lit; }
C
gcc -std=gnu11 -Wall -Wextra -I "$T" -o "$T/bake" "$T/main.c" -lm
"$T/bake" > "$T/c.raw"
python3 - "$T/c.raw" <<'PY'
import sys
sys.path.insert(0, "tools")
import preview_home as ph
raw = open(sys.argv[1], "rb").read()
plane = ph.IMG * ph.IMG * 4
fail = 0
for i, (tag, lit) in enumerate((("常态", False), ("亮灯态", True))):
    ref = ph.bake(lambda x, y: ph.sample_house(x, y, lit)).load()
    off = i * plane
    bad, worst = 0, 0
    for y in range(ph.IMG):
        for x in range(ph.IMG):
            k = off + (y * ph.IMG + x) * 4
            b, g, r, a = raw[k:k + 4]                     # C 侧是 B,G,R,A
            pr, pg, pb, pa = ref[x, y]
            if pa == 0 and a == 0:
                continue
            d = max(abs(r - pr), abs(g - pg), abs(b - pb), abs(a - pa))
            worst = max(worst, d)
            if d > 1:
                bad += 1
    print("%s:最大逐通道差 %d,超差(>1)像素 %d/%d" % (tag, worst, bad, ph.IMG * ph.IMG))
    fail |= bad
sys.exit(1 if fail else 0)
PY
echo "家精灵自检:C 版与效果图一致 ✅"
