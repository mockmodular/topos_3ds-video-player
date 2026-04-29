//Includes.
#include "video_player.h"
extern void memcpy_asm(uint8_t*, uint8_t*, uint32_t);
#include "vid_state.h"
#include "vid_panel.h"
#include "vid_texture.h"
#include "vid_settings.h"
#include "vid_sync.h"
#include "vid_screen.h"
#include "vid_hid.h"
#include "vid_cmd.h"
#include "vid_worker.h"
#include "vid_lifecycle.h"
#include "vid_decode.h"
#include "vid_draw.h"

#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <malloc.h>
#include <libavutil/cpu.h>

#include "system/draw/draw.h"
#include "system/sem.h"
#include "system/util/converter.h"
#include "system/util/err.h"
#include "system/util/file.h"
#include "system/util/hid.h"
#include "system/util/hw_config.h"
#include "system/util/log.h"
#include "system/util/speaker.h"
#include "system/util/watch.h"
#include "system/util/sync.h"


//Prototypes.
static void Vid_init_variable(void);
static void Vid_init_player_data(void);
static void Vid_init_ui_data(void);
//Removed static from thread to make symbol accessble from other files (for debug).
void Vid_init_thread(void* arg);
void Vid_exit_thread(void* arg);

//Variables.
Vid_player vid_player = { 0, };
//Code.
bool Vid_query_init_flag(void)
{
	return vid_player.inited;
}

bool Vid_query_running_flag(void)
{
	return vid_player.main_run;
}

uint8_t Vid_get_use_hw_color_conversion(void)
{
	return vid_player.use_hw_color_conversion;
}

void Vid_set_use_hw_color_conversion(uint8_t value)
{
	vid_player.use_hw_color_conversion_pending = value;
	/* 仅完全未占用解码会话时与 active 同步；PREPARE_PLAYING 由解码线程 open 前统一从 pending 拷贝 */
	if(vid_player.state == PLAYER_STATE_IDLE)
		vid_player.use_hw_color_conversion = value;
}

bool Vid_get_use_hw_decoding(void)
{
	return vid_player.use_hw_decoding;
}

void Vid_set_use_hw_decoding(bool value)
{
	vid_player.use_hw_decoding_pending = value;
	if(vid_player.state == PLAYER_STATE_IDLE)
		vid_player.use_hw_decoding = value;
}

void Vid_resume(void)
{
	/* 与 Sem `is_bottom_lcd_on` 一致：每次进入可播放/可交互运行态时默认点亮底屏（逻辑亮 + HW 线程随后跟上） */
	Vid_exit_full_screen();

	vid_player.thread_suspend = false;
	vid_player.main_run = true;
	//Reset key state on scene change.
	Util_hid_reset_key_state(HID_KEY_BIT_ALL);
	Draw_set_refresh_needed(true);

	for(uint32_t i = 0; i < EYE_MAX; i++)
		vid_player.next_frame_update_time[i] = (osGetTime() + vid_player.video_frametime[i]);
}

void Vid_suspend(void)
{
	/* Legacy: used to return to main menu / exit; UI entry points removed. */
	vid_player.thread_suspend = true;
	vid_player.main_run = false;
}

