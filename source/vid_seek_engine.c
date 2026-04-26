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

/* 与 vid_decode DECODE_THREAD_SEEK_REQUEST 一致：这些状态下不叠新 demux。 */
static bool seek_engine_seek_busy(void)
{
	return vid_player.state == PLAYER_STATE_PREPARE_SEEKING
	    || vid_player.state == PLAYER_STATE_SEEKING
	    || vid_player.state == PLAYER_STATE_BUFFERING;
}

/* 步进基准：管道内已有「用户选定」的 seek_pos 时（含 deferred 等 BUFFERING）应相对 seek_pos 累加，避免相对 media 双计。 */
static bool seek_engine_step_base_is_seek_pos(void)
{
	return vid_player.state == PLAYER_STATE_PREPARE_SEEKING
	    || vid_player.state == PLAYER_STATE_SEEKING
	    || vid_player.seek_request_deferred;
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

	/* 同一时间只跑一波 seek（含 BUFFERING）；忙则只记结束后用当前 seek_pos 再 seek 一次。 */
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
	/* 与 submit 一致：未进入可播放会话前勿写 seek_pos，否则 OPEN 完成后的首帧会与错误目标不一致。 */
	if(!seek_engine_can_submit())
		return;

	vid_player.seek_pos = vid_player.seek_pos_cache;
	seek_engine_submit();
}

void VidSeekEngine_on_step_fwd(void)
{
	if(!seek_engine_can_submit())
		return;

	double current_pos = seek_engine_step_base_is_seek_pos()
		? vid_player.seek_pos : vid_player.media_current_pos;

	vid_player.seek_pos = current_pos + DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);
	seek_engine_submit();
}

void VidSeekEngine_on_step_back(void)
{
	if(!seek_engine_can_submit())
		return;

	double current_pos = seek_engine_step_base_is_seek_pos()
		? vid_player.seek_pos : vid_player.media_current_pos;

	vid_player.seek_pos = current_pos - DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);
	seek_engine_submit();
}

VidSeekEngineView VidSeekEngine_get_view(void)
{
	VidSeekEngineView v;
	/* BUFFERING 且已有延后 seek 时，进度条应显示「将要去」的 seek_pos，避免先弹回 media_current_pos。 */
	bool show_committed_seek_target = (vid_player.state == PLAYER_STATE_SEEKING
	                                || vid_player.state == PLAYER_STATE_PREPARE_SEEKING
	                                || (vid_player.state == PLAYER_STATE_BUFFERING && vid_player.seek_request_deferred));

	v.duration_ms    = vid_player.media_duration;
	v.is_seeking     = show_committed_seek_target;
	v.is_drag_active = vid_player.seek_bar.selected;

	if(v.is_drag_active)
		v.display_pos_ms = vid_player.seek_pos_cache;
	else if(show_committed_seek_target)
		v.display_pos_ms = vid_player.seek_pos;
	else
		v.display_pos_ms = vid_player.media_current_pos;

	return v;
}
