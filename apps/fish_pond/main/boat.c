// boat —— 船/线/饵运动实现(SPEC.md §6.1)
#include "boat.h"

#include <math.h>
#include <stdint.h>

#include "bsp/m5stack_core_2.h"

#include "feedback.h"
#include "sprites.h"
#include "tuning.h"

#define BOAT_TOP_Y  (WATERLINE_Y - BOAT_H + 8)

static fpond_boat_sprite_t s_boat;
static fpond_bait_sprite_t s_bait;
static lv_obj_t *s_line;

static float s_boat_cx;
static float s_line_len;
static float s_reel_ratio = 1.0f;
static float s_bait_x, s_bait_y;   // 缓动后的实际渲染坐标
static bool  s_line_was_at_min = true;

// 静止时跳过重绘(SPEC §6.2 硬规矩:没在动的像素不重绘);船/线/饵一旦停稳,取整坐标
// 会连续多帧不变,这里据此省掉冗余 lv_obj_set_pos/set_size 调用。
static int  s_last_boat_x = INT32_MIN, s_last_bait_x, s_last_bait_y, s_last_line_h;
static bool s_last_paddle;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 死区 + 线性重缩放:死区外仍是满行程、无跳变(同 crane_game 摇杆吊臂手法)
static float apply_deadzone(float n)
{
    float a = fabsf(n);
    if (a < BOAT_JOY_DEADZONE) return 0.f;
    float scaled = (a - BOAT_JOY_DEADZONE) / (1.0f - BOAT_JOY_DEADZONE);
    return (n < 0) ? -scaled : scaled;
}

void boat_create(lv_obj_t *scr)
{
    s_boat_cx = (BOAT_MIN_X + BOAT_MAX_X) / 2.0f;
    s_line_len = LINE_MIN_PX;
    s_bait_x = s_boat_cx;
    s_bait_y = WATERLINE_Y + s_line_len;

    bsp_display_lock(0);
    fpond_boat_sprite_create(&s_boat, scr);
    s_line = fpond_box(scr, 2, 1, COL_LINE, 0);
    fpond_bait_sprite_create(&s_bait, scr);

    fpond_boat_sprite_set_pos(&s_boat, (int)s_boat_cx, BOAT_TOP_Y, false);
    fpond_bait_sprite_set_pos(&s_bait, (int)s_bait_x, (int)s_bait_y);
    lv_obj_set_pos(s_line, (int)s_boat_cx - 1, BOAT_TOP_Y + BOAT_H);
    lv_obj_set_size(s_line, 2, 1);
    bsp_display_unlock();
}

void boat_set_reel_ratio(float ratio) { s_reel_ratio = ratio; }

void boat_tick(int dt_ms, float joy_x_raw, int enc_delta)
{
    float nx = apply_deadzone(joy_x_raw);
    float vx = BOAT_SPEED_MAX * nx;
    s_boat_cx = clampf(s_boat_cx + vx * dt_ms / 1000.0f, BOAT_MIN_X, BOAT_MAX_X);   // 撞边缘滑停不粘

    if (enc_delta != 0) {
        s_line_len = clampf(s_line_len + enc_delta * CRANK_PX_PER_DET * s_reel_ratio,
                             LINE_MIN_PX, LINE_MAX_PX);
    }

    // 从"收到头"开始重新放线 = 一次新的抛饵:波纹+噗通(SPEC §7"饵入水")
    bool at_min = s_line_len <= LINE_MIN_PX + 1.0f;
    if (s_line_was_at_min && !at_min) feedback_bait_splash();
    s_line_was_at_min = at_min;

    // 饵目标位 = (船 x, 水线 + 线长);双轴缓动(宁慢勿飘)
    float target_x = s_boat_cx;
    float target_y = WATERLINE_Y + s_line_len;
    float alpha = clampf((float)dt_ms / BAIT_EASE_TAU_MS, 0.f, 1.f);
    s_bait_x += (target_x - s_bait_x) * alpha;
    s_bait_y += (target_y - s_bait_y) * alpha;

    int boat_x = (int)s_boat_cx, bait_x = (int)s_bait_x, bait_y = (int)s_bait_y;
    int line_top = BOAT_TOP_Y + BOAT_H;
    int line_h   = bait_y - line_top; if (line_h < 1) line_h = 1;
    bool paddle  = fabsf(vx) > 1.0f;

    if (boat_x == s_last_boat_x && bait_x == s_last_bait_x && bait_y == s_last_bait_y
        && line_h == s_last_line_h && paddle == s_last_paddle) {
        return;   // 取整坐标与上一帧完全相同:船/线/饵都已停稳,跳过冗余重绘
    }
    s_last_boat_x = boat_x; s_last_bait_x = bait_x; s_last_bait_y = bait_y;
    s_last_line_h = line_h; s_last_paddle = paddle;

    bsp_display_lock(0);
    fpond_boat_sprite_set_pos(&s_boat, boat_x, BOAT_TOP_Y, paddle);
    lv_obj_set_pos(s_line, boat_x - 1, line_top);
    lv_obj_set_size(s_line, 2, line_h);
    fpond_bait_sprite_set_pos(&s_bait, bait_x, bait_y);
    bsp_display_unlock();
}

int boat_bait_x(void) { return (int)s_bait_x; }
int boat_bait_y(void) { return (int)s_bait_y; }
int boat_line_len(void) { return (int)s_line_len; }
