// fish —— 鱼 AI 状态机 + 收线实现(SPEC.md §6.2/§6.3)
//
// PATROL(巡游)→ CHASE(转向饵靠近;瞪眼/两级张嘴由距离连续算出,不单独建状态)
//   → CHOMP(咬合停留)→ HOOKED(收线拉扯,鱼锚定在饵上)→ 出水进桶,原地立即重生一条新鱼。
// 饵离开感知圈 → GIGGLE(咯咯泡泡,~0.8s)→ 回 PATROL,零失败(SPEC §2)。
// 只有一条鱼可同时上钩:s_hooked_idx 门控其余鱼的 CHOMP 转换。
#include "fish.h"

#include <math.h>

#include "esp_random.h"

#include "bsp/m5stack_core_2.h"

#include "boat.h"
#include "bucket.h"
#include "feedback.h"
#include "sprites.h"
#include "tuning.h"

#define SCREEN_W 320

typedef enum {
    AI_PATROL = 0,
    AI_CHASE,     // 瞪眼/张嘴造型由 vis_cache 每帧按距离连续算出
    AI_CHOMP,
    AI_HOOKED,
    AI_GIGGLE,
} fish_ai_t;

typedef struct {
    fish_species_t species;
    fpond_fish_sprite_t sprite;
    int   w, h;
    float speed;
    int   y_center;
    int   x_min, x_max;
    float x;
    int   dir;               // +1(右)/-1(左),数值与 fish_face_t 对齐,可直接强转
    fish_ai_t ai;
    int   ai_ms;
    fish_vis_t vis_cache;     // AI_CHASE 每帧算好的视觉子状态(NOTICE/APPROACH_SMALL/BIG)
    bool  swim_b;
    int   swim_ms;
    int   render_accum_ms;    // 巡游态渲染节流累加(SPEC §6.5:两鱼 20fps 错帧,常态帧预算达标)
    int   giggle_cooldown_ms;
    int   reel_idle_ms;       // HOOKED 内:距上次曲柄增量多久(打盹判据)
    bool  dozing;
    lv_obj_t *bubble;         // 装饰泡:GIGGLE 咯咯泡 / HOOKED 打盹 zzz 复用同一个对象
} fish_t;

static fish_t s_fish[FISH_SPECIES_COUNT];
static int    s_hooked_idx = -1;      // -1 = 当前没有鱼上钩
static int    s_hooked_prev_line;     // HOOKED 期间上一帧线长(推断曲柄是否有动作)

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void respawn_one(fish_t *f);
static void render_common(fish_t *f);

static void fish_init_one(fish_t *f, fish_species_t species, int w, int h, float speed,
                          int y_center, uint32_t body_col, uint32_t belly_col, lv_obj_t *scr)
{
    f->species = species;
    f->w = w; f->h = h; f->speed = speed; f->y_center = y_center;
    f->x_min = FISH_X_MARGIN + w / 2;
    f->x_max = SCREEN_W - FISH_X_MARGIN - w / 2;
    // 两鱼 20fps 渲染错帧(SPEC §6.5):初始累加相位错开半周期,避免同帧齐动峰值超预算
    f->render_accum_ms = (species == FISH_SPECIES_FAT) ? 0 : (FISH_RENDER_MS / 2);
    fpond_fish_sprite_create(&f->sprite, scr, w, h, body_col, belly_col);
    f->bubble = fpond_dot_create(scr, 10, COL_BUBBLE);
}

void fish_create(lv_obj_t *scr)
{
    bsp_display_lock(0);
    fish_init_one(&s_fish[FISH_SPECIES_FAT], FISH_SPECIES_FAT,
                  FISH_FAT_W, FISH_FAT_H, FISH_FAT_SPEED, FISH_FAT_Y,
                  COL_FAT_BODY, COL_FAT_BELLY, scr);
    fish_init_one(&s_fish[FISH_SPECIES_LAZY], FISH_SPECIES_LAZY,
                  FISH_LAZY_W, FISH_LAZY_H, FISH_LAZY_SPEED, FISH_LAZY_Y,
                  COL_LAZY_BODY, COL_LAZY_BELLY, scr);
    bsp_display_unlock();
    fish_round_setup();
}

