//Includes.
#include "video_player.h"
extern void memcpy_asm(uint8_t*, uint8_t*, uint32_t);
#include "vid_state.h"
#include "vid_texture.h"
#include "vid_settings.h"
#include "vid_sync.h"
#include "vid_screen.h"
#include "vid_worker.h"
#include "vid_lifecycle.h"
#include "vid_panel.h"
#include "vid_decode.h"

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>

#include "system/sem.h"
#include "system/util/converter.h"
#include "system/util/err.h"
#include "system/util/hid.h"
#include "system/util/log.h"
#include "system/util/speaker.h"
#include "system/util/sync.h"
#include "system/util/watch.h"

//Defines (reference vid_player directly, cannot move to vid_state.h).
#define HW_DECODER_RAW_IMAGE_SIZE				(uint32_t)(vid_player.video_info[EYE_LEFT].width * vid_player.video_info[EYE_LEFT].height * 4)
#define SW_DECODER_RAW_IMAGE_SIZE(index)		(uint32_t)(vid_player.video_info[index].width * vid_player.video_info[index].height * 1.5)

/* fake_model off(255) 且 console_model 为「四核探测下的 O3DS 档」：永不尝试 MVD。
 * fake 强制 O3DS(0)/N3DS(1) 时仍按 use_hw_decoding 尝试 MVD（新机上模拟老机等）。 */
static bool vid_decode_mvd_blocked_real_o3ds_fake_off(void)
{
	uint8_t fm = Sem_query_fake_model();
	if (fm < (uint8_t)DEF_SEM_MODEL_MAX)
		return false;
	{
		Sem_state st = { 0, };
		Sem_get_state(&st);
		return !DEF_SEM_MODEL_IS_NEW(st.console_model);
	}
}

/* After one SEEKING wave hits seek_demux_target_ms: run at most one follow-up seek (latest seek_pos). */
static void vid_decode_finish_seek_wave_try_deferred(void)
{
	uint32_t r = DEF_ERR_OTHER;

	if(!vid_player.seek_request_deferred)
		return;

	{
		double saved_queued = vid_player.seek_queued_pos_ms;

		vid_player.seek_queued_pos_ms = vid_player.seek_pos;
		DEF_LOG_RESULT_SMART(r, Util_queue_add(&vid_player.decode_thread_command_queue,
			DECODE_THREAD_SEEK_REQUEST, NULL, QUEUE_OP_TIMEOUT_US,
			(Queue_option)(QUEUE_OPTION_SEND_TO_FRONT | QUEUE_OPTION_DO_NOT_ADD_IF_EXIST)),
			(r == DEF_SUCCESS), r);
		if(r == DEF_SUCCESS)
			vid_player.seek_request_deferred = false;
		else
		{
			vid_player.seek_queued_pos_ms = saved_queued;
			/* seek_request_deferred 保持 true，缓冲结束等路径会再试 */
		}
	}
}

/* Refresh seek_start_pos_out, flags, speaker, audio flush, and queue demux seek for seek_demux_target_ms. */
static void decode_thread_issue_demux_seek(double *seek_start_pos_out)
{
	uint32_t result = DEF_ERR_OTHER;

	vid_player.seek_demux_target_ms = vid_player.seek_queued_pos_ms;

	*seek_start_pos_out = vid_player.media_current_pos;
	vid_player.seek_start_pos_after_jump = VID_SEEK_JUMP_ANCHOR_UNSET;

	if((*seek_start_pos_out) > vid_player.seek_demux_target_ms)
		vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_SEEK_BACKWARD_WAIT);
	else
		vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_SEEK_BACKWARD_WAIT);

	Util_speaker_clear_buffer(DEF_VID_SPEAKER_SESSION_ID);

	if(vid_player.num_of_audio_tracks > 0)
	{
		DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.audio_decode_thread_command_queue, AUDIO_DECODE_THREAD_SEEK_REQUEST,
		NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_SEND_TO_FRONT), (result == DEF_SUCCESS), result);
	}

	/* read_packet uses seek_demux_target_ms (set above from seek_queued_pos_ms at enqueue time). */
	DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.read_packet_thread_command_queue, READ_PACKET_THREAD_SEEK_REQUEST,
	NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
}