void Vid_init(bool draw)
{
	DEF_LOG_STRING("Initializing...");
	uint32_t result = DEF_ERR_OTHER;
	Sem_state state = { 0, };

	//Reset everything first.
	memset(&vid_player, 0x00, sizeof(Vid_player));

	Sem_get_state(&state);
	DEF_LOG_RESULT_SMART(result, Util_str_init(&vid_player.status), (result == DEF_SUCCESS), result);

	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.status.sequential_id, sizeof(vid_player.status.sequential_id));

	if(DEF_SEM_MODEL_IS_NEW(state.console_model) && Util_is_core_available(2))
		vid_player.init_thread = threadCreate(Vid_init_thread, NULL, DEF_THREAD_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 2, false);
	else
	{
		APT_SetAppCpuTimeLimit(80);
		vid_player.init_thread = threadCreate(Vid_init_thread, NULL, DEF_THREAD_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 1, false);
	}

	while(!vid_player.inited)
	{
		if(draw)
			Vid_draw_init_exit_message();
		else
			Util_sleep(20000);
	}

	DEF_LOG_RESULT_SMART(result, threadJoin(vid_player.init_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	threadFree(vid_player.init_thread);

	Util_str_clear(&vid_player.status);
	Vid_resume();

	DEF_LOG_STRING("Initialized.");
}

void Vid_exit(bool draw)
{
	DEF_LOG_STRING("Exiting...");
	uint32_t result = DEF_ERR_OTHER;

	vid_player.exit_thread = threadCreate(Vid_exit_thread, NULL, DEF_THREAD_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 1, false);

	while(vid_player.inited)
	{
		if(draw)
			Vid_draw_init_exit_message();
		else
			Util_sleep(20000);
	}

	DEF_LOG_RESULT_SMART(result, threadJoin(vid_player.exit_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	threadFree(vid_player.exit_thread);

	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.status.sequential_id);
	Util_str_free(&vid_player.status);
	Draw_set_refresh_needed(true);

	Vid_panel_exit();

	//Reset everything.
	memset(&vid_player, 0x00, sizeof(Vid_player));

	DEF_LOG_STRING("Exited.");
}

static void Vid_init_variable(void)
{
	Vid_init_settings();
	Vid_init_hidden_settings();
	Vid_init_debug_view_data();
	Vid_init_desync_data();
	Vid_init_player_data();
	Vid_init_media_data();
	Vid_init_video_data();
	Vid_init_audio_data();
	Vid_init_ui_data();
}


void Vid_init_debug_view_data(void)
{
	vid_player.previous_ts = 0;
	vid_player.total_dropped_frames = 0;

	for(uint8_t i = 0; i < DEBUG_GRAPH_TEMP_ELEMENTS; i++)
		vid_player.frame_list[i] = NULL;
}

static void Vid_init_player_data(void)
{
	vid_player.state = PLAYER_STATE_IDLE;
	vid_player.sub_state = PLAYER_SUB_STATE_NONE;
	memset(vid_player.file.name, 0x0, sizeof(vid_player.file.name));
	memset(vid_player.file.directory, 0x0, sizeof(vid_player.file.directory));
	vid_player.file.index = 0;
}

void Vid_init_media_data(void)
{
	vid_player.is_eof = false;
	vid_player.media_duration = 0;
	vid_player.media_current_pos = 0;
	vid_player.seek_pos_cache = 0;
	vid_player.seek_pos = 0;
	vid_player.seek_queued_pos_ms = 0;
	vid_player.seek_demux_target_ms = 0;
	vid_player.seek_start_pos_after_jump = VID_SEEK_JUMP_ANCHOR_UNSET;
	vid_player.seek_request_deferred = false;
	vid_player.seek_exec_epoch_start_ms = 0;
	vid_player.seek_stall_rescue_packets = 0;
}

void Vid_init_video_data(void)
{
	u64 current_ts = osGetTime();

	vid_player.num_of_video_tracks = 0;
	vid_player.next_vfps_update = (current_ts + 1000);
	vid_player.buffer_progress = 0;
	vid_player.is_sbs_3d = false;

	for(uint32_t i = 0; i < EYE_MAX; i++)
	{
		vid_player.next_store_index[i] = 0;
		vid_player.next_draw_index[i] = 0;
		vid_player.vps[i] = 0;
		vid_player.vps_cache[i] = 0;
		vid_player.next_frame_update_time[i] = current_ts;
		vid_player.video_frametime[i] = 0;
		vid_player.video_x_offset[i] = 0;
		vid_player.video_y_offset[i] = 15;
		vid_player.video_zoom[i] = 1;
		vid_player.video_current_pos[i] = 0;
		for(uint8_t b = 0; b < VIDEO_BUFFERS; b++)
			vid_player.video_buffer_pts[b][i] = -1.0;
		vid_player.video_info[i].width = 0;
		vid_player.video_info[i].height = 0;
		vid_player.video_info[i].codec_width = 0;
		vid_player.video_info[i].codec_height = 0;
		vid_player.video_info[i].framerate = 0;
		memset(vid_player.video_info[i].format_name, 0x0, sizeof(vid_player.video_info[i].format_name));
		memset(vid_player.video_info[i].short_format_name, 0x0, sizeof(vid_player.video_info[i].short_format_name));
		strcpy(vid_player.video_info[i].format_name, "n/a");
		strcpy(vid_player.video_info[i].short_format_name, "n/a");
		vid_player.video_info[i].duration = 0;
		vid_player.video_info[i].thread_type = MEDIA_THREAD_TYPE_NONE;
		vid_player.video_info[i].sar_width = 1;
		vid_player.video_info[i].sar_height = 1;
		vid_player.video_info[i].pixel_format = RAW_PIXEL_INVALID;
	}

	Util_sync_lock(&vid_player.texture_init_free_lock, UINT64_MAX);
	for(uint8_t i = 0; i < VIDEO_BUFFERS; i++)
	{
		for(uint32_t k = 0; k < EYE_MAX; k++)
			Vid_large_texture_free(&vid_player.large_image[i][k]);
	}
	Util_sync_unlock(&vid_player.texture_init_free_lock);

	linearFree(vid_player.sbs_right_buf);
	vid_player.sbs_right_buf = NULL;
}

void Vid_init_audio_data(void)
{
	vid_player.num_of_audio_tracks = 0;
	vid_player.audio_current_pos = 0;
	vid_player.last_decoded_audio_pos = 0;

	for(uint8_t i = 0; i < DEF_DECODER_MAX_AUDIO_TRACKS; i++)
	{
		vid_player.audio_info[i].bitrate = 0;
		vid_player.audio_info[i].sample_rate = 0;
		vid_player.audio_info[i].ch = 0;
		vid_player.audio_info[i].duration = 0;
		memset(vid_player.audio_info[i].format_name, 0x0, sizeof(vid_player.audio_info[i].format_name));
		memset(vid_player.audio_info[i].short_format_name, 0x0, sizeof(vid_player.audio_info[i].short_format_name));
		memset(vid_player.audio_info[i].track_lang, 0x0, sizeof(vid_player.audio_info[i].track_lang));
		strcpy(vid_player.audio_info[i].format_name, "n/a");
		strcpy(vid_player.audio_info[i].short_format_name, "n/a");
		strcpy(vid_player.audio_info[i].track_lang, "n/a");
		vid_player.audio_info[i].sample_format = RAW_SAMPLE_INVALID;
	}
}


static void Vid_init_ui_data(void)
{
	vid_player.is_waiting_home_menu = false;
	vid_player.is_setting_volume = false;
	vid_player.is_setting_seek_duration = false;
	vid_player.must_resume_after_home_menu = false;

	{
		VidPanelCtx panel_ctx = {
			.player       = &vid_player,
			.panel        = &vid_player.panel,
			.open_file_cb = NULL,   /* vid_panel.c 内部使用 panel_open_file */
		};
		Vid_panel_init(panel_ctx);
	}
}



void Vid_init_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	uint32_t result = DEF_ERR_OTHER;
	Sem_state state = { 0, };

	Sem_get_state(&state);

	Vid_init_variable();
	Vid_exit_full_screen();

	DEF_LOG_RESULT_SMART(result, Util_sync_create(&vid_player.texture_init_free_lock, SYNC_TYPE_NON_RECURSIVE_MUTEX), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_sync_create(&vid_player.delay_update_lock, SYNC_TYPE_NON_RECURSIVE_MUTEX), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_sync_create(&vid_player.play_request_pending_lock, SYNC_TYPE_NON_RECURSIVE_MUTEX), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Vid_cmd_init(), (result == DEF_SUCCESS), result);

	vid_player.seek_bar = Draw_get_empty_image();

	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.state, sizeof(vid_player.state));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.user_playback_paused, sizeof(vid_player.user_playback_paused));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.is_setting_seek_duration, sizeof(vid_player.is_setting_seek_duration));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.is_setting_volume, sizeof(vid_player.is_setting_volume));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.use_hw_decoding_pending, sizeof(vid_player.use_hw_decoding_pending));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.use_hw_color_conversion_pending, sizeof(vid_player.use_hw_color_conversion_pending));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.use_multi_threaded_decoding, sizeof(vid_player.use_multi_threaded_decoding));

	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.texture_filter_mode, sizeof(vid_player.texture_filter_mode));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_scale_mode, sizeof(vid_player.video_scale_mode));

	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.seek_pos_cache, sizeof(vid_player.seek_pos_cache));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.seek_pos, sizeof(vid_player.seek_pos));
	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.buffer_progress, sizeof(vid_player.buffer_progress));

	Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.seek_bar.selected, sizeof(vid_player.seek_bar.selected));

	for(uint32_t i = 0; i < EYE_MAX; i++)
	{
		Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_x_offset[i], sizeof(vid_player.video_x_offset[i]));
		Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_y_offset[i], sizeof(vid_player.video_y_offset[i]));
		Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_zoom[i], sizeof(vid_player.video_zoom[i]));
	}

	DEF_LOG_RESULT_SMART(result, Util_queue_create(&vid_player.decode_thread_command_queue, 200), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_queue_create(&vid_player.decode_thread_notification_queue, 100), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_queue_create(&vid_player.read_packet_thread_command_queue, 200), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_queue_create(&vid_player.decode_video_thread_command_queue, 200), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_queue_create(&vid_player.convert_thread_command_queue, 200), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_queue_create(&vid_player.audio_decode_thread_command_queue, 200), (result == DEF_SUCCESS), result);

	DEF_LOG_RESULT_SMART(result, Vid_load_settings(), (result == DEF_SUCCESS), result);
	Vid_panel_files_sync_after_settings_load();
	Vid_init_hidden_settings();

	/* 每次启动：底屏固定为「亮」（两态之一）；设置读档后仍保证 Sem 与下屏电源一致 */
	Vid_exit_full_screen();
	vid_player.auto_full_screen_count = 0;

	vid_player.thread_run = true;
	vid_player.thread_suspend = true;
	vid_player.decode_thread = threadCreate(Vid_decode_thread, NULL, DEF_THREAD_STACKSIZE, DEF_THREAD_PRIORITY_REALTIME, 0, false);
	vid_player.decode_video_thread = threadCreate(Vid_decode_video_thread, NULL, 1024 * 1024, DEF_THREAD_PRIORITY_NORMAL, 1, false);
	vid_player.convert_thread = threadCreate(Vid_convert_thread, NULL, DEF_THREAD_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 0, false);
	vid_player.read_packet_thread = threadCreate(Vid_read_packet_thread, NULL, DEF_THREAD_STACKSIZE, DEF_THREAD_PRIORITY_REALTIME, 0, false);
	vid_player.audio_decode_thread = threadCreate(Vid_audio_decode_thread, NULL, DEF_THREAD_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, 0, false);

	vid_player.inited = true;

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}

