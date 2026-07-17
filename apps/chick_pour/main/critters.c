#include "critters.h"
#include "scene.h"
#include "tuning.h"

#include <math.h>
#include "bsp/m5stack_core_2.h"
#include "lvgl.h"

// ── 烘焙身体精灵(SPEC §1:仿 tilt_maze 五角星的 4×4 超采样烘 ARGB)───────────
// 圆身 + 一块偏移的浅色"绒毛高光"两色过渡(同星星的 body→core 混合思路),
// init 时算一次,之后当普通图片贴,§6.4"烘好再贴、不每帧算 alpha"。
// 字节序 B,G,R,A(lv_color32_t,与 tilt_maze render.c 的星星精灵一致)。
#define BODY_IMG_W   20
#define BODY_IMG_H   20

static uint8_t        s_body_px[ANIMAL_KINDS][BODY_IMG_W * BODY_IMG_H * 4];
static lv_image_dsc_t s_body_dsc[ANIMAL_KINDS];

// 点是否在圆内(用于超采样抗锯齿,同 tilt_maze in_star() 的点内测试套路,换成圆判据)
static inline bool in_circle(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static void bake_body_sprite(int kind, uint32_t body_hex, uint32_t sheen_hex)
{
    const float cx = BODY_IMG_W / 2.0f;
    const float cy = BODY_IMG_H / 2.0f + 0.4f;     // 圆身重心略下沉,视觉更"蹲实"
    const float R  = ANIMAL_R + 0.6f;              // 略大于物理半径,饱满一点(仍在 20px 画布内)
    const float sx = cx - 2.2f, sy = cy - 3.0f;    // 高光偏左上,像被光照到的绒毛
    const float sr = R * 0.42f;

    const uint8_t body[3]  = { (uint8_t)(body_hex),  (uint8_t)(body_hex >> 8),  (uint8_t)(body_hex >> 16) };
    const uint8_t sheen[3] = { (uint8_t)(sheen_hex), (uint8_t)(sheen_hex >> 8), (uint8_t)(sheen_hex >> 16) };

    uint8_t *px = s_body_px[kind];
    for (int y = 0; y < BODY_IMG_H; y++) {
        for (int x = 0; x < BODY_IMG_W; x++) {
            int hit = 0, hit_sheen = 0;
            for (int suby = 0; suby < 4; suby++) {
                for (int subx = 0; subx < 4; subx++) {
                    float px_f = x + (subx + 0.5f) / 4;
                    float py_f = y + (suby + 0.5f) / 4;
                    if (in_circle(px_f, py_f, cx, cy, R)) {
                        hit++;
                        if (in_circle(px_f, py_f, sx, sy, sr)) hit_sheen++;
                    }
                }
            }
            uint8_t *o = &px[(y * BODY_IMG_W + x) * 4];
            float t = hit ? (float)hit_sheen / hit : 0;   // 主体色→高光色过渡
            for (int c = 0; c < 3; c++) {
                o[c] = (uint8_t)(body[c] + t * ((float)sheen[c] - body[c]));
            }
            o[3] = (uint8_t)(hit * 255 / 16);
        }
    }

    lv_image_dsc_t *dsc = &s_body_dsc[kind];
    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.w      = BODY_IMG_W;
    dsc->header.h      = BODY_IMG_H;
    dsc->header.stride = BODY_IMG_W * 4;
    dsc->data_size     = BODY_IMG_W * BODY_IMG_H * 4;
    dsc->data          = px;
}

// ── 装扮件尺寸(眼/喙,子对象叠,仿 chain_lab PRIZE_LOOK 的装扮思路)───────────
#define EYE_SZ    2
#define EYE_Y     7
#define EYE_L_X   6
#define EYE_R_X   12
#define EYE_COL   0x3A3A38

#define BEAK_W    5
#define BEAK_H    3
#define BEAK_X    8
#define BEAK_Y    11

static const uint32_t BEAK_COLOR[ANIMAL_KINDS] = { 0xF0A030, 0xF2C14E };   // 鸡喙橙 / 鸭喙偏黄橙

typedef struct {
    lv_obj_t *root;    // 透明容器(整只一起移动/缩放的对象,仿 chain_lab s_pit_doll)
    lv_obj_t *body;    // 烘焙身体图
    lv_obj_t *eye_l;   // 眼睛留句柄:睡着压扁成 2×1(闭眼),醒来恢复 2×2
    lv_obj_t *eye_r;
} critter_view_t;

static critter_view_t s_view[ANIMAL_COUNT];
static float          s_squash[ANIMAL_COUNT];   // 撞击挤压脉冲(0~1),逐帧衰减,仿 tilt_maze s_squash
static int            s_shake[ANIMAL_COUNT];    // 摇头剩余帧数(§5.2),critters_update 逐帧推进
static int            s_hop[ANIMAL_COUNT];      // 小跳剩余帧数(醒来/摇一摇),含错拍等待段
static float          s_breath_ph;              // ATTRACT 慢呼吸相位
static lv_obj_t      *s_zzz;                    // 睡觉 Zzz(文字仅装饰,§2)

// 小跳:HOP_FRAMES 内 translate_y 走半个正弦(离地又落回);>HOP_FRAMES 的部分是错拍等待
#define HOP_FRAMES   16
#define HOP_AMP_PX   8
#define HOP_STAGGER  2      // 每只比前一只晚开跳的帧数(波浪感)

// ── 摇头(进错家)──────────────────────────────────────────────────────
#define SHAKE_FRAMES  24        // ~400ms @60Hz:两个正弦周期 = 左右各摆一下
#define SHAKE_AMP_PX  5

// ── 捕获"蹦进门"(SPEC §5.3)────────────────────────────────────────────
// lv_anim 只演视觉(进度 0..256 → 直线插值 + 抛物弧 + 缩小),游戏侧计数即时累加,
// 完成回调只隐藏自己的 LVGL 对象(LVGL 任务上下文,不碰游戏状态 —— 影子变量纪律)。
#define CAP_HOP_PX      14      // 弧线最高抬升
#define CAP_END_SCALE   112     // 终点缩放(LV_SCALE_NONE=256 → ~44%,"钻进门里变小")

typedef struct {
    lv_obj_t *root;
    float x0, y0;               // 起点(捕获时动物停在门区的位置,中心坐标)
    float x1, y1;               // 终点(家内部中心)
} cap_anim_t;

static cap_anim_t s_cap[ANIMAL_COUNT];

static void cap_exec(void *var, int32_t v)
{
    cap_anim_t *c = (cap_anim_t *)var;
    float t = v / 256.0f;
    float x = c->x0 + (c->x1 - c->x0) * t;
    float y = c->y0 + (c->y1 - c->y0) * t - CAP_HOP_PX * sinf(t * 3.14159f);
    lv_obj_set_pos(c->root, (int)(x - BODY_IMG_W / 2), (int)(y - BODY_IMG_H / 2));

    int scale = LV_SCALE_NONE - (int)((LV_SCALE_NONE - CAP_END_SCALE) * t);
    lv_obj_set_style_transform_scale_x(c->root, scale, 0);
    lv_obj_set_style_transform_scale_y(c->root, scale, 0);
}

static void cap_done(lv_anim_t *a)
{
    cap_anim_t *c = (cap_anim_t *)a->var;
    lv_obj_add_flag(c->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_transform_scale_x(c->root, LV_SCALE_NONE, 0);
    lv_obj_set_style_transform_scale_y(c->root, LV_SCALE_NONE, 0);
}

static lv_obj_t *plain(lv_obj_t *parent, int w, int h, int x, int y, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void critters_init(const animal_t animals[], int n)
{
    if (n > ANIMAL_COUNT) n = ANIMAL_COUNT;

    bake_body_sprite(ANIMAL_CHICK, 0xF7C233, 0xFFF0AE);   // 黄小鸡:暖黄 + 浅黄绒毛高光
    bake_body_sprite(ANIMAL_DUCK,  0xF2F2ED, 0xFFFFFF);   // 白小鸭:米白 + 纯白高光

    bsp_display_lock(0);
    lv_obj_t *scr = lv_screen_active();

    for (int i = 0; i < n; i++) {
        int kind = (int)animals[i].kind;

        // 透明容器承载整只(容器本身零 opa,不产生任何像素填充/混合成本,§6.4)。
        lv_obj_t *root = lv_obj_create(scr);
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, BODY_IMG_W, BODY_IMG_H);
        lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_transform_pivot_x(root, BODY_IMG_W / 2, 0);
        lv_obj_set_style_transform_pivot_y(root, BODY_IMG_H / 2, 0);

        lv_obj_t *body = lv_image_create(root);
        lv_image_set_src(body, &s_body_dsc[kind]);
        lv_obj_set_pos(body, 0, 0);

        s_view[i].eye_l = plain(root, EYE_SZ, EYE_SZ, EYE_L_X, EYE_Y, EYE_COL, LV_RADIUS_CIRCLE);
        s_view[i].eye_r = plain(root, EYE_SZ, EYE_SZ, EYE_R_X, EYE_Y, EYE_COL, LV_RADIUS_CIRCLE);
        plain(root, BEAK_W, BEAK_H, BEAK_X, BEAK_Y, BEAK_COLOR[kind], 1);

        lv_obj_set_pos(root, (int)(animals[i].x - BODY_IMG_W / 2), (int)(animals[i].y - BODY_IMG_H / 2));

        s_view[i].root = root;
        s_view[i].body = body;
        s_squash[i] = 0;
        s_shake[i]  = 0;
        s_hop[i]    = 0;
    }

    // Zzz(ATTRACT 睡觉装饰):群体上方居中,预建 hidden;轻飘由 lv_anim 常驻演,
    // 只在显示时才有脏矩形(hidden 的对象 LVGL 不重绘,不占帧预算)。
    s_zzz = lv_label_create(scr);
    lv_label_set_text(s_zzz, "Z z");
    lv_obj_set_style_text_color(s_zzz, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(s_zzz, LV_OPA_80, 0);
    lv_obj_set_pos(s_zzz, (int)(PLAY_W / 2 + 24), (int)(PLAY_H / 2 - 52));
    lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);

    s_breath_ph = 0;
    bsp_display_unlock();
}

void critters_update(const animal_t animals[], int n)
{
    if (n > ANIMAL_COUNT) n = ANIMAL_COUNT;

    bsp_display_lock(0);
    for (int i = 0; i < n; i++) {
        lv_obj_t *root = s_view[i].root;
        if (!root) continue;
        if (!animals[i].active) continue;   // 已归家:位置/缩放归捕获动画管,这里不抢

        lv_obj_set_pos(root, (int)(animals[i].x - BODY_IMG_W / 2), (int)(animals[i].y - BODY_IMG_H / 2));

        // 挤压脉冲:纵压横展,逐帧指数衰减(仿 tilt_maze render_ball_update 的 s_squash)。
        if (s_squash[i] > 0.001f) {
            float ex = 1.0f + 0.30f * s_squash[i];
            float ey = 1.0f - 0.30f * s_squash[i];
            lv_obj_set_style_transform_scale_x(root, (int)(LV_SCALE_NONE * ex), 0);
            lv_obj_set_style_transform_scale_y(root, (int)(LV_SCALE_NONE * ey), 0);
            s_squash[i] *= 0.82f;
            if (s_squash[i] < 0.02f) {
                s_squash[i] = 0;
                lv_obj_set_style_transform_scale_x(root, LV_SCALE_NONE, 0);
                lv_obj_set_style_transform_scale_y(root, LV_SCALE_NONE, 0);
            }
        }

        // 摇头脉冲(进错家,§5.2):translate_x 走两个正弦周期 = 左右各摆一下;
        // translate 与 set_pos 相互独立,不打架(scene.c 派对用 translate_y 同理)。
        if (s_shake[i] > 0) {
            s_shake[i]--;
            float ph = (float)(SHAKE_FRAMES - s_shake[i]) * (2.0f * 3.14159f * 2.0f / SHAKE_FRAMES);
            lv_obj_set_style_translate_x(root, (int)(-SHAKE_AMP_PX * sinf(ph)), 0);
            if (s_shake[i] == 0) lv_obj_set_style_translate_x(root, 0, 0);
        }

        // 小跳脉冲(醒来/摇一摇):translate_y 半个正弦 = 离地又落回;
        // 超出 HOP_FRAMES 的部分是错拍等待段(按下标晚开跳,波浪感)。
        if (s_hop[i] > 0) {
            s_hop[i]--;
            if (s_hop[i] < HOP_FRAMES) {
                float t = (float)(HOP_FRAMES - s_hop[i]) / HOP_FRAMES;
                lv_obj_set_style_translate_y(root, (int)(-HOP_AMP_PX * sinf(t * 3.14159f)), 0);
                if (s_hop[i] == 0) lv_obj_set_style_translate_y(root, 0, 0);
            }
        }
    }
    bsp_display_unlock();
}

void critters_squash(int idx)
{
    if (idx < 0 || idx >= ANIMAL_COUNT) return;
    s_squash[idx] = 1.0f;   // 下一帧 critters_update 起效并衰减(跨任务只写一个 float,同 tilt_maze 惯例)
}

void critters_shake_head(int idx)
{
    if (idx < 0 || idx >= ANIMAL_COUNT) return;
    s_shake[idx] = SHAKE_FRAMES;   // 同 squash:反馈任务只写计数,推进在 game_task 帧循环里
}

void critters_hop_all(void)
{
    // 只写计数(醒来时由 game_state 调、彩蛋也在 game_task 上下文),update 逐帧推进;
    // inactive 的照设无妨——update 会跳过,不会画。
    for (int i = 0; i < ANIMAL_COUNT; i++) s_hop[i] = HOP_FRAMES + i * HOP_STAGGER;
}

void critters_set_asleep(bool asleep)
{
    bsp_display_lock(0);
    for (int i = 0; i < ANIMAL_COUNT; i++) {
        if (!s_view[i].root) continue;
        // 闭眼 = 眼睛压扁成 2×1 横线(对象还在,零增删);醒来恢复圆点 + 复位呼吸缩放
        lv_obj_set_size(s_view[i].eye_l, EYE_SZ, asleep ? 1 : EYE_SZ);
        lv_obj_set_size(s_view[i].eye_r, EYE_SZ, asleep ? 1 : EYE_SZ);
        if (!asleep) {
            lv_obj_set_style_transform_scale_x(s_view[i].root, LV_SCALE_NONE, 0);
            lv_obj_set_style_transform_scale_y(s_view[i].root, LV_SCALE_NONE, 0);
        }
    }
    if (s_zzz) {
        if (asleep) lv_obj_remove_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

void critters_idle_tick(void)
{
    // 氛围动效走 §6.5 低频档:每 3 帧更新一次(~20fps),10 只 22×22 脏矩形 ≈3k px/帧均摊
    static int div;
    if (++div % 3) return;
    s_breath_ph += 0.15f;   // ~2s 一个呼吸周期

    bsp_display_lock(0);
    for (int i = 0; i < ANIMAL_COUNT; i++) {
        if (!s_view[i].root) continue;
        int sc = LV_SCALE_NONE + (int)(10.0f * sinf(s_breath_ph + i * 0.7f));   // ±4%,每只相位错开
        lv_obj_set_style_transform_scale_x(s_view[i].root, sc, 0);
        lv_obj_set_style_transform_scale_y(s_view[i].root, sc, 0);
    }
    bsp_display_unlock();
}

void critters_capture(int idx, const animal_t *a)
{
    if (idx < 0 || idx >= ANIMAL_COUNT || !s_view[idx].root) return;

    cap_anim_t *c = &s_cap[idx];
    c->root = s_view[idx].root;
    c->x0 = a->x;
    c->y0 = a->y;
    if (a->kind == ANIMAL_CHICK) {
        c->x1 = (HOUSE_RECT.x0 + HOUSE_RECT.x1) / 2;
        c->y1 = (HOUSE_RECT.y0 + HOUSE_RECT.y1) / 2 + 6;   // 屋身偏下(屋顶占上 20px)
    } else {
        c->x1 = (POND_RECT.x0 + POND_RECT.x1) / 2;
        c->y1 = (POND_RECT.y0 + POND_RECT.y1) / 2;
    }

    bsp_display_lock(0);
    // 清掉正在演的脉冲(进错家摇头途中转进对的家等),动画从干净状态起跳
    s_squash[idx] = 0;
    s_shake[idx]  = 0;
    lv_obj_set_style_translate_x(c->root, 0, 0);

    lv_anim_delete(c, NULL);
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, c);
    lv_anim_set_exec_cb(&an, cap_exec);
    lv_anim_set_values(&an, 0, 256);
    lv_anim_set_duration(&an, CAPTURE_ANIM_MS);
    lv_anim_set_completed_cb(&an, cap_done);
    lv_anim_start(&an);
    bsp_display_unlock();
}

void critters_respawn(const animal_t animals[], int n)
{
    if (n > ANIMAL_COUNT) n = ANIMAL_COUNT;

    bsp_display_lock(0);
    for (int i = 0; i < n; i++) {
        lv_obj_t *root = s_view[i].root;
        if (!root) continue;

        lv_anim_delete(&s_cap[i], NULL);   // 残余捕获动画(理论上早结束,防御一手)
        s_squash[i] = 0;
        s_shake[i]  = 0;
        s_hop[i]    = 0;
        lv_obj_set_style_translate_x(root, 0, 0);
        lv_obj_set_style_translate_y(root, 0, 0);
        lv_obj_set_style_transform_scale_x(root, LV_SCALE_NONE, 0);
        lv_obj_set_style_transform_scale_y(root, LV_SCALE_NONE, 0);
        lv_obj_set_pos(root, (int)(animals[i].x - BODY_IMG_W / 2), (int)(animals[i].y - BODY_IMG_H / 2));
        lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}