void Vid_decode_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	bool is_eof = false;
	/* 自然播放结束后保留解码会话：为 true 时不再向队列重复投递 PLAY_NEXT */
	bool natural_eof_session_held = false;
	bool is_read_packet_thread_active = false;
	bool is_waiting_video_decoder = false;
	uint8_t backward_timeout = SEEK_BACKWARD_TIMEOUT;
	uint8_t wait_count = SEEK_IGNORE_PACKETS;
	uint32_t result = 0;
	uint32_t audio_bar_pos = 0;
	double seek_start_pos = 0;
	while (vid_player.thread_run)
	{
		uint64_t timeout_us = DEF_THREAD_ACTIVE_SLEEP_TIME;
		void* message = NULL;
		Vid_command event = NONE_REQUEST;
		Vid_notification notification = NONE_NOTIFICATION;

		if(vid_player.state == PLAYER_STATE_IDLE)
		{
			while (vid_player.thread_suspend)
				Util_sleep(DEF_THREAD_INACTIVE_SLEEP_TIME);
		}

		//If player state is not idle, don't wait for commands
		//as we want to decode audio/videos as quick as possible.
		if(vid_player.state != PLAYER_STATE_IDLE)
			timeout_us = 0;

		//Reset afk count so that system won't go sleep.
		if(vid_player.state != PLAYER_STATE_IDLE && vid_player.state != PLAYER_STATE_PAUSE)
			Util_hid_reset_afk_time();

		result = Util_queue_get(&vid_player.decode_thread_command_queue, (uint32_t*)&event, &message, timeout_us);
		if(result == DEF_SUCCESS)
		{
			switch (event)
			{
				case DECODE_THREAD_PLAY_REQUEST:
				{
					Vid_file* new_file = (Vid_file*)message;

					if(!new_file)
					{
						Util_sync_lock(&vid_player.play_request_pending_lock, UINT64_MAX);
						new_file = vid_player.play_request_pending;
						vid_player.play_request_pending = NULL;
						Util_sync_unlock(&vid_player.play_request_pending_lock);
					}

					if(!new_file && !(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING))
						break;

					bool switch_player_panel_from_files_open = false;

					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING)
					{
						uint8_t num_of_audio_tracks = 0;
						uint8_t num_of_video_tracks = 0;
						Str_data path = { 0, };

						Util_str_init(&path);

						//Update the file.
						if(new_file)
						{
							switch_player_panel_from_files_open = new_file->request_player_panel_on_ok;
							snprintf(vid_player.file.name, sizeof(vid_player.file.name), "%s", new_file->name);
							snprintf(vid_player.file.directory, sizeof(vid_player.file.directory), "%s", new_file->directory);
							vid_player.file.index = new_file->index;
							vid_player.file.request_player_panel_on_ok = false;

							free(new_file);
							new_file = NULL;
						}

						Util_str_set(&path, vid_player.file.directory);
						Util_str_add(&path, vid_player.file.name);
						if(!Util_str_has_data(&path))
						{
							Util_str_free(&path);
							break;
						}

						Vid_init_debug_view_data();
						Vid_init_desync_data();
						Vid_init_media_data();
						Vid_init_video_data();
						Vid_init_audio_data();
						vid_player.state = PLAYER_STATE_PLAYING;
						vid_player.sub_state = PLAYER_SUB_STATE_NONE;
						is_eof = false;
						natural_eof_session_held = false;
						is_read_packet_thread_active = false;
						is_waiting_video_decoder = false;

						wait_count = SEEK_IGNORE_PACKETS;
						audio_bar_pos = 0;
						seek_start_pos = 0;
					DEF_LOG_FORMAT("decoder open path=[%s]", DEF_STR_NEVER_NULL(&path));

					DEF_LOG_RESULT_SMART(result, Util_decoder_open_file(path.buffer, &num_of_audio_tracks, &num_of_video_tracks, DEF_VID_DECORDER_SESSION_ID), (result == DEF_SUCCESS), result);

					if(result != DEF_SUCCESS)
					{
						Util_str_free(&path);
						goto error;
					}

					Util_str_free(&path);

						//Overwirte number of tracks if disable flag is set.
						if(vid_player.disable_audio)
						{
							num_of_audio_tracks = 0;
							for(uint8_t i = 0; i < DEF_DECODER_MAX_AUDIO_TRACKS; i++)
								strcpy(vid_player.audio_info[i].format_name, "disabled");
						}
						if(vid_player.disable_video)
						{
							num_of_video_tracks = 0;
							for(uint32_t i = 0; i < EYE_MAX; i++)
							{
								strcpy(vid_player.video_info[i].format_name, "disabled");
								vid_player.video_info[i].is_av1_codec = false;
							}
						}

						if(num_of_audio_tracks > 0)
						{
							DEF_LOG_RESULT_SMART(result, Util_speaker_init(), (result == DEF_SUCCESS), result);
							if(result != DEF_SUCCESS)
							{
								DEF_LOG_RESULT(Util_speaker_init, false, result);
								//Continue initialization without error dialog (video-only playback).
							}

							DEF_LOG_RESULT_SMART(result, Util_decoder_audio_init(num_of_audio_tracks, DEF_VID_DECORDER_SESSION_ID), (result == DEF_SUCCESS), result);
							if(result == DEF_SUCCESS)
							{
								uint8_t playing_ch = 0;

								for(uint8_t i = 0; i < num_of_audio_tracks; i++)
									Util_decoder_audio_get_info(&vid_player.audio_info[i], i, DEF_VID_DECORDER_SESSION_ID);


								//3DS only supports up to 2ch.
								playing_ch = (vid_player.audio_info[0].ch > 2 ? 2 : vid_player.audio_info[0].ch);
								DEF_LOG_RESULT_SMART(result, Util_speaker_set_audio_info(DEF_VID_SPEAKER_SESSION_ID, playing_ch, vid_player.audio_info[0].sample_rate), (result == DEF_SUCCESS), result);

								vid_player.num_of_audio_tracks = num_of_audio_tracks;
							}
							else
							{
								//If audio format is not supported, disable audio so that video can be played without audio.
								Util_speaker_exit();
								vid_player.num_of_audio_tracks = 0;
								for(uint32_t i = 0; i < num_of_audio_tracks; i++)
									strcpy(vid_player.audio_info[i].format_name, "Unsupported format");

								//Ignore the error.
								result = 0;
							}
						}

						if(num_of_video_tracks > 0)
						{
							uint8_t request_threads = (vid_player.use_multi_threaded_decoding ? vid_player.num_of_threads : 1);
							Media_thread_type request_thread_type = vid_player.use_multi_threaded_decoding ? vid_player.thread_mode : MEDIA_THREAD_TYPE_NONE;

							//Video player only supports up to EYE_MAX tracks, decoder may support more so cap it here.
							num_of_video_tracks = Util_min(num_of_video_tracks, EYE_MAX);

							DEF_LOG_RESULT_SMART(result, Util_decoder_video_init(0, num_of_video_tracks,
							request_threads, request_thread_type, DEF_VID_DECORDER_SESSION_ID), (result == DEF_SUCCESS), result);
							if(result == DEF_SUCCESS)
							{
								Sem_config sem_cfg = { 0, };
								Sem_state state = { 0, };

								Sem_get_config(&sem_cfg);
								Sem_get_state(&state);

							//SBS 3D mode: explicit 3D setting, or AUTO with matching resolution (h==240, w>=640).
							vid_player.is_sbs_3d = (sem_cfg.screen_mode == DEF_SEM_SCREEN_MODE_3D);

							for(uint8_t i = 0; i < num_of_video_tracks; i++)
							{
								Util_decoder_video_get_info(&vid_player.video_info[i], i, DEF_VID_DECORDER_SESSION_ID);

								if(vid_player.video_info[i].framerate == 0)
									vid_player.video_frametime[i] = 0;
								else
									vid_player.video_frametime[i] = (1000.0 / vid_player.video_info[i].framerate);
							}

							if(sem_cfg.screen_mode == DEF_SEM_SCREEN_MODE_AUTO && num_of_video_tracks > 0)
							{
								uint32_t w, h;

								/* Presentation Video width/height (not codec padded size, e.g. AV1 128px alignment). */
								Vid_video_presentation_wh(EYE_LEFT, &w, &h);
								/* Auto: SBS 3d if (y=240 and x>=640) or (x=800 and y<=240), else 2d */
								vid_player.is_sbs_3d = ((h == 240 && w >= 640) || (w == 800 && h <= 240));
							}

							if(vid_player.is_sbs_3d)
							{
								for(uint8_t i = 0; i < num_of_video_tracks; i++)
								{
									//In SBS 3D mode, each half-frame is square pixels.
									vid_player.video_info[i].sar_width = 1;
									vid_player.video_info[i].sar_height = 1;
								}
							}

								//Hardware decoder only supports 1 track at a time.
								if(num_of_video_tracks == 1 && vid_player.use_hw_decoding
								&& !vid_decode_mvd_blocked_real_o3ds_fake_off()
								&& vid_player.video_info[EYE_LEFT].pixel_format == RAW_PIXEL_YUV420P
								&& strcmp(vid_player.video_info[EYE_LEFT].format_name, "H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10") == 0)
								{
									/* 先置位再 init：mvdstd 失败（如 O3DS 无可用 MVD）时清位并走软解，不整文件失败 */
									vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_HW_DECODING);
									DEF_LOG_RESULT_SMART(result, Util_decoder_mvd_init(DEF_VID_DECORDER_SESSION_ID), (result == DEF_SUCCESS), result);
									if(result != DEF_SUCCESS)
										vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_HW_DECODING);
								}

								if(!(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING) && vid_player.use_hw_color_conversion)
								{
									bool can_use_hw_color_converter = true;

									//Check if we can use HW color converter (y2r).
									for(uint8_t i = 0; i < num_of_video_tracks; i++)
									{
										if(vid_player.video_info[i].codec_width > DEF_DRAW_MAX_TEXTURE_SIZE || vid_player.video_info[i].codec_height > DEF_DRAW_MAX_TEXTURE_SIZE
										|| (vid_player.video_info[i].pixel_format != RAW_PIXEL_YUV420P && vid_player.video_info[i].pixel_format != RAW_PIXEL_YUVJ420P))
										{
											can_use_hw_color_converter = false;
											break;
										}
									}

									if(can_use_hw_color_converter)
									{
										//We can use hw color converter for this video.
										vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_HW_CONVERSION);

										DEF_LOG_RESULT_SMART(result, Util_converter_y2r_init(), (result == DEF_SUCCESS), result);
										if(result != DEF_SUCCESS)
										{
											goto error;
										}
									}
								}

								//Allocate texture buffers.
								for(uint8_t i = 0; i < VIDEO_BUFFERS; i++)
								{
									for(uint8_t k = 0; k < num_of_video_tracks; k++)
									{
										//In SBS 3D mode, EYE_LEFT texture is half-width (400px).
										uint32_t tex_width = (vid_player.is_sbs_3d && k == EYE_LEFT)
											? vid_player.video_info[k].codec_width / 2
											: vid_player.video_info[k].codec_width;

										Util_sync_lock(&vid_player.texture_init_free_lock, UINT64_MAX);
										result = Vid_large_texture_init(&vid_player.large_image[i][k], tex_width, vid_player.video_info[k].codec_height, RAW_PIXEL_ABGR8888, true);
										Util_sync_unlock(&vid_player.texture_init_free_lock);

										if(result != DEF_SUCCESS)
										{
											DEF_LOG_RESULT(Vid_large_texture_init, false, result);
											goto error;
										}
									}

									//In SBS 3D mode, also allocate right eye texture at half-width.
									if(vid_player.is_sbs_3d)
									{
										Util_sync_lock(&vid_player.texture_init_free_lock, UINT64_MAX);
										result = Vid_large_texture_init(&vid_player.large_image[i][EYE_RIGHT],
											vid_player.video_info[EYE_LEFT].codec_width / 2,
											vid_player.video_info[EYE_LEFT].codec_height, RAW_PIXEL_ABGR8888, true);
										Util_sync_unlock(&vid_player.texture_init_free_lock);

										if(result != DEF_SUCCESS)
										{
											DEF_LOG_RESULT(Vid_large_texture_init, false, result);
											goto error;
										}
									}
								}

							//Apply texture filter.
							for(uint8_t i = 0; i < VIDEO_BUFFERS; i++)
							{
								for(uint32_t k = 0; k < EYE_MAX; k++)
									Vid_large_texture_set_filter(&vid_player.large_image[i][k], Vid_effective_use_linear_texture_filter(k));
							}

						if(vid_player.is_sbs_3d)
						{
							uint32_t half_w = vid_player.video_info[EYE_LEFT].codec_width / 2;
							uint32_t h = vid_player.video_info[EYE_LEFT].codec_height;
						vid_player.sbs_right_buf = (uint8_t*)linearAlloc(half_w * h * 4);
						if(!vid_player.sbs_right_buf)
							{
								goto error;
							}
						}

						{
							APT_SetAppCpuTimeLimit(80);
						}

								vid_player.num_of_video_tracks = num_of_video_tracks;
							}
							else
							{
								//If video format is not supported, disable video so that audio can be played without video.
								vid_player.num_of_video_tracks = 0;
								for(uint8_t i = 0; i < num_of_video_tracks; i++)
								{
									strcpy(vid_player.video_info[i].format_name, "Unsupported format");
									vid_player.video_info[i].is_av1_codec = false;
								}

								//Ignore the error.
								result = 0;
							}
						}


						//Set media duration.
						vid_player.media_duration = DEF_UTIL_S_TO_MS_D(Vid_get_media_duration(vid_player.video_info[EYE_LEFT].duration, vid_player.video_info[EYE_RIGHT].duration, vid_player.audio_info[0].duration));

						//Can't play with no audio and no video.
						if(vid_player.num_of_audio_tracks == 0 && vid_player.num_of_video_tracks == 0)
						{
							result = DEF_ERR_OTHER;
							DEF_LOG_STRING("No playable media!!!!!");
							goto error;
						}

						//Fit video to screen if file has video tracks.
						if(vid_player.num_of_video_tracks > 0 && !Util_err_query_show_flag())
						{
							for(uint32_t i = 0; i < EYE_MAX; i++)
								Vid_fit_to_screen(FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i);

							//Reset key state on scene change.
							Util_hid_reset_key_state(HID_KEY_BIT_ALL);
						}
						else
						{
							for(uint32_t i = 0; i < EYE_MAX; i++)
								Vid_fit_to_screen(NON_FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i);

							Vid_exit_full_screen();
							//Reset key state on scene change.
							Util_hid_reset_key_state(HID_KEY_BIT_ALL);
						}

						//If we have normal videos, buffer some data before starting/resuming playback.
						//If there is seek event in queue, don't do it because it will automatically
						//enter buffering state after seeking.
						if(!Util_queue_check_event_exist(&vid_player.decode_thread_command_queue, DECODE_THREAD_SEEK_REQUEST)
						&& Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime))
						{
							//Pause the playback.
							Util_speaker_pause(DEF_VID_SPEAKER_SESSION_ID);
							//Add resume later bit.
							vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_RESUME_LATER);
							vid_player.state = PLAYER_STATE_BUFFERING;
						}

						{
							double ram_to_keep_base = (vid_player.ram_to_keep_base / 1000.0 / 1000.0);
							DEF_LOG_DOUBLE(ram_to_keep_base);
						}

						//Start reading packets.
						is_read_packet_thread_active = true;
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.read_packet_thread_command_queue, READ_PACKET_THREAD_READ_PACKET_REQUEST,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

						//Start converting color, this thread keeps running unless we send abort request.
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.convert_thread_command_queue, CONVERT_THREAD_CONVERT_REQUEST,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

						if(!Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime))
							Util_watch_add(WATCH_HANDLE_VIDEO_PLAYER, &audio_bar_pos, sizeof(audio_bar_pos));

						//Log media info for better debugging.
						Vid_log_media_info();

						if(switch_player_panel_from_files_open)
						{
							vid_player.panel = VID_PANEL_PLAYER;
							vid_player.auto_full_screen_count = 0;
						}

						break;

						error:
						/* Same citro2d overlay as file-list open failure (not Util_err). */
						Util_err_set_show_flag(false);
						vid_player.panel = VID_PANEL_FILES;
						Vid_panel_files_show_play_error();
						Draw_set_refresh_needed(true);

						//Log media info for better debugging.
						Vid_log_media_info();

						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_ABORT_REQUEST,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_SEND_TO_FRONT), (result == DEF_SUCCESS), result);

						continue;
					}
					else
					{
						//If currently player state is not idle, abort current playback first.
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_ABORT_REQUEST,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

						//Then play new one, pass the received new_file again.
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_PLAY_REQUEST,
						new_file, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
					}

					break;
				}

				case DECODE_THREAD_PAUSE_REQUEST:
				{
					//Do nothing if player state is idle, prepare playing or pause.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING
					|| vid_player.state == PLAYER_STATE_PAUSE)
						break;

					if(vid_player.state == PLAYER_STATE_PLAYING)
					{
						Util_speaker_pause(DEF_VID_SPEAKER_SESSION_ID);
						vid_player.state = PLAYER_STATE_PAUSE;
					}
					else
					{
						//Remove resume later bit.
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_RESUME_LATER);
					}

					break;
				}

				case DECODE_THREAD_RESUME_REQUEST:
				{
					//Do nothing if player state is idle, prepare playing or playing.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING
					|| vid_player.state == PLAYER_STATE_PLAYING)
						break;

					if(vid_player.state == PLAYER_STATE_PAUSE)
					{
						Util_speaker_resume(DEF_VID_SPEAKER_SESSION_ID);
						vid_player.state = PLAYER_STATE_PLAYING;
					}
					else
					{
						//Add resume later bit.
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_RESUME_LATER);
					}

					break;
				}

				case DECODE_THREAD_SEEK_REQUEST:
				{
					//Do nothing if player state is idle or prepare playing.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING)
						break;

					if(vid_player.state == PLAYER_STATE_PREPARE_SEEKING
					|| vid_player.state == PLAYER_STATE_SEEKING)
					{
						/* One seek wave at a time: never re-issue demux while the previous is in flight. */
						vid_player.seek_request_deferred = true;
						break;
					}

					if(vid_player.state == PLAYER_STATE_PLAYING)//Add resume later bit.
					{
						Util_speaker_pause(DEF_VID_SPEAKER_SESSION_ID);
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_RESUME_LATER);
					}
					else if(vid_player.state == PLAYER_STATE_PAUSE)//Remove resume later bit.
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_RESUME_LATER);

					vid_player.state = PLAYER_STATE_PREPARE_SEEKING;
					is_eof = false;
					natural_eof_session_held = false;
					decode_thread_issue_demux_seek(&seek_start_pos);

					break;
				}

				case DECODE_THREAD_PLAY_NEXT_REQUEST:
				{
					/* 播放到末尾：不关文件、不释放纹理，保持文件名与时长，便于拖条重定位；按 B 的 ABORT 才彻底退出 */
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING)
						break;

					if(vid_player.num_of_audio_tracks > 0)
						Util_speaker_pause(DEF_VID_SPEAKER_SESSION_ID);

					vid_player.state = PLAYER_STATE_PAUSE;
					vid_player.sub_state = (Vid_player_sub_state)(
						vid_player.sub_state & PLAYER_SUB_STATE_PERSIST_AFTER_NATURAL_EOF_MASK);
					natural_eof_session_held = true;
					if(vid_player.media_duration > 0.0)
						vid_player.media_current_pos = vid_player.media_duration;

					break;
				}

				case DECODE_THREAD_ABORT_REQUEST:
				case DECODE_THREAD_SHUTDOWN_REQUEST:
				{
					//Do nothing if player state is idle or prepare playing.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING)
					{
						vid_player.state = PLAYER_STATE_IDLE;
						vid_player.sub_state = PLAYER_SUB_STATE_NONE;
						natural_eof_session_held = false;
						memset(vid_player.file.name, 0, sizeof(vid_player.file.name));
						memset(vid_player.file.directory, 0, sizeof(vid_player.file.directory));
						vid_player.file.index = 0;
						vid_player.media_current_pos = 0;
						vid_player.media_duration = 0;
						vid_player.seek_request_deferred = false;
						vid_player.seek_queued_pos_ms = 0;
						vid_player.seek_demux_target_ms = 0;

						if(event == DECODE_THREAD_SHUTDOWN_REQUEST)
						{
							//Exit the threads.
							vid_player.thread_run = false;
							continue;
						}

						break;
					}

					//Wait for read packet thread (also flush queues).
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.read_packet_thread_command_queue, READ_PACKET_THREAD_ABORT_REQUEST,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
					while(true)
					{
						result = Util_queue_get(&vid_player.decode_thread_notification_queue, (uint32_t*)&notification, NULL, QUEUE_OP_TIMEOUT_US);
						if(result == DEF_SUCCESS && notification == READ_PACKET_THREAD_FINISHED_ABORTING_NOTIFICATION)
							break;
					}

					//Wait for convert thread (also flush queues).
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.convert_thread_command_queue, CONVERT_THREAD_ABORT_REQUEST,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
					while(true)
					{
						result = Util_queue_get(&vid_player.decode_thread_notification_queue, (uint32_t*)&notification, NULL, QUEUE_OP_TIMEOUT_US);
						if(result == DEF_SUCCESS && notification == CONVERT_THREAD_FINISHED_ABORTING_NOTIFICATION)
							break;
					}

				//Wait for video decoding thread (also flush queues).
				DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_video_thread_command_queue, DECODE_VIDEO_THREAD_ABORT_REQUEST,
				NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
				while(true)
				{
					result = Util_queue_get(&vid_player.decode_thread_notification_queue, (uint32_t*)&notification, NULL, QUEUE_OP_TIMEOUT_US);
					if(result == DEF_SUCCESS && notification == DECODE_VIDEO_THREAD_FINISHED_ABORTING_NOTIFICATION)
						break;
				}

				//Wait for audio decode thread (also flush queues).
				if(vid_player.num_of_audio_tracks > 0)
				{
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.audio_decode_thread_command_queue, AUDIO_DECODE_THREAD_ABORT_REQUEST,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_SEND_TO_FRONT), (result == DEF_SUCCESS), result);
					while(true)
					{
						result = Util_queue_get(&vid_player.decode_thread_notification_queue, (uint32_t*)&notification, NULL, QUEUE_OP_TIMEOUT_US);
						if(result == DEF_SUCCESS && notification == AUDIO_DECODE_THREAD_FINISHED_ABORTING_NOTIFICATION)
							break;
					}
				}

			if(vid_player.num_of_audio_tracks > 0)
				Util_speaker_exit();

			Util_decoder_close_file(DEF_VID_DECORDER_SESSION_ID);
			if(vid_player.sub_state & PLAYER_SUB_STATE_HW_CONVERSION)
				Util_converter_y2r_exit();
			Util_converter_ffmpeg_cache_clear();

				Util_sync_lock(&vid_player.texture_init_free_lock, UINT64_MAX);
				for(uint8_t i = 0; i < VIDEO_BUFFERS; i++)
				{
					for(uint32_t k = 0; k < EYE_MAX; k++)
						Vid_large_texture_free(&vid_player.large_image[i][k]);
				}
			Util_sync_unlock(&vid_player.texture_init_free_lock);

		linearFree(vid_player.sbs_right_buf);
		vid_player.sbs_right_buf = NULL;

			Vid_init_video_data();
			Vid_init_audio_data();

			if(!Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime))
						Util_watch_remove(WATCH_HANDLE_VIDEO_PLAYER, &audio_bar_pos);

					vid_player.seek_bar.selected = false;

					{
						for(uint32_t i = 0; i < EYE_MAX; i++)
							Vid_fit_to_screen(NON_FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i);

						Vid_exit_full_screen();
						//Reset key state on scene change.
						Util_hid_reset_key_state(HID_KEY_BIT_ALL);
						vid_player.state = PLAYER_STATE_IDLE;
						memset(vid_player.file.name, 0, sizeof(vid_player.file.name));
						memset(vid_player.file.directory, 0, sizeof(vid_player.file.directory));
						vid_player.file.index = 0;
						vid_player.media_current_pos = 0;
						vid_player.media_duration = 0;
						natural_eof_session_held = false;
						vid_player.seek_request_deferred = false;
						vid_player.seek_queued_pos_ms = 0;
						vid_player.seek_demux_target_ms = 0;
					}

					vid_player.sub_state = PLAYER_SUB_STATE_NONE;

					if(event == DECODE_THREAD_SHUTDOWN_REQUEST)
					{
						//Exit the threads.
						vid_player.thread_run = false;
						continue;
					}

					break;
				}

				case DECODE_THREAD_INCREASE_KEEP_RAM_REQUEST:
				{
					uint32_t previous_size = vid_player.ram_to_keep_base;
					uint32_t raw_image_size = 0;

					if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)//Hardware decoder only supports 1 track at a time.
						raw_image_size = HW_DECODER_RAW_IMAGE_SIZE;
					else
					{
						for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
							raw_image_size = Util_max(raw_image_size, SW_DECODER_RAW_IMAGE_SIZE(i));
					}

					//Increase RAM amount to keep to reduce the chance of out of memory loop.
					vid_player.ram_to_keep_base = (previous_size + raw_image_size);

					DEF_LOG_STRING("Out of memory has been detected!!!!!");
					DEF_LOG_FORMAT("Increasing RAM amount to keep (%.3fMB->%.3fMB)",
					(previous_size / 1000.0 / 1000.0), ((previous_size + raw_image_size) / 1000.0 / 1000.0));

					break;
				}
				default:
					break;
			}
		}

		result = Util_queue_get(&vid_player.decode_thread_notification_queue, (uint32_t*)&notification, NULL, 0);
		if(result == DEF_SUCCESS)
		{
			switch (notification)
			{
				case READ_PACKET_THREAD_FINISHED_READING_EOF_NOTIFICATION:
					is_eof = true;

				//Fall through.
				case READ_PACKET_THREAD_FINISHED_READING_NOTIFICATION:
				{
					is_read_packet_thread_active = false;
					break;
				}

				case READ_PACKET_THREAD_FINISHED_SEEKING_NOTIFICATION:
				{
					//Do nothing if player state is not prepare seeking.
					if(vid_player.state != PLAYER_STATE_PREPARE_SEEKING)
						break;

					if(vid_player.num_of_video_tracks == 0)
					{
						/* Audio-only (or no video): no frame threads — same as old loop with count 0. */
						wait_count = SEEK_IGNORE_PACKETS;
						backward_timeout = SEEK_BACKWARD_TIMEOUT;
						vid_player.state = PLAYER_STATE_SEEKING;
					}
					else
					{
						//If there are video tracks, clear the video cache first.
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_video_thread_command_queue, DECODE_VIDEO_THREAD_CLEAR_CACHE_REQUEST,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
					}

					break;
				}

				case DECODE_VIDEO_THREAD_FINISHED_COPYING_PACKET_NOTIFICATION:
				{
					is_waiting_video_decoder = false;
					break;
				}

				case DECODE_THREAD_FINISHED_BUFFERING_NOTIFICATION:
				case CONVERT_THREAD_FINISHED_BUFFERING_NOTIFICATION:
				{
					//Do nothing if player state is not buffering.
					if(vid_player.state != PLAYER_STATE_BUFFERING)
						break;

					if(vid_player.sub_state & PLAYER_SUB_STATE_RESUME_LATER)
					{
						//Remove resume later bit.
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_RESUME_LATER);

						//Resume the playback.
						Util_speaker_resume(DEF_VID_SPEAKER_SESSION_ID);
						vid_player.state = PLAYER_STATE_PLAYING;
					}
					else
					{
						//Don't resume.
						vid_player.state = PLAYER_STATE_PAUSE;
					}

					/* e.g. follow-up seek 入队失败时 seek_request_deferred 仍为 true：缓冲结束后再试一次 */
					vid_decode_finish_seek_wave_try_deferred();

					break;
				}

				case CONVERT_THREAD_OUT_OF_BUFFER_NOTIFICATION:
				{
					//Do nothing if player state is not playing nor pause.
					if(vid_player.state != PLAYER_STATE_PLAYING && vid_player.state != PLAYER_STATE_PAUSE)
						break;

					//Do nothing if we completely reached EOF.
					if(is_eof && Util_decoder_get_available_packet_num(DEF_VID_DECORDER_SESSION_ID) == 0)
						break;

					if(vid_player.state == PLAYER_STATE_PLAYING)
					{
						//Pause the playback.
						Util_speaker_pause(DEF_VID_SPEAKER_SESSION_ID);
						//Add resume later bit.
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_RESUME_LATER);
					}
					else//Remove resume later bit.
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_RESUME_LATER);

					vid_player.state = PLAYER_STATE_BUFFERING;

					break;
				}

				case CONVERT_THREAD_FINISHED_CLEARING_CACHE:
				{
					uint8_t num_of_threads = 0;

					//Do nothing if player state is not prepare seeking.
					if(vid_player.state != PLAYER_STATE_PREPARE_SEEKING)
						break;

					//All threads have stopped using the decoder 鈥?safe to reopen.
					{
						uint8_t num_audio = 0, num_video = 0;
						char full_path[MAX_PATH_LENGTH + MAX_FILE_NAME_LENGTH];
						bool use_hw      = (vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING) != 0;
						bool use_hw_conv = (vid_player.sub_state & PLAYER_SUB_STATE_HW_CONVERSION) != 0;

						if(use_hw_conv)
							Util_converter_y2r_exit();
						Util_converter_ffmpeg_cache_clear();

						snprintf(full_path, sizeof(full_path), "%s%s", vid_player.file.directory, vid_player.file.name);
						Util_decoder_close_file(DEF_VID_DECORDER_SESSION_ID);

						if(Util_decoder_open_file(full_path, &num_audio, &num_video, DEF_VID_DECORDER_SESSION_ID) == DEF_SUCCESS)
						{
							if(num_audio > 0)
								Util_decoder_audio_init(num_audio, DEF_VID_DECORDER_SESSION_ID);

							if(num_video > 0)
							{
								uint8_t req_threads = (vid_player.use_multi_threaded_decoding ? vid_player.num_of_threads : 1);
								Media_thread_type req_type = (vid_player.use_multi_threaded_decoding ? vid_player.thread_mode : MEDIA_THREAD_TYPE_NONE);
								num_video = (uint8_t)Util_min(num_video, EYE_MAX);
								Util_decoder_video_init(0, num_video, req_threads, req_type, DEF_VID_DECORDER_SESSION_ID);
								if(use_hw && !vid_decode_mvd_blocked_real_o3ds_fake_off())
								{
									uint32_t mvd_r = Util_decoder_mvd_init(DEF_VID_DECORDER_SESSION_ID);
									if(mvd_r != DEF_SUCCESS)
										vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_HW_DECODING);
								}
								else if(use_hw && vid_decode_mvd_blocked_real_o3ds_fake_off())
									vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_HW_DECODING);
								if(!(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING) && use_hw_conv)
									Util_converter_y2r_init();
							}

							//Re-seek to target position after reopen.
							Util_decoder_seek(vid_player.seek_demux_target_ms, MEDIA_SEEK_FLAG_BACKWARD, DEF_VID_DECORDER_SESSION_ID);
							Util_decoder_clear_cache_packet(DEF_VID_DECORDER_SESSION_ID);
						}
					}

					for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
						num_of_threads = Util_max(num_of_threads, ((vid_player.video_info[i].thread_type == MEDIA_THREAD_TYPE_FRAME) ? vid_player.num_of_threads : 0));

					//After clearing cache start seeking.
					wait_count = (SEEK_IGNORE_PACKETS + num_of_threads);
					backward_timeout = SEEK_BACKWARD_TIMEOUT;
					vid_player.state = PLAYER_STATE_SEEKING;

					break;
				}

				default:
					break;
			}
		}

		if(vid_player.state == PLAYER_STATE_PLAYING || vid_player.state == PLAYER_STATE_SEEKING
		|| vid_player.state == PLAYER_STATE_PAUSE || vid_player.state == PLAYER_STATE_BUFFERING)
		{
			bool key_frame = false;
			uint8_t packet_index = 0;
			uint8_t playing_ch = 0;
			uint16_t num_of_cached_packets = 0;
			uint16_t num_of_video_buffers = 0;
			uint32_t audio_buffers_size = 0;
			uint32_t required_free_ram = 0;
			double audio_buffer_health_ms = 0;
			Media_packet_type type = MEDIA_PACKET_TYPE_UNKNOWN;

			//Calculate how much free RAM we need and check buffer health.
			//To prevent out of memory on other tasks, make sure we have at least :
			//ram_to_keep_base + (raw image size * 2) for hardware decoding.
			//ram_to_keep_base + (raw image size * (1 + num_of_threads)) for software decoding.
			if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)//Hardware decoder only supports 1 track at a time.
			{
				required_free_ram = (vid_player.ram_to_keep_base + (HW_DECODER_RAW_IMAGE_SIZE * 2));
				num_of_video_buffers = Util_decoder_mvd_get_available_raw_image_num(DEF_VID_DECORDER_SESSION_ID);
			}
			else
			{
				uint8_t num_of_active_threads = 1;

				for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
					num_of_active_threads = Util_max(num_of_active_threads, ((vid_player.video_info[i].thread_type == MEDIA_THREAD_TYPE_FRAME) ? vid_player.num_of_threads : 1));

				for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
				{
					required_free_ram = Util_max(required_free_ram, (vid_player.ram_to_keep_base + (SW_DECODER_RAW_IMAGE_SIZE(i) * (num_of_active_threads + 1))));
					num_of_video_buffers = Util_max(num_of_video_buffers, Util_decoder_video_get_available_raw_image_num(i, DEF_VID_DECORDER_SESSION_ID));
				}
			}
			num_of_cached_packets = Util_decoder_get_available_packet_num(DEF_VID_DECORDER_SESSION_ID);
			audio_buffers_size = Util_speaker_get_available_buffer_size(DEF_VID_SPEAKER_SESSION_ID);

			//Audio buffer health (in ms) is ((buffer_size / bytes_per_sample / playing_ch / sample_rate) * 1000).
			//3DS only supports up to 2ch.
			playing_ch = (vid_player.audio_info[0].ch > 2 ? 2 : vid_player.audio_info[0].ch);
			audio_buffer_health_ms = DEF_UTIL_S_TO_MS_D(audio_buffers_size / 2.0 / playing_ch / vid_player.audio_info[0].sample_rate);

			//Update audio position.
			if(vid_player.num_of_audio_tracks > 0)
				vid_player.audio_current_pos = (vid_player.last_decoded_audio_pos - audio_buffer_health_ms);

			//Update current media position.
			vid_player.media_current_pos = Vid_get_current_media_pos(vid_player.video_current_pos[EYE_LEFT], vid_player.video_current_pos[EYE_RIGHT], vid_player.audio_current_pos);

			//If file does NOT have normal videos, update bar pos to see if the position has changed
			//so that we can update the screen when it gets changed.
			if(!Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime))
			{
				//We want to update screen every 100ms hence divide by 100.
				//(audio_bar_pos has been registerd in watch list.)
				audio_bar_pos = (uint32_t)(vid_player.media_current_pos / 100);
			}

			//If we only have half number of packets in buffer, we have not reached eof
			//and read packet thread is inactive, send a read packet request.
			if((num_of_cached_packets < (DEF_DECODER_MAX_CACHE_PACKETS / 2)) && !is_read_packet_thread_active && !is_eof
			&& !(natural_eof_session_held && vid_player.state == PLAYER_STATE_PAUSE))
			{
				is_read_packet_thread_active = true;

				//Start reading packets.
				DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.read_packet_thread_command_queue, READ_PACKET_THREAD_READ_PACKET_REQUEST,
				NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
			}

			if(num_of_cached_packets == 0 && is_eof)
			{
				//We consumed all of packets.
				bool is_audio_done = true;
				bool is_video_done = true;

				//Wait for audio playback.
				if(vid_player.num_of_audio_tracks > 0)
				{
					if(audio_buffers_size > 0)
						is_audio_done = false;
				}

				//Wait for video playback.
				if(Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime))
				{
					if(num_of_video_buffers > 0)
						is_video_done = false;
				}

				//Stop buffering if we completely reached EOF.
				if(vid_player.state == PLAYER_STATE_BUFFERING)
				{
					//Notify we've done buffering (can't buffer anymore).
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, DECODE_THREAD_FINISHED_BUFFERING_NOTIFICATION,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
					continue;
				}

				if(is_audio_done && is_video_done && !natural_eof_session_held)
				{
					//We've finished playing.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_PLAY_NEXT_REQUEST,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
					continue;
				}

				Util_sleep(10000);
				continue;
			}

		if(is_waiting_video_decoder || (num_of_video_buffers + 1 >= DEF_DECODER_MAX_RAW_IMAGE)
		|| Util_check_free_linear_space() < required_free_ram)
			{
				//If one of them is true, wait and try again later.
				//1. Video decoder thread has not completed copying the packet yet.
				//2. Video buffer is full.
				//3. Speaker buffer is full.
				//4. No enough free RAM.

				if(is_waiting_video_decoder)
				{
					//Wait for video decoder thread.
					Util_sleep(1000);
					continue;
				}
			else if(num_of_video_buffers > 0)
				{
					//We don't have enough free RAM but have cached raw pictures,
					//so wait frametime ms for them to get playbacked and freed.
					//If framerate is unknown, sleep 10ms.
					uint64_t sleep = 10000;

					for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
						sleep = Util_max(sleep, ((vid_player.video_info[i].framerate > 0) ? (1000000 / vid_player.video_info[i].framerate) : 10000));

					if(vid_player.state == PLAYER_STATE_BUFFERING)
					{
						//Notify we've done buffering (can't buffer anymore).
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, DECODE_THREAD_FINISHED_BUFFERING_NOTIFICATION,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
						continue;
					}

					Util_sleep(sleep);
					continue;
				}
				else
				{
					//If we don't have enough RAM and there are no cached raw pictures,
					//it is unlikely to increase amount of free RAM by waiting since
					//no video buffer get freed, so we have to try to decode it anyway.
				}
			}

			result = Util_decoder_parse_packet(&type, &packet_index, &key_frame, DEF_VID_DECORDER_SESSION_ID);
			if(result == DEF_ERR_TRY_AGAIN)//Packet is not ready, try again later.
			{
				Util_sleep(5000);
				continue;
			}
			else if(result != DEF_SUCCESS)//For other errors, log error detail.
			{
				DEF_LOG_RESULT(Util_decoder_parse_packet, false, result);
				Util_sleep(10000);
				continue;
			}

			//Handle seek request.
			if(vid_player.state == PLAYER_STATE_SEEKING && (type == MEDIA_PACKET_TYPE_VIDEO
			|| !Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime)))
			{
				if((vid_player.sub_state & PLAYER_SUB_STATE_SEEK_BACKWARD_WAIT))
				{
					bool is_behind = false;

					if(vid_player.media_current_pos == 0 || vid_player.media_current_pos < seek_start_pos)
						is_behind = true;

					//Make sure we went back.
					if(wait_count == 0 && (is_behind || !Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime)))
					{
						vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_SEEK_BACKWARD_WAIT);//Remove seek backward wait bit.

						if(vid_player.seek_start_pos_after_jump == VID_SEEK_JUMP_ANCHOR_UNSET)
							vid_player.seek_start_pos_after_jump = vid_player.media_current_pos;//We've jumped behing destination.
					}
				}
				else if(vid_player.seek_start_pos_after_jump == VID_SEEK_JUMP_ANCHOR_UNSET && wait_count == 0)
					vid_player.seek_start_pos_after_jump = vid_player.media_current_pos;//We've jumped.

				if(!(vid_player.sub_state & PLAYER_SUB_STATE_SEEK_BACKWARD_WAIT) && vid_player.media_current_pos >= vid_player.seek_demux_target_ms)
				{
					//Seek has finished.

					//Buffer some data before resuming playback if file contains normal videos.
					if(Vid_has_video(vid_player.num_of_video_tracks, vid_player.video_frametime))
						vid_player.state = PLAYER_STATE_BUFFERING;
					else
					{
						if(vid_player.sub_state & PLAYER_SUB_STATE_RESUME_LATER)
						{
							//Remove resume later bit.
							vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_RESUME_LATER);

							//Resume the playback.
							Util_speaker_resume(DEF_VID_SPEAKER_SESSION_ID);
							vid_player.state = PLAYER_STATE_PLAYING;
						}
						else
						{
							//Don't resume.
							vid_player.state = PLAYER_STATE_PAUSE;
						}
					}
					vid_decode_finish_seek_wave_try_deferred();
				}

				if(wait_count > 0)
					wait_count--;
				else if(backward_timeout > 0)
					backward_timeout--;
				else//Timeout, remove seek backward wait bit.
					vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_SEEK_BACKWARD_WAIT);
			}

			if(type == MEDIA_PACKET_TYPE_UNKNOWN)
			{
				DEF_LOG_STRING("Unknown packet type!!!!!");
				Util_sleep(10000);
				continue;
			}
			else if(type == MEDIA_PACKET_TYPE_AUDIO)
			{
				if(vid_player.num_of_audio_tracks > packet_index && packet_index == 0)
				{
					result = Util_decoder_ready_audio_packet(packet_index, DEF_VID_DECORDER_SESSION_ID);
					if(result == DEF_SUCCESS)
					{
					uint8_t* audio = NULL;
					uint32_t audio_samples = 0;
					double pos = 0;

						result = Util_decoder_audio_decode(&audio_samples, &audio, &pos, packet_index, DEF_VID_DECORDER_SESSION_ID);

					if(result == DEF_SUCCESS)
					{
						//When not seeking, hand decoded PCM off to the audio thread (convert+speaker happen there).
						if(vid_player.state != PLAYER_STATE_SEEKING)
						{
							Vid_audio_decoded_data* audio_data = (Vid_audio_decoded_data*)malloc(sizeof(Vid_audio_decoded_data));
							if(audio_data)
							{
								audio_data->audio = audio;
								audio_data->audio_samples = audio_samples;
								audio_data->pos = pos;
								audio_data->packet_index = packet_index;
								audio = NULL; //Ownership transferred.

								result = Util_queue_add(&vid_player.audio_decode_thread_command_queue,
								AUDIO_DECODE_THREAD_DECODE_REQUEST, audio_data, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE);
								if(result != DEF_SUCCESS)
								{
									DEF_LOG_RESULT(Util_queue_add, false, result);
									free(audio_data->audio);
									free(audio_data);
								}
							}
						}

					//last_decoded_audio_pos is now updated by Vid_audio_decode_thread
					//after audio is actually sent to the speaker (Step 7 A/V sync broadcast).
					}
					else if(result != DEF_SUCCESS)
						DEF_LOG_RESULT(Util_decoder_audio_decode, false, result);

					free(audio);
					audio = NULL;
					}
					else
						DEF_LOG_RESULT(Util_decoder_ready_audio_packet, false, result);
				}
				else//This packet is not what we are looking for now, just skip it.
					Util_decoder_skip_audio_packet(packet_index, DEF_VID_DECORDER_SESSION_ID);
			}
			else if(type == MEDIA_PACKET_TYPE_VIDEO)
			{
				if(vid_player.num_of_video_tracks > packet_index && (!(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
				|| ((vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING) && packet_index == 0)))
				{
					Vid_video_packet_data* packet_info = (Vid_video_packet_data*)malloc(sizeof(Vid_video_packet_data));

					if(packet_info)
					{
						packet_info->is_key_frame = key_frame;
						packet_info->packet_index = packet_index;

						is_waiting_video_decoder = true;
						//Decode the next frame.
						//Too noisy.
						// DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_video_thread_command_queue, DECODE_VIDEO_THREAD_DECODE_REQUEST,
						// packet_info, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

						result = Util_queue_add(&vid_player.decode_video_thread_command_queue, DECODE_VIDEO_THREAD_DECODE_REQUEST,
						packet_info, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE);
						if(result != DEF_SUCCESS)
							DEF_LOG_RESULT(Util_queue_add, false, result);
					}
					else
						Util_decoder_skip_video_packet(packet_index, DEF_VID_DECORDER_SESSION_ID);
				}
				else//This packet is not what we are looking for now, just skip it.
					Util_decoder_skip_video_packet(packet_index, DEF_VID_DECORDER_SESSION_ID);
			}
		}
		else
			Util_sleep(1000);
	}

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}