static void reset_fish_state(fish_t *f)
{
    f->x = (float)(f->x_min + (int)(esp_random() % (uint32_t)(f->x_max - f->x_min + 1)));
    f->dir = (esp_random() & 1) ? 1 : -1;
    f->ai = AI_PATROL;
    f->ai_ms = 0;
    f->vis_cache = FISH_VIS_PATROL_A;
    f->swim_b = false;
    f->swim_ms = 0;
    f->giggle_cooldown_ms = 0;
    f->reel_idle_ms = 0;
    f->dozing = false;
}

void fish_round_setup(void)
{
    s_hooked_idx = -1;
    for (int i = 0; i < FISH_SPECIES_COUNT; i++) {
        fish_t *f = &s_fish[i];
        reset_fish_state(f);
        bsp_display_lock(0);
        fpond_fish_sprite_show(&f->sprite, true);
        lv_obj_add_flag(f->bubble, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
        render_common(f);
    }
}

static void respawn_one(fish_t *f)
{
    reset_fish_state(f);
    bsp_display_lock(0);
    lv_obj_add_flag(f->bubble, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
    render_common(f);
}

static void render_common(fish_t *f)
{
    fish_vis_t vis;
    switch (f->ai) {
    case AI_PATROL: vis = f->swim_b ? FISH_VIS_PATROL_B : FISH_VIS_PATROL_A; break;
    case AI_CHASE:  vis = f->vis_cache; break;
    case AI_CHOMP:  vis = FISH_VIS_CHOMP; break;
    case AI_GIGGLE: vis = FISH_VIS_PATROL_A; break;
    default:        vis = FISH_VIS_PATROL_A; break;
    }
    bsp_display_lock(0);
    fpond_fish_sprite_set_state(&f->sprite, vis, (fish_face_t)f->dir);
    fpond_fish_sprite_set_pos(&f->sprite, (int)f->x, f->y_center);
    bsp_display_unlock();
}

// ── HOOKED:鱼锚定在饵上,曲柄收线,拉扯戏全靠声+震(SPEC §6.3)────────────────
static void tick_hooked(fish_t *f, int dt_ms, int bx, int by)
{
    int line = boat_line_len();

    if (line <= LINE_MIN_PX) {   // 饵升到水线 = 出水,交给木桶接管入桶动画
        feedback_surface();
        bucket_catch_start(f->species, bx, by);
        s_hooked_idx = -1;
        boat_set_reel_ratio(1.0f);
        respawn_one(f);          // 立即在本层重生一条新鱼(SPEC §5"进桶-未满→PLAY")
        return;
    }

    int dline = line - s_hooked_prev_line;
    s_hooked_prev_line = line;

    bool was_dozing = f->dozing;
    if (dline != 0) {
        f->reel_idle_ms = 0;
        f->dozing = false;
        feedback_reel_tick();
    } else {
        f->reel_idle_ms += dt_ms;
        if (!f->dozing && f->reel_idle_ms >= REEL_DOZE_MS) {
            f->dozing = true;
            feedback_doze();
        }
    }

    int wig = 0;
    if (!f->dozing) {
        f->ai_ms += dt_ms;   // 复用 ai_ms 当摆动相位计时(HOOKED 期间不再用于别的用途)
        wig = (int)(WIGGLE_AMP_PX * sinf(2.0f * (float)M_PI * WIGGLE_HZ * f->ai_ms / 1000.0f));
    }

    bsp_display_lock(0);
    fpond_fish_sprite_set_state(&f->sprite, FISH_VIS_CHOMP, (fish_face_t)f->dir);
    fpond_fish_sprite_set_pos(&f->sprite, bx + wig, by);
    if (f->dozing) {
        lv_obj_set_pos(f->bubble, bx + f->w / 2 - 5, by - f->h / 2 - 12);
        lv_obj_remove_flag(f->bubble, LV_OBJ_FLAG_HIDDEN);
    } else if (was_dozing) {
        lv_obj_add_flag(f->bubble, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

static void tick_one(fish_t *f, int idx, int dt_ms)
{
    if (f->ai == AI_HOOKED) {
        tick_hooked(f, dt_ms, boat_bait_x(), boat_bait_y());
        return;
    }

    int bx = boat_bait_x(), by = boat_bait_y();

    f->swim_ms += dt_ms;
    if (f->swim_ms >= FISH_SWIM_FRAME_MS) { f->swim_ms = 0; f->swim_b = !f->swim_b; }
    if (f->giggle_cooldown_ms > 0) f->giggle_cooldown_ms -= dt_ms;

    switch (f->ai) {
    case AI_PATROL: {
        f->x += f->dir * f->speed * dt_ms / 1000.0f;
        if (f->x <= f->x_min) { f->x = (float)f->x_min; f->dir = 1;  }
        if (f->x >= f->x_max) { f->x = (float)f->x_max; f->dir = -1; }
        float ddx = bx - f->x, ddy = by - f->y_center;
        if (sqrtf(ddx * ddx + ddy * ddy) < SENSE_R) {
            f->ai = AI_CHASE; f->ai_ms = 0;
            feedback_fish_notice();
        }
        break;
    }
    case AI_CHASE: {
        float dx = bx - f->x;
        f->dir = (dx >= 0) ? 1 : -1;
        float step = f->speed * APPROACH_MULT * dt_ms / 1000.0f;
        if (fabsf(dx) > step) f->x += (dx > 0 ? 1.0f : -1.0f) * step;
        else                  f->x = (float)bx;
        f->x = clampf(f->x, (float)f->x_min, (float)f->x_max);

        dx = bx - f->x;
        float dy = by - f->y_center;
        float dist = sqrtf(dx * dx + dy * dy);

        if (dist >= SENSE_R) {   // 饵离开感知圈:咯咯(节流)回巡游,零失败
            f->ai = AI_GIGGLE; f->ai_ms = 0;
            if (f->giggle_cooldown_ms <= 0) {
                feedback_giggle();
                f->giggle_cooldown_ms = GIGGLE_COOLDOWN_MS;
            }
            bsp_display_lock(0);
            lv_obj_set_style_bg_color(f->bubble, lv_color_hex(COL_BUBBLE), 0);
            lv_obj_set_pos(f->bubble, (int)f->x - 5, f->y_center - f->h / 2 - 12);
            lv_obj_remove_flag(f->bubble, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();
            break;
        }

        float mouth_x = f->x + f->dir * (f->w * 0.4f);
        float mdx = mouth_x - bx, mdy = (float)f->y_center - by;
        float mouth_dist = sqrtf(mdx * mdx + mdy * mdy);

        if (mouth_dist <= BITE_R && s_hooked_idx < 0 && !bucket_is_busy()) {
            f->ai = AI_CHOMP; f->ai_ms = 0;
            feedback_bite();
            break;
        }

        if (dist < MOUTH_R) {
            float mid = BITE_R + (MOUTH_R - BITE_R) * 0.5f;
            f->vis_cache = (dist < mid) ? FISH_VIS_APPROACH_BIG : FISH_VIS_APPROACH_SMALL;
        } else {
            f->vis_cache = FISH_VIS_NOTICE;
        }
        break;
    }
    case AI_CHOMP:
        f->ai_ms += dt_ms;
        if (f->ai_ms >= CHOMP_HOLD_MS) {
            s_hooked_idx = idx;
            f->ai = AI_HOOKED; f->ai_ms = 0;
            f->reel_idle_ms = 0; f->dozing = false;
            boat_set_reel_ratio(f->species == FISH_SPECIES_LAZY ? REEL_LAZY_RATIO : 1.0f);
            s_hooked_prev_line = boat_line_len();
        }
        break;
    case AI_GIGGLE:
        f->ai_ms += dt_ms;
        f->x += f->dir * f->speed * 0.5f * dt_ms / 1000.0f;   // 咯咯期间仍慢慢漂,不生硬定住
        f->x = clampf(f->x, (float)f->x_min, (float)f->x_max);
        if (f->ai_ms >= GIGGLE_COOLDOWN_MS) {
            f->ai = AI_PATROL; f->ai_ms = 0;
            bsp_display_lock(0);
            lv_obj_add_flag(f->bubble, LV_OBJ_FLAG_HIDDEN);
            bsp_display_unlock();
        }
        break;
    default: break;
    }

    // 巡游态渲染节流到 20fps、两鱼错帧(SPEC §6.5 帧预算);其余态(转向/张嘴/咬合/咯咯)
    // 是短促的交互高光时刻,每帧渲染保跟手,不受此节流影响。
    if (f->ai == AI_PATROL) {
        f->render_accum_ms += dt_ms;
        if (f->render_accum_ms < FISH_RENDER_MS) return;
        f->render_accum_ms -= FISH_RENDER_MS;
    }
    render_common(f);
}

void fish_tick(int dt_ms)
{
    for (int i = 0; i < FISH_SPECIES_COUNT; i++) tick_one(&s_fish[i], i, dt_ms);
}
