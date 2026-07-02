#include "motion_detect.h"

#include <math.h>
#include <string.h>

#define DEFAULT_WAKE_THRESH 0.12f   // Core2+Bottom2 实测:平放噪声 <0.08,拿起 >0.12
#define DEFAULT_WAKE_FRAMES 3

void motion_detect_init(motion_detect_t *md, float wake_thresh, int wake_frames)
{
    memset(md, 0, sizeof(*md));
    md->wake_thresh = (wake_thresh > 0) ? wake_thresh : DEFAULT_WAKE_THRESH;
    md->wake_frames = (wake_frames > 0) ? wake_frames : DEFAULT_WAKE_FRAMES;
}

float motion_detect_feed(motion_detect_t *md, const float accel_g[3])
{
    md->motion = 0;
    if (accel_g) {
        if (md->prev_valid) {
            md->motion = fabsf(accel_g[0] - md->prev[0])
                       + fabsf(accel_g[1] - md->prev[1])
                       + fabsf(accel_g[2] - md->prev[2]);
        }
        memcpy(md->prev, accel_g, sizeof(md->prev));
        md->prev_valid = true;
    }
    return md->motion;
}

int motion_detect_tick_still(motion_detect_t *md)
{
    if (md->motion > md->wake_thresh) md->still_frames = 0;
    else                              md->still_frames++;
    return md->still_frames;
}

bool motion_detect_tick_wake(motion_detect_t *md)
{
    if (md->motion > md->wake_thresh) md->moving_frames++;
    else                              md->moving_frames = 0;
    return md->moving_frames >= md->wake_frames;
}

void motion_detect_reset(motion_detect_t *md)
{
    md->still_frames  = 0;
    md->moving_frames = 0;
}