void Vid_read_packet_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	uint32_t result = DEF_ERR_OTHER;

	while (vid_player.thread_run)
	{
		Vid_command event = NONE_REQUEST;

		result = Util_queue_get(&vid_player.read_packet_thread_command_queue, (uint32_t*)&event, NULL, DEF_THREAD_ACTIVE_SLEEP_TIME);
		if(result == DEF_SUCCESS)
		{
			switch (event)
			{
				case READ_PACKET_THREAD_READ_PACKET_REQUEST:
				{
					//Do nothing if player state is idle or prepare playing.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING)
						break;

					while(true)
					{
						result = Util_decoder_read_packet(DEF_VID_DECORDER_SESSION_ID);
						if(result == DEF_SUCCESS)
						{
							if(Util_decoder_get_available_packet_num(DEF_VID_DECORDER_SESSION_ID) + 1 >= DEF_DECODER_MAX_CACHE_PACKETS)
							{
								//Notify we've done reading.
								DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, READ_PACKET_THREAD_FINISHED_READING_NOTIFICATION,
								NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

								break;
							}
						}
						if(result == DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS)
						{
							//Notify we've reached EOF.
							DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, READ_PACKET_THREAD_FINISHED_READING_EOF_NOTIFICATION,
							NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

							break;
						}
						else if(result != DEF_SUCCESS)
						{
							if(result == DEF_ERR_TRY_AGAIN)
							{
								//Wait and try again.
								Util_sleep(4000);
							}
							else
							{
								DEF_LOG_RESULT(Util_decoder_read_packet, false, result);
								//Notify we've done reading.
								DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, READ_PACKET_THREAD_FINISHED_READING_NOTIFICATION,
								NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

								break;
							}
						}

						//If we get seek or abort request while reading, break the loop.
						if(Util_queue_check_event_exist(&vid_player.read_packet_thread_command_queue, READ_PACKET_THREAD_SEEK_REQUEST)
						|| Util_queue_check_event_exist(&vid_player.read_packet_thread_command_queue, READ_PACKET_THREAD_ABORT_REQUEST))
						{
							//Notify we've done reading.
							DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, READ_PACKET_THREAD_FINISHED_READING_NOTIFICATION,
							NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

							break;
						}
					}

					break;
				}

				case READ_PACKET_THREAD_SEEK_REQUEST:
				{
					//Do nothing if player state is idle or prepare playing.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING)
						break;

					DEF_LOG_RESULT_SMART(result, Util_decoder_seek(vid_player.seek_demux_target_ms, MEDIA_SEEK_FLAG_BACKWARD,
					DEF_VID_DECORDER_SESSION_ID), (result == DEF_SUCCESS), result);
					Util_decoder_clear_cache_packet(DEF_VID_DECORDER_SESSION_ID);

					//Notify we've done seeking.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, READ_PACKET_THREAD_FINISHED_SEEKING_NOTIFICATION,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

					break;
				}

				case READ_PACKET_THREAD_ABORT_REQUEST:
				{
					//Flush the command queue.
					while(true)
					{
						result = Util_queue_get(&vid_player.read_packet_thread_command_queue, (uint32_t*)&event, NULL, 0);
						if(result != DEF_SUCCESS)
							break;
					}

					//Notify we've done aborting.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, READ_PACKET_THREAD_FINISHED_ABORTING_NOTIFICATION,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

					break;
				}

				default:
					break;
			}
		}

		if(vid_player.state == PLAYER_STATE_IDLE)
		{
			while (vid_player.thread_suspend)
				Util_sleep(DEF_THREAD_INACTIVE_SLEEP_TIME);
		}
	}

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}