void Vid_exit_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	uint32_t result = DEF_ERR_OTHER;

	vid_player.inited = false;
	vid_player.thread_suspend = false;

	DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_SHUTDOWN_REQUEST,
	NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_SEND_TO_FRONT), (result == DEF_SUCCESS), result);

	//Exit full-screen to avoid bottom LCD blackout.
	Vid_exit_full_screen();

	DEF_LOG_RESULT_SMART(result, threadJoin(vid_player.decode_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, threadJoin(vid_player.decode_video_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, threadJoin(vid_player.convert_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, threadJoin(vid_player.read_packet_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, threadJoin(vid_player.audio_decode_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);

	threadFree(vid_player.decode_thread);
	threadFree(vid_player.decode_video_thread);
	threadFree(vid_player.convert_thread);
	threadFree(vid_player.read_packet_thread);
	threadFree(vid_player.audio_decode_thread);

	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.state);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.user_playback_paused);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.is_setting_seek_duration);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.is_setting_volume);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.use_hw_decoding_pending);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.use_hw_color_conversion_pending);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.use_multi_threaded_decoding);

	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.texture_filter_mode);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_scale_mode);

	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.seek_pos_cache);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.seek_pos);
	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.buffer_progress);

	Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.seek_bar.selected);

	for(uint32_t i = 0; i < EYE_MAX; i++)
	{
		Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_x_offset[i]);
		Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_y_offset[i]);
		Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &vid_player.video_zoom[i]);
	}

	Util_queue_delete(&vid_player.decode_thread_command_queue);
	Util_queue_delete(&vid_player.decode_thread_notification_queue);
	Util_queue_delete(&vid_player.read_packet_thread_command_queue);
	Util_queue_delete(&vid_player.decode_video_thread_command_queue);
	Util_queue_delete(&vid_player.convert_thread_command_queue);
	Util_queue_delete(&vid_player.audio_decode_thread_command_queue);

	Util_sync_lock(&vid_player.play_request_pending_lock, UINT64_MAX);
	if(vid_player.play_request_pending)
	{
		free(vid_player.play_request_pending);
		vid_player.play_request_pending = NULL;
	}
	Util_sync_unlock(&vid_player.play_request_pending_lock);
	Util_sync_destroy(&vid_player.play_request_pending_lock);

	Util_sync_destroy(&vid_player.texture_init_free_lock);
	Util_sync_destroy(&vid_player.delay_update_lock);

	Vid_cmd_destroy();

	vid_player.inited = false;

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}
