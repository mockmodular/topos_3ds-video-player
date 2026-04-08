//Includes.
#include "video_player.h"
#include "vid_state.h"
#include "vid_texture.h"
#include "vid_screen.h"
#include "vid_sync.h"
#include "vid_worker.h"
#include "vid_draw.h"
#include "vid_seekbar.h"
#include "vid_seek_engine.h"

#include <inttypes.h>
#include <math.h>
#include <malloc.h>
#include <libavutil/cpu.h>

#include "system/draw/draw.h"
#include "system/menu.h"
#include "system/sem.h"
#include "system/util/converter.h"
#include "system/util/cpu_usage.h"
#include "system/util/err.h"
#include "system/util/hw_config.h"
#include "system/util/log.h"
#include "system/util/speaker.h"
#include "system/util/watch.h"
#include "vid_panel.h"


//Code.
void Vid_main(void)
{
	uint32_t back_color = DEF_DRAW_WHITE;
	uint64_t current_ts = osGetTime();
	uint8_t image_index[EYE_MAX] = { 0, };
	double image_width[EYE_MAX] = { 0, };
	double image_height[EYE_MAX] = { 0, };
	double video_x_offset[SCREEN_POS_MAX] = { 0, };
	double video_y_offset[SCREEN_POS_MAX] = { 0, };
	Draw_image_data background = Draw_get_empty_image();
	Watch_handle_bit watch_handle_bit = (DEF_WATCH_HANDLE_BIT_GLOBAL | DEF_WATCH_HANDLE_BIT_VIDEO_PLAYER);
	Sem_config config = { 0, };
	Sem_state state = { 0, };
	Vid_eye screen_pos_to_eye[SCREEN_POS_MAX] = { EYE_LEFT, EYE_RIGHT, EYE_LEFT, };

	Sem_get_config(&config);
	Sem_get_state(&state);

	if (config.is_night)
		back_color = DEF_DRAW_BLACK;

	//Assign previous frame index first.
	for(uint32_t i = 0; i < EYE_MAX; i++)
	{
		if(vid_player.next_draw_index[i] > 0)
			image_index[i] = (vid_player.next_draw_index[i] - 1);
		else
			image_index[i] = (VIDEO_BUFFERS - 1);
	}

	if(vid_player.state == PLAYER_STATE_PLAYING && vid_player.num_of_video_tracks > 0)
	{
		bool is_at_least_1_buffer_full = false;
		bool is_at_least_1_buffer_empty = false;
		uint8_t buffer_health[EYE_MAX] = { 0, };

		//Check for buffer health.
		for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
		{
			if(vid_player.next_draw_index[i] <= vid_player.next_store_index[i])
				buffer_health[i] = (vid_player.next_store_index[i] - vid_player.next_draw_index[i]);
			else
				buffer_health[i] = ((VIDEO_BUFFERS - vid_player.next_draw_index[i]) + vid_player.next_store_index[i]);

			if(!is_at_least_1_buffer_full)
				is_at_least_1_buffer_full = (buffer_health[i] >= (VIDEO_BUFFERS - 1));
			if(!is_at_least_1_buffer_empty)
				is_at_least_1_buffer_empty = (buffer_health[i] == 0);
		}

		for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
		{
			if(vid_player.next_frame_update_time[i] <= current_ts)
			{
				if(buffer_health[i] > 0)//We have buffer on current track.
				{
					if(!is_at_least_1_buffer_empty//We have all buffers, we can continue.
					|| is_at_least_1_buffer_full)//At least 1 buffer is full, we must continue or we'll stall.
					{
						bool wait = false;
						double next_ts = 0;
						double video_delay = 0;
						double wait_threshold = 0;
						double force_wait_threshold = 0;

						if(vid_player.num_of_audio_tracks > 0)
						{
							Util_sync_lock(&vid_player.delay_update_lock, UINT64_MAX);
							Vid_update_video_delay(i);
							video_delay = vid_player.video_delay_ms[i][DELAY_SAMPLES - 1];
							Util_sync_unlock(&vid_player.delay_update_lock);
						}
						else
							video_delay = 0;

						// A/V sync: when to hold video (wait for audio).
						wait_threshold = WAIT_THRESHOLD(vid_player.video_frametime[i]);
						force_wait_threshold = FORCE_WAIT_THRESHOLD(vid_player.video_frametime[i]);

						if(video_delay < wait_threshold)
						{
							if(vid_player.wait_threshold_exceeded_ts[i] == 0)
								vid_player.wait_threshold_exceeded_ts[i] = current_ts;

							if((current_ts - vid_player.wait_threshold_exceeded_ts[i]) > WAIT_THRESHOLD_ALLOWED_DURATION(vid_player.video_frametime[i]))
								wait = true;
						}
						else
							vid_player.wait_threshold_exceeded_ts[i] = 0;

						if(video_delay < force_wait_threshold)
							wait = true;

						if(wait && Util_speaker_get_available_buffer_num(DEF_VID_SPEAKER_SESSION_ID) > 0)
						{
							//Video is too fast, don't update a video frame to wait for audio.
						}
						else
						{
							//Update buffer index.
							image_index[i] = vid_player.next_draw_index[i];

							if((vid_player.next_draw_index[i] + 1) < VIDEO_BUFFERS)
								vid_player.next_draw_index[i]++;
							else
								vid_player.next_draw_index[i] = 0;

							//Mirror draw index for right eye: SBS 3D, or mono 2D (single track) so both eyes use the same frame.
							if((vid_player.is_sbs_3d || vid_player.num_of_video_tracks == 1) && i == EYE_LEFT)
							{
								image_index[EYE_RIGHT] = image_index[EYE_LEFT];
								vid_player.next_draw_index[EYE_RIGHT] = vid_player.next_draw_index[EYE_LEFT];
							}

							Draw_set_refresh_needed(true);
							vid_player.vps_cache[i]++;
							vid_player.last_video_frame_updated_ts[i] = current_ts;
						}

						//Update next frame update timestamp.
						next_ts = (vid_player.next_frame_update_time[i] + vid_player.video_frametime[i]);

						if(current_ts >= (next_ts + (vid_player.video_frametime[i] * 10)))
							vid_player.next_frame_update_time[i] = (current_ts + vid_player.video_frametime[i]);
						else
							vid_player.next_frame_update_time[i] = next_ts;
					}
					else
					{
						//We don't have all buffers, and we can still wait.
						for(uint32_t k = 0; k < EYE_MAX; k++)
							vid_player.next_frame_update_time[k] = (current_ts + vid_player.video_frametime[k]);
					}
				}
				else
				{
					// No decoded frame ready for this eye; skip draw update this tick.
				}
			}
		}
	}
	else
	{
		for(uint32_t i = 0; i < EYE_MAX; i++)
			vid_player.next_frame_update_time[i] = (current_ts + vid_player.video_frametime[i]);
	}

	//Update vps (video playback framerate).
	if(current_ts >= vid_player.next_vfps_update)
	{
		if(current_ts >= (vid_player.next_vfps_update + 1000))
			vid_player.next_vfps_update = (current_ts + 1000);
		else
			vid_player.next_vfps_update += 1000;

		for(uint32_t i = 0; i < EYE_MAX; i++)
		{
			vid_player.vps[i] = vid_player.vps_cache[i];
			vid_player.vps_cache[i] = 0;
		}
	}

	//Calculate image size and drawing position.
	for(uint32_t i = 0; i < EYE_MAX; i++)
	{
		double sar_width_ratio = 0;
		double sar_height_ratio = 0;

		if(i >= vid_player.num_of_video_tracks)
			break;

		if(vid_player.is_sbs_3d)
		{
			//SBS 3D hardcoded for 800x240: each half is 400x240, fills screen exactly.
			sar_width_ratio = 1;
			sar_height_ratio = 1;
		}
		else
		{
			sar_width_ratio = vid_player.video_info[i].sar_width;
			sar_height_ratio = vid_player.video_info[i].sar_height;
		}
		image_width[i] = vid_player.large_image[image_index[i]][i].image_width * sar_width_ratio * vid_player.video_zoom[i];
		image_height[i] = vid_player.large_image[image_index[i]][i].image_height * sar_height_ratio * vid_player.video_zoom[i];
	}

	//In SBS 3D mode, right eye uses same display dimensions as left eye.
	if(vid_player.is_sbs_3d)
	{
		image_width[EYE_RIGHT] = image_width[EYE_LEFT];
		image_height[EYE_RIGHT] = image_height[EYE_LEFT];
	}
	else if(vid_player.num_of_video_tracks == 1)
	{
		//Mono 2D: only one texture (EYE_LEFT); duplicate dimensions for right-eye pass.
		image_width[EYE_RIGHT] = image_width[EYE_LEFT];
		image_height[EYE_RIGHT] = image_height[EYE_LEFT];
	}

	for(uint32_t i = 0; i < SCREEN_POS_MAX; i++)
	{
		video_x_offset[i] = vid_player.video_x_offset[screen_pos_to_eye[i]];
		video_y_offset[i] = vid_player.video_y_offset[screen_pos_to_eye[i]];
	}


	//+--------------------------------------------------------------------------------+
	//| An Alternative ASCII GNU (https://www.gnu.org/graphics/alternative-ascii.html) |
	//| Copyright (C) 2003 Vijay Kumar                                                 |
	//|                                                                                |
	//| License: GNU GPL v2.0-or-later                                                 |
	//| SPDX-License-Identifier: GPL-2.0-or-later                                      |
	//+--------------------------------------------------------------------------------+
	//Apply bottom screen offset.
	// (0, 0) ---------------------------------------->(-40, 0)--------------------------------------->(-40, -240)
	// +-------------------------------------------+   +-------------------------------------------+   +-------------------------------------------+
	// |  +-------------------------------------+  |   |  +-------------------------------------+  |   |  +-------------------------------------+  |
	// |  |     _-`````-,           ,- '- .     |  |   |  |     _-`````-,           ,- '- .     |  |   |  |     _-`````-,           ,- '- .     |  |
	// |  |   .'   .- - |          | - -.  `.   |  |   |  |   .'   .- - |          | - -.  `.   |  |   |  |   .'   .- - |          | - -.  `.   |  |
	// |  |  /.'  /                     `.   \  |  |   |  |  /.'  /                     `.   \  |  |   |  |  /.'  /                     `.   \  |  |
	// |  | :/   :      _...   ..._      ``   : |  |   |  | :/   :      _...   ..._      ``   : |  |   |  | :/   :      _...   ..._      ``   : |  |
	// |  | ::   :     /._ .`:'_.._\.    ||   : |  |   |  | ::   :     /._ .`:'_.._\.    ||   : |  |   |  | ::   :     /._ .`:'_.._\.    ||   : |  |
	// |  | ::    `._ ./  ,`  :    \ . _.''   . |  |   |  | ::    `._ ./  ,`  :    \ . _.''   . |  |   |  | ::    `._ ./  ,`  :    \ . _.''   . |  |
	// |  +-------------------------------------+  |   |  +-------------------------------------+  |   |  +-------------------------------------+  |
	// |                                           |   |     <-- 40px                              |   |                                           |
	// |     +-------------------------------+     |   |     +-------------------------------+     |   |  ^  +-------------------------------+     |
	// |     |     _-`````-,           ,- '- |     |   |     |  _-`````-,           ,- '- .  |     |   |  |  |.      /   |  -.  \-. \\_      |     |
	// |     |   .'   .- - |          | - -. |     |   |     |.'   .- - |          | - -.  `.|     |   |  |  |\:._ _/  .'   .@)  \@) ` `\ ,.'|     |
	// |     |  /.'  /                     `.|     |   |     |.'  /                     `.   |     |   | 240 |   _/,--'       .- .\,-.`--`.  |     |
	// |     | :/   :      _...   ..._      `|     |   |     |   :      _...   ..._      ``  |     |   |  px |     ,'/''     (( \ `  )       |     |
	// |     | ::   :     /._ .`:'_.._\.    ||     |   |     |   :     /._ .`:'_.._\.    ||  |     |   |     |      /'/'  \    `-'  (        |     |
	// |     | ::    `._ ./  ,`  :    \ . _.'|     |   |     |    `._ ./  ,`  :    \ . _.''  |     |   |     |       '/''  `._,-----'        |     |
	// |     +-------------------------------+     |   |     +-------------------------------+     |   |     +-------------------------------+     |
	// +-------------------------------------------+   +-------------------------------------------+   +-------------------------------------------+
	video_x_offset[SCREEN_POS_BOTTOM] -= 40;
	video_y_offset[SCREEN_POS_BOTTOM] -= 240;

	Vid_update_decoding_statistics_every_100ms();

	if(Util_err_query_show_flag())
		watch_handle_bit |= DEF_WATCH_HANDLE_BIT_ERR;
	/* 屏上日志已禁用（原 Util_log_query_show_flag） */
	/* if(Util_log_query_show_flag())
		watch_handle_bit |= DEF_WATCH_HANDLE_BIT_LOG; */

	Vid_control_full_screen();

	if(Util_watch_is_changed(watch_handle_bit) || Draw_is_refresh_needed() || !config.is_eco)
	{
		Draw_set_refresh_needed(false);
		Draw_frame_ready();

		if(config.is_top_lcd_on)
		{
			Str_data top_center_msg = { 0, };
			Str_data bottom_left_msg = { 0, };
			Str_data bottom_center_msg = { 0, };

			Util_str_init(&top_center_msg);
			Util_str_init(&bottom_left_msg);
			Util_str_init(&bottom_center_msg);

			Draw_screen_ready(DRAW_SCREEN_TOP_LEFT, vid_player.is_full_screen ? DEF_DRAW_BLACK : back_color);

			if(vid_player.state != PLAYER_STATE_IDLE)
			{
				//Draw videos.
				if(Util_sync_lock(&vid_player.texture_init_free_lock, 0) == DEF_SUCCESS)
				{
					{ Vid_eye left_src = vid_player.sbs_swap_eyes ? EYE_RIGHT : EYE_LEFT;
					Vid_large_texture_draw(&vid_player.large_image[image_index[EYE_LEFT]][left_src], video_x_offset[SCREEN_POS_TOP_LEFT], video_y_offset[SCREEN_POS_TOP_LEFT], image_width[EYE_LEFT], image_height[EYE_LEFT]); }
					Util_sync_unlock(&vid_player.texture_init_free_lock);
				}

			}

			if(Util_str_has_data(&top_center_msg))
			{
				Draw_with_background(&top_center_msg, 0, 20, 0.45, 0.45, DEF_DRAW_WHITE, DRAW_X_ALIGN_CENTER, DRAW_Y_ALIGN_CENTER,
				400, 30, DRAW_BACKGROUND_UNDER_TEXT, &background, 0xA0000000);
			}

			if(Util_str_has_data(&bottom_left_msg))
			{
				Draw_with_background(&bottom_left_msg, 0, 200, 0.45, 0.45, DEF_DRAW_WHITE, DRAW_X_ALIGN_LEFT, DRAW_Y_ALIGN_BOTTOM,
				400, 40, DRAW_BACKGROUND_UNDER_TEXT, &background, 0xA0000000);
			}

			if(Util_str_has_data(&bottom_center_msg))
			{
				Draw_with_background(&bottom_center_msg, 0, 200, 0.5, 0.5, DEF_DRAW_WHITE, DRAW_X_ALIGN_CENTER, DRAW_Y_ALIGN_BOTTOM,
				400, 40, DRAW_BACKGROUND_UNDER_TEXT, &background, 0xA0000000);
			}

			/* Debug 模式 UI 已移除 */
			/* if(config.is_debug)
				Draw_debug_info(config.is_night, state.free_ram, state.free_linear_ram); */

			/* if(Util_log_query_show_flag())
				Util_log_draw(); */

			if(Draw_is_3d_mode())
			{
				Draw_screen_ready(DRAW_SCREEN_TOP_RIGHT, vid_player.is_full_screen ? DEF_DRAW_BLACK : back_color);


				if(vid_player.state != PLAYER_STATE_IDLE)
				{
					//Draw right eye: SBS uses split textures; mono 2D duplicates left eye (same tex + position).
					if(Util_sync_lock(&vid_player.texture_init_free_lock, 0) == DEF_SUCCESS)
					{
						if(vid_player.is_sbs_3d)
						{
							Vid_eye right_src = vid_player.sbs_swap_eyes ? EYE_LEFT : EYE_RIGHT;
							Vid_large_texture_draw(&vid_player.large_image[image_index[EYE_RIGHT]][right_src], video_x_offset[SCREEN_POS_TOP_RIGHT], video_y_offset[SCREEN_POS_TOP_RIGHT], image_width[EYE_RIGHT], image_height[EYE_RIGHT]);
						}
						else if(vid_player.num_of_video_tracks == 1)
							Vid_large_texture_draw(&vid_player.large_image[image_index[EYE_LEFT]][EYE_LEFT], video_x_offset[SCREEN_POS_TOP_LEFT], video_y_offset[SCREEN_POS_TOP_LEFT], image_width[EYE_LEFT], image_height[EYE_LEFT]);
						else
						{
							Vid_eye right_src = vid_player.sbs_swap_eyes ? EYE_LEFT : EYE_RIGHT;
							Vid_large_texture_draw(&vid_player.large_image[image_index[EYE_RIGHT]][right_src], video_x_offset[SCREEN_POS_TOP_RIGHT], video_y_offset[SCREEN_POS_TOP_RIGHT], image_width[EYE_RIGHT], image_height[EYE_RIGHT]);
						}
						Util_sync_unlock(&vid_player.texture_init_free_lock);
					}

				}

				if(Util_str_has_data(&top_center_msg))
				{
					Draw_with_background(&top_center_msg, 0, 20, 0.45, 0.45, DEF_DRAW_WHITE, DRAW_X_ALIGN_CENTER, DRAW_Y_ALIGN_CENTER,
					400, 30, DRAW_BACKGROUND_UNDER_TEXT, &background, 0xA0000000);
				}

				if(Util_str_has_data(&bottom_left_msg))
				{
					Draw_with_background(&bottom_left_msg, 0, 200, 0.45, 0.45, DEF_DRAW_WHITE, DRAW_X_ALIGN_LEFT, DRAW_Y_ALIGN_BOTTOM,
					400, 40, DRAW_BACKGROUND_UNDER_TEXT, &background, 0xA0000000);
				}

				if(Util_str_has_data(&bottom_center_msg))
				{
					Draw_with_background(&bottom_center_msg, 0, 200, 0.5, 0.5, DEF_DRAW_WHITE, DRAW_X_ALIGN_CENTER, DRAW_Y_ALIGN_BOTTOM,
					400, 40, DRAW_BACKGROUND_UNDER_TEXT, &background, 0xA0000000);
				}

				/* if(config.is_debug)
					Draw_debug_info(config.is_night, state.free_ram, state.free_linear_ram); */

				/* if(Util_log_query_show_flag())
					Util_log_draw(); */
			}

			Util_str_free(&top_center_msg);
			Util_str_free(&bottom_left_msg);
			Util_str_free(&bottom_center_msg);
		}

		Vid_panel_draw_bottom_screen(
			config.is_bottom_lcd_on,
			back_color,
			Util_err_query_show_flag());

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();

	if(vid_player.is_setting_volume)
		vid_player.is_setting_volume = false;
	else if(vid_player.is_setting_seek_duration)
		vid_player.is_setting_seek_duration = false;

	if(!aptIsSleepAllowed())
		aptSetSleepAllowed(true);
}

void Vid_draw_init_exit_message(void)
{
	/* Init/exit wait: both LCDs solid black, no text or overlays. */
	Draw_set_refresh_needed(false);
	Draw_frame_ready();

	Draw_screen_ready(DRAW_SCREEN_TOP_LEFT, DEF_DRAW_BLACK);
	if (Draw_is_3d_mode())
		Draw_screen_ready(DRAW_SCREEN_TOP_RIGHT, DEF_DRAW_BLACK);
	Draw_screen_ready(DRAW_SCREEN_BOTTOM, DEF_DRAW_BLACK);

	Draw_apply_draw();
}
