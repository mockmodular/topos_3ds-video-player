#if !defined(DEF_VID_SEEKBAR_H)
#define DEF_VID_SEEKBAR_H

#include <stdbool.h>
#include <stdint.h>

#include "vid_panel_layout.h"

/* Seek bar geometry derived from vid_panel_layout.h (single source of truth).
 * X/W/H/Y are kept as float aliases for backwards compatibility with
 * VidSeekBar_touch_x_to_pos() which uses them as float arithmetic. */
#define VID_SEEKBAR_X    ((double)VP_PROGRESS_X_MIN)
#define VID_SEEKBAR_Y    ((double)VP_PROGRESS_Y)
#define VID_SEEKBAR_W    ((double)VP_PROGRESS_TOTAL_W)
#define VID_SEEKBAR_H    ((double)VP_PROGRESS_HIT_H)

/* Returns true if the touch point falls within the seek bar + slop area. */
static inline bool VidSeekBar_hit_test(int16_t touch_x, int16_t touch_y)
{
	return vp_progress_hit((int)touch_x, (int)touch_y);
}

/* Converts a raw touch pixel X to a media position in milliseconds.
 * Result is clamped to [0, duration_ms]. */
static inline double VidSeekBar_touch_x_to_pos(int16_t touch_x, double duration_ms)
{
	double ratio = ((double)touch_x - VID_SEEKBAR_X) / VID_SEEKBAR_W;
	if(ratio < 0.0) ratio = 0.0;
	if(ratio > 1.0) ratio = 1.0;
	return duration_ms * ratio;
}

#endif //!defined(DEF_VID_SEEKBAR_H)
