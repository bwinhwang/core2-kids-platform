// 躲猫猫昼夜屋 v2 —— 相册层实现(见 album.h)
#include "album.h"

#include "esp_log.h"
#include "nvs.h"

#include "bsp/m5stack_core_2.h"

#include "tuning.h"
#include "visitor.h"

static const char *TAG = "peekaboo_album";

#define SLOT_W   18
#define SLOT_H   18
#define SLOT_GAP 4
#define SLOT_X0  8
#define SLOT_Y   6
#define STAR_SZ  14

#define UNCOLLECTED_BASE 0xCFC6B8
#define UNCOLLECTED_FEAT 0xB8B0A2

static uint8_t s_mask;
static int     s_count;
static int     s_last_marked_id = -1;
static int     s_prev_round_last_id = -1;
static uint16_t s_parade_count;

static lv_obj_t *s_slot_base[VISITOR_POOL_N];
static lv_obj_t *s_slot_feat[VISITOR_POOL_N];
static lv_obj_t *s_stars[PARADE_STARS_MAX];

static nvs_handle_t s_nvs;
static bool         s_nvs_ok;

// ── NVS(P4)──────────────────────────────────────────────────────────
static void nvs_load(void)
{
    s_nvs_ok = (nvs_open(PEEKABOO_NVS_NS, NVS_READWRITE, &s_nvs) == ESP_OK);
    if (!s_nvs_ok) {
        ESP_LOGW(TAG, "NVS 打开失败,相册进度本次不持久化");
        return;
    }
    uint8_t mask = 0;
    uint16_t pc = 0;
    nvs_get_u8(s_nvs, "album_mask", &mask);     // NOT_FOUND 时保持 0(首次开机)
    nvs_get_u16(s_nvs, "parade_count", &pc);
    s_mask = mask;
    s_parade_count = pc;
    for (int i = 0; i < VISITOR_POOL_N; i++) if (mask & (1u << i)) s_count++;
}

static void nvs_save_mask(void)
{
    if (!s_nvs_ok) return;
    nvs_set_u8(s_nvs, "album_mask", s_mask);
    nvs_commit(s_nvs);
}

static void nvs_save_parade(void)
{
    if (!s_nvs_ok) return;
    nvs_set_u16(s_nvs, "parade_count", s_parade_count);
    nvs_commit(s_nvs);
}

// ── UI ───────────────────────────────────────────────────────────────
static lv_obj_t *plain(lv_obj_t *parent, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// 每位一个小特征块(粗略呼应 visitor.c 的造型要点,18x18 小图标够用)
static void slot_feature_geom(int id, int *w, int *h, int *dx, int *dy, int *radius)
{
    switch (id) {
    case 0: *w = 5; *h = 5; *dx = -5; *dy = -6; *radius = 2; break;   // CAT 耳
    case 1: *w = 6; *h = 6; *dx = 0;  *dy = -1; *radius = LV_RADIUS_CIRCLE; break; // OWL 眼圈
    case 2: *w = 5; *h = 5; *dx = -4; *dy = -5; *radius = LV_RADIUS_CIRCLE; break; // FROG 泡眼
    case 3: *w = 4; *h = 9; *dx = -5; *dy = -8; *radius = 2; break;   // BUNNY 耳
    case 4: *w = 6; *h = 4; *dx = 4;  *dy = 2;  *radius = 2; break;   // DUCK 喙
    default: *w = 4; *h = 4; *dx = 3; *dy = -5; *radius = 1; break;   // HEDGE 刺
    }
}

static void slot_apply(int id)
{
    bool got = s_mask & (1u << id);
    uint32_t base = got ? visitor_color(id) : UNCOLLECTED_BASE;
    uint32_t feat = got ? 0xFFFFFF : UNCOLLECTED_FEAT;
    lv_obj_set_style_bg_color(s_slot_base[id], lv_color_hex(base), 0);
    lv_obj_set_style_bg_color(s_slot_feat[id], lv_color_hex(feat), 0);
}

static void stars_apply(void)
{
    int n = s_parade_count > PARADE_STARS_MAX ? PARADE_STARS_MAX : (int)s_parade_count;
    for (int i = 0; i < PARADE_STARS_MAX; i++) {
        if (i < n) lv_obj_remove_flag(s_stars[i], LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag(s_stars[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void album_create(lv_obj_t *scr)
{
    nvs_load();

    for (int i = 0; i < VISITOR_POOL_N; i++) {
        int x = SLOT_X0 + i * (SLOT_W + SLOT_GAP);
        lv_obj_t *base = plain(scr, SLOT_W, SLOT_H, UNCOLLECTED_BASE, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(base, x, SLOT_Y);
        s_slot_base[i] = base;

        int w, h, dx, dy, radius;
        slot_feature_geom(i, &w, &h, &dx, &dy, &radius);
        lv_obj_t *feat = plain(base, w, h, UNCOLLECTED_FEAT, radius);
        lv_obj_align(feat, LV_ALIGN_CENTER, dx, dy);
        s_slot_feat[i] = feat;

        slot_apply(i);
    }

    int star_x0 = SLOT_X0 + VISITOR_POOL_N * (SLOT_W + SLOT_GAP) + 6;
    for (int i = 0; i < PARADE_STARS_MAX; i++) {
        lv_obj_t *s = plain(scr, STAR_SZ, STAR_SZ, 0xFFE89B, LV_RADIUS_CIRCLE);
        lv_obj_set_pos(s, star_x0 + i * (STAR_SZ + 2), SLOT_Y + 2);
        lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
        s_stars[i] = s;
    }
    stars_apply();
}

bool album_is_collected(int id)
{
    if (id < 0 || id >= VISITOR_POOL_N) return false;
    return s_mask & (1u << id);
}

bool album_mark_collected(int id)
{
    if (id < 0 || id >= VISITOR_POOL_N) return false;
    s_last_marked_id = id;
    if (s_mask & (1u << id)) return false;

    s_mask |= (1u << id);
    s_count++;
    bsp_display_lock(0);
    slot_apply(id);
    bsp_display_unlock();
    nvs_save_mask();
    return true;
}

int album_count(void) { return s_count; }

int album_last_full_round_id(void) { return s_prev_round_last_id; }

void album_reset(void)
{
    s_prev_round_last_id = s_last_marked_id;
    s_mask = 0;
    s_count = 0;
    s_last_marked_id = -1;
    s_parade_count++;

    bsp_display_lock(0);
    for (int i = 0; i < VISITOR_POOL_N; i++) slot_apply(i);
    stars_apply();
    bsp_display_unlock();

    nvs_save_mask();
    nvs_save_parade();
}

int album_parade_count(void) { return s_parade_count; }

uint8_t album_mask(void) { return s_mask; }
