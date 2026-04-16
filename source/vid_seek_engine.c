//Includes.
#include "video_player.h"
#include "vid_state.h"
#include "vid_seekbar.h"
#include "vid_seek_engine.h"

#include "system/util/err.h"
#include "system/util/log.h"
#include "system/util/util.h"

//Code.

/* Align with Vid_hid_enqueue_seek: no seek while no playable session yet. */
static bool seek_engine_can_submit(void)
{
	return !(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING);
}

/* True while decode/read path is already executing one seek wave (no overlapping demux). */
static bool seek_engine_seek_busy(void)
{
	return vid_player.state == PLAYER_STATE_PREPARE_SEEKING
	    || vid_player.state == PLAYER_STATE_SEEKING;
}

/* Clamp seek_pos, enqueue DECODE_THREAD_SEEK_REQUEST (bar commit + step seek share this). */
static void seek_engine_submit(void)
{
	uint32_t result = DEF_ERR_OTHER;

	if(!seek_engine_can_submit())
		return;

	if(vid_player.media_duration > 0.0)
		vid_player.seek_pos = Util_min_d(Util_max_d(vid_player.seek_pos, 0.0), vid_player.media_duration);
	else if(vid_player.seek_pos < 0.0)
		vid_player.seek_pos = 0.0;

	/* 同一时间只跑一波 seek；忙则只记「结束后要再 seek 一次」并保留最新 seek_pos。 */
	if(seek_engine_seek_busy())
	{
		vid_player.seek_request_deferred = true;
		return;
	}

	/* 先冻结 seek_queued 再入队，避免解码线程在「入队成功、主线程尚未写 seek_queued」的窗口读到旧值。
	 * 若 DO_NOT_ADD_IF_EXIST 失败则回滚 seek_queued，避免污染队列里尚未处理的那次 seek 的目标。 */
	{
		double saved_queued = vid_player.seek_queued_pos_ms;

		vid_player.seek_queued_pos_ms = vid_player.seek_pos;
		DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue,
			DECODE_THREAD_SEEK_REQUEST, NULL, QUEUE_OP_TIMEOUT_US,
			(Queue_option)(QUEUE_OPTION_SEND_TO_FRONT | QUEUE_OPTION_DO_NOT_ADD_IF_EXIST)),
			(result == DEF_SUCCESS), result);
		if(result != DEF_SUCCESS)
		{
			vid_player.seek_queued_pos_ms = saved_queued;
			vid_player.seek_request_deferred = true;
		}
	}
}

void VidSeekEngine_on_bar_drag(int16_t touch_x)
{
	vid_player.seek_pos_cache = VidSeekBar_touch_x_to_pos(touch_x, vid_player.media_duration);
}

void VidSeekEngine_on_bar_commit(void)
{
	vid_player.seek_pos = vid_player.seek_pos_cache;
	seek_engine_submit();
}

void VidSeekEngine_on_step_fwd(void)
{
	/* 连按左右：在 seek 波进行中必须用 seek_pos 累加。
	 * seek_queued_pos_ms 整波冻结，若用它作基准则第 2 次及以后的按键都会算成「同一起点 + 一步」，无法连续快进。 */
	double current_pos = (vid_player.state == PLAYER_STATE_SEEKING || vid_player.state == PLAYER_STATE_PREPARE_SEEKING)
		? vid_player.seek_pos : vid_player.media_current_pos;

	vid_player.seek_pos = current_pos + DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);
	seek_engine_submit();
}

void VidSeekEngine_on_step_back(void)
{
	double current_pos = (vid_player.state == PLAYER_STATE_SEEKING || vid_player.state == PLAYER_STATE_PREPARE_SEEKING)
		? vid_player.seek_pos : vid_player.media_current_pos;

	vid_player.seek_pos = current_pos - DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);
	seek_engine_submit();
}

VidSeekEngineView VidSeekEngine_get_view(void)
{
	VidSeekEngineView v;
	bool is_seeking = (vid_player.state == PLAYER_STATE_SEEKING
	                || vid_player.state == PLAYER_STATE_PREPARE_SEEKING);

	v.duration_ms    = vid_player.media_duration;
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
