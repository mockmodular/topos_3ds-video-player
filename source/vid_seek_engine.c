//Includes.
#include "video_player.h"
#include "vid_state.h"
#include "vid_seekbar.h"
#include "vid_seek_engine.h"

#include "system/util/err.h"
#include "system/util/log.h"
#include "system/util/util.h"

//Code.
void VidSeekEngine_on_bar_drag(int16_t touch_x)
{
	vid_player.seek_pos_cache = VidSeekBar_touch_x_to_pos(touch_x, vid_player.media_duration);
}

void VidSeekEngine_on_bar_commit(void)
{
	uint32_t result = DEF_ERR_OTHER;
	vid_player.seek_pos = vid_player.seek_pos_cache;
	DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue,
		DECODE_THREAD_SEEK_REQUEST, NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE),
		(result == DEF_SUCCESS), result);
}

void VidSeekEngine_on_step_fwd(void)
{
	uint32_t result = DEF_ERR_OTHER;
	double current_pos = (vid_player.state == PLAYER_STATE_SEEKING || vid_player.state == PLAYER_STATE_PREPARE_SEEKING)
		? vid_player.seek_pos : vid_player.media_current_pos;

	if((current_pos + DEF_UTIL_S_TO_MS_D(vid_player.seek_duration)) > vid_player.media_duration)
		vid_player.seek_pos = vid_player.media_duration;
	else
		vid_player.seek_pos = current_pos + DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);

	DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue,
		DECODE_THREAD_SEEK_REQUEST, NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE),
		(result == DEF_SUCCESS), result);
}

void VidSeekEngine_on_step_back(void)
{
	uint32_t result = DEF_ERR_OTHER;
	double current_pos = (vid_player.state == PLAYER_STATE_SEEKING || vid_player.state == PLAYER_STATE_PREPARE_SEEKING)
		? vid_player.seek_pos : vid_player.media_current_pos;

	if((current_pos - DEF_UTIL_S_TO_MS_D(vid_player.seek_duration)) < 0)
		vid_player.seek_pos = 0;
	else
		vid_player.seek_pos = current_pos - DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);

	DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue,
		DECODE_THREAD_SEEK_REQUEST, NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE),
		(result == DEF_SUCCESS), result);
}

VidSeekEngineView VidSeekEngine_get_view(void)
{
	VidSeekEngineView v;
	bool is_seeking = (vid_player.state == PLAYER_STATE_SEEKING
	                || vid_player.state == PLAYER_STATE_PREPARE_SEEKING);

	v.duration_ms    = vid_player.media_duration;
	v.seek_progress  = vid_player.seek_progress;
	v.is_seeking     = is_seeking;
	v.is_drag_active = vid_player.seek_bar.selected;

	if(v.is_drag_active)
		v.display_pos_ms = vid_player.seek_pos_cache;
	else if(is_seeking)
		v.display_pos_ms = vid_player.seek_pos;
	else
		v.display_pos_ms = vid_player.media_current_pos;

	return v;
}