void Vid_audio_decode_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	uint32_t result = DEF_ERR_OTHER;

	while (vid_player.thread_run)
	{
		Vid_command event = NONE_REQUEST;
		void* message = NULL;

		result = Util_queue_get(&vid_player.audio_decode_thread_command_queue,
		                        (uint32_t*)&event, &message, DEF_THREAD_ACTIVE_SLEEP_TIME);
		if(result == DEF_SUCCESS)
		{
			switch(event)
			{
				case AUDIO_DECODE_THREAD_DECODE_REQUEST:
				{
					Vid_audio_decoded_data* audio_data = (Vid_audio_decoded_data*)message;
					if(audio_data)
					{
						Converter_audio_parameters parameters = { 0, };
						parameters.converted = NULL;

						parameters.source = audio_data->audio;
						parameters.in_ch = vid_player.audio_info[audio_data->packet_index].ch;
						parameters.in_sample_rate = vid_player.audio_info[audio_data->packet_index].sample_rate;
						parameters.in_sample_format = vid_player.audio_info[audio_data->packet_index].sample_format;
						parameters.in_samples = audio_data->audio_samples;
						//3DS only supports up to 2ch.
						parameters.out_ch = (vid_player.audio_info[audio_data->packet_index].ch > 2 ? 2 : vid_player.audio_info[audio_data->packet_index].ch);
						parameters.out_sample_rate = vid_player.audio_info[audio_data->packet_index].sample_rate;
						parameters.out_sample_format = RAW_SAMPLE_S16;

						result = Util_converter_convert_audio(&parameters);
						if(result == DEF_SUCCESS)
						{
							bool too_big = false;

							//Change volume.
							if(vid_player.volume != 100)
							{
								for(uint32_t i = 0; i < (parameters.out_samples * parameters.out_ch * 2); i += 2)
								{
									if(*(int16_t*)(parameters.converted + i) * ((double)vid_player.volume / 100) > INT16_MAX)
									{
										*(int16_t*)(parameters.converted + i) = INT16_MAX;
										too_big = true;
									}
									else
										*(int16_t*)(parameters.converted + i) = *(int16_t*)(parameters.converted + i) * ((double)vid_player.volume / 100);
								}

								if(too_big)
									vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state | PLAYER_SUB_STATE_TOO_BIG);
								else
									vid_player.sub_state = (Vid_player_sub_state)(vid_player.sub_state & ~PLAYER_SUB_STATE_TOO_BIG);
							}

						//Add audio to speaker buffer, wait up to 250ms.
						for(uint8_t i = 0; i < 125; i++)
						{
							result = Util_speaker_add_buffer(DEF_VID_SPEAKER_SESSION_ID, parameters.converted,
							(parameters.out_samples * parameters.out_ch * 2));
							if(result != DEF_ERR_TRY_AGAIN)
								break;

							Util_sleep(2000);
						}

						//Broadcast: update audio position to reflect what is actually in the speaker queue.
						//decode_thread uses this to calculate audio_current_pos = last_decoded_audio_pos - speaker_buffer_health_ms.
						if(result == DEF_SUCCESS)
							vid_player.last_decoded_audio_pos = audio_data->pos;
						}
						else
							DEF_LOG_RESULT(Util_converter_convert_audio, false, result);

						free(parameters.converted);
						free(audio_data->audio);
						free(audio_data);
						parameters.converted = NULL;
					}
					break;
				}
			case AUDIO_DECODE_THREAD_SEEK_REQUEST:
			case AUDIO_DECODE_THREAD_ABORT_REQUEST:
			{
				//Drain all stale decode requests; free their data without touching the speaker.
				while(true)
				{
					Vid_command queued_event = NONE_REQUEST;
					void* queued_msg = NULL;

					if(Util_queue_get(&vid_player.audio_decode_thread_command_queue,
					(uint32_t*)&queued_event, &queued_msg, 0) != DEF_SUCCESS)
						break;

					if(queued_event == AUDIO_DECODE_THREAD_DECODE_REQUEST)
					{
						Vid_audio_decoded_data* d = (Vid_audio_decoded_data*)queued_msg;
						if(d)
						{
							free(d->audio);
							free(d);
						}
					}
					else
						free(queued_msg);
				}

				//For abort: notify decode_thread that the audio queue is clean.
				//For seek: fire-and-forget, decode_thread does not wait for this notification.
				if(event == AUDIO_DECODE_THREAD_ABORT_REQUEST)
				{
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue,
					AUDIO_DECODE_THREAD_FINISHED_ABORTING_NOTIFICATION, NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE),
					(result == DEF_SUCCESS), result);
				}
				break;
			}
			default:
				free(message);
				break;
			}
			message = NULL;
		}

		if(vid_player.state == PLAYER_STATE_IDLE)
		{
			while (vid_player.thread_suspend)
				Util_sleep(DEF_THREAD_INACTIVE_SLEEP_TIME);
		}
	}

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}

