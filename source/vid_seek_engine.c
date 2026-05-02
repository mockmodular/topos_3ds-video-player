//Includes.
#include "video_player.h"
#include "vid_state.h"
#include "vid_seekbar.h"
#include "vid_seek_engine.h"

#include "system/util/err.h"
#include "system/util/log.h"
#include "system/util/util.h"

//Code.

/* 左右键/摇杆：按下只动 seek_pos_cache，松手才 commit+submit */
static bool s_kbd_lr_previewing;
static int8_t s_kbd_lr_dir; /* +1 前进预览，-1 后退预览 */

/* Align with Vid_hid_enqueue_seek: no seek while no playable session yet. */
static bool seek_engine_can_submit(void)
{
	return !(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING);
}

/* 与 vid_decode DECODE_THREAD_SEEK_REQUEST 一致：仅 PREPARE_SEEKING/SEEKING 叠 seek 时 defer；BUFFERING 不 busy。 */
static bool seek_engine_seek_busy(void)
{
	return vid_player.state == PLAYER_STATE_PREPARE_SEEKING
	    || vid_player.state == PLAYER_STATE_SEEKING;
}

/* 步进基准：管道内已有「用户选定」的 seek_pos 时（含 deferred / seek 后填缓冲 BUFFERING）应相对 seek_pos 累加，避免相对 media 双计。 */
static bool seek_engine_step_base_is_seek_pos(void)
{
	return vid_player.state == PLAYER_STATE_PREPARE_SEEKING
	    || vid_player.state == PLAYER_STATE_SEEKING
	    || vid_player.seek_request_deferred
	    || (vid_player.state == PLAYER_STATE_BUFFERING
	        && (vid_player.sub_state & PLAYER_SUB_STATE_POST_SEEK_BUFFERING));
}

/* Clamp seek_pos, enqueue DECODE_THREAD_SEEK_REQUEST (bar commit + step seek share this). */
static void seek_engine_submit(void)
{
	uint32_t result = DEF_ERR_OTHER;

	if(!seek_engine_can_submit())
		return;

	/* 合法 seek 门闩：未真正开始播放前禁止提交 demux seek（预览仍可改 seek_pos_cache）。 */
	if(vid_player.playback_not_started)
		return;

	if(vid_player.media_duration > 0.0)
		vid_player.seek_pos = Util_min_d(Util_max_d(vid_player.seek_pos, 0.0), vid_player.media_duration);
	else if(vid_player.seek_pos < 0.0)
		vid_player.seek_pos = 0.0;

	/* 同一时间只跑一波 seek（含 BUFFERING）；忙则 deferred，解码线程在「本波开始」+100ms 只入队最新 seek_pos。 */
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

void VidSeekEngine_mark_playback_started(void)
{
	vid_player.playback_not_started = false;
}

/* 将 playback_not_started 置 true 的唯一实现体。调用点：入队打开、Lifecycle 的 init/exit 复位。 */
void VidSeekEngine_mark_playback_not_started(void)
{
	vid_player.playback_not_started = true;
}

static void seek_engine_clamp_seek_pos_cache(void)
{
	if(vid_player.media_duration > 0.0)
		vid_player.seek_pos_cache = Util_min_d(Util_max_d(vid_player.seek_pos_cache, 0.0), vid_player.media_duration);
	else if(vid_player.seek_pos_cache < 0.0)
		vid_player.seek_pos_cache = 0.0;
}

void VidSeekEngine_cancel_kbd_preview(void)
{
	s_kbd_lr_previewing = false;
	s_kbd_lr_dir = 0;
}

void VidSeekEngine_on_bar_drag(int16_t touch_x)
{
	VidSeekEngine_cancel_kbd_preview();
	vid_player.seek_pos_cache = VidSeekBar_touch_x_to_pos(touch_x, vid_player.media_duration);
}

void VidSeekEngine_on_bar_commit(void)
{
	VidSeekEngine_cancel_kbd_preview();
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

	VidSeekEngine_cancel_kbd_preview();
	double current_pos = seek_engine_step_base_is_seek_pos()
		? vid_player.seek_pos : vid_player.media_current_pos;

	vid_player.seek_pos = current_pos + DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);
	seek_engine_submit();
}

void VidSeekEngine_on_step_back(void)
{
	if(!seek_engine_can_submit())
		return;

	VidSeekEngine_cancel_kbd_preview();
	double current_pos = seek_engine_step_base_is_seek_pos()
		? vid_player.seek_pos : vid_player.media_current_pos;

	vid_player.seek_pos = current_pos - DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);
	seek_engine_submit();
}

void VidSeekEngine_on_kbd_lr_preview_fwd(void)
{
	const double step = DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);

	if(!seek_engine_can_submit())
	{
		VidSeekEngine_cancel_kbd_preview();
		return;
	}

	if(!s_kbd_lr_previewing || s_kbd_lr_dir != 1)
	{
		const double base = seek_engine_step_base_is_seek_pos()
			? vid_player.seek_pos : vid_player.media_current_pos;

		vid_player.seek_pos_cache = base + step;
		s_kbd_lr_previewing = true;
		s_kbd_lr_dir = 1;
	}
	else
		vid_player.seek_pos_cache += step;

	seek_engine_clamp_seek_pos_cache();
}

void VidSeekEngine_on_kbd_lr_preview_back(void)
{
	const double step = DEF_UTIL_S_TO_MS_D(vid_player.seek_duration);

	if(!seek_engine_can_submit())
	{
		VidSeekEngine_cancel_kbd_preview();
		return;
	}

	if(!s_kbd_lr_previewing || s_kbd_lr_dir != -1)
	{
		const double base = seek_engine_step_base_is_seek_pos()
			? vid_player.seek_pos : vid_player.media_current_pos;

		vid_player.seek_pos_cache = base - step;
		s_kbd_lr_previewing = true;
		s_kbd_lr_dir = -1;
	}
	else
		vid_player.seek_pos_cache -= step;

	seek_engine_clamp_seek_pos_cache();
}

bool VidSeekEngine_on_kbd_lr_release(void)
{
	if(!s_kbd_lr_previewing)
		return false;

	s_kbd_lr_previewing = false;
	s_kbd_lr_dir = 0;

	if(!seek_engine_can_submit())
		return false;

	vid_player.seek_pos = vid_player.seek_pos_cache;
	seek_engine_submit();
	return true;
}

VidSeekEngineView VidSeekEngine_get_view(void)
{
	VidSeekEngineView v;
	/* 预览 cache > 已提交目标 seek_pos > 播放头 media_current_pos（见 vid_seek_engine.h 说明）。 */
	bool show_committed_seek_target = (vid_player.state == PLAYER_STATE_SEEKING
	                                || vid_player.state == PLAYER_STATE_PREPARE_SEEKING
	                                || (vid_player.state == PLAYER_STATE_BUFFERING && vid_player.seek_request_deferred));

	v.duration_ms    = vid_player.media_duration;
	v.is_seeking     = show_committed_seek_target;
	v.is_drag_active = (vid_player.seek_bar.selected || s_kbd_lr_previewing);

	if(v.is_drag_active)
		v.display_pos_ms = vid_player.seek_pos_cache;
	else if(show_committed_seek_target)
		v.display_pos_ms = vid_player.seek_pos;
	else
		v.display_pos_ms = vid_player.media_current_pos;

	return v;
}
