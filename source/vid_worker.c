//Includes.
#include "vid_worker.h"

extern void memcpy_asm(uint8_t*, uint8_t*, uint32_t);

#include "vid_texture.h"
#include "vid_sync.h"
#include "vid_screen.h"
#include "video_player.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "system/util/converter.h"
#include "system/util/decoder.h"
#include "system/util/err.h"
#include "system/util/log.h"
#include "system/util/speaker.h"
#include "system/util/str.h"
#include "system/util/util.h"

// RGBA8 Morton x-delta table (pixel_size=4, period 4 per 8 px).
static const uint16_t s_mvd_ilx[4] = {16, 48, 16, 176};

// MVD SBS: one-pass alpha fix (A=0→0xFF) + row-split + Morton tiling for both eyes.
// half_w = visible pixels per eye; full_width = source stride in pixels (codec-padded row width).
static void vid_sbs_morton_at_half(const uint8_t* mvd, uint32_t full_width, uint32_t half_w, uint32_t height,
                                C3D_Tex* l_tex, C3D_Tex* r_tex)
{
	uint16_t tex_w  = (uint16_t)l_tex->width;
	uint16_t ily[8] = {8, 24, 8, 88, 8, 24, 8, 0};
	ily[7] = (uint16_t)((uint32_t)tex_w * 32u - 168u);

	uint8_t* l_dst      = (uint8_t*)l_tex->data;
	uint8_t* r_dst      = (uint8_t*)r_tex->data;
	uint32_t c3d_offset = 0;
	uint8_t  cnt_y      = 0;

	for(uint32_t y = 0; y < height; y++)
	{
		const uint32_t* l_src = (const uint32_t*)(mvd + y * full_width * 4);
		const uint32_t* r_src = l_src + half_w;
		uint32_t c3d_pos = 0;
		uint8_t  cnt_x   = 0;

		for(uint32_t x = 0; x < half_w; x += 2)
		{
			uint32_t off = c3d_pos + c3d_offset;
			// ABGR32: byte[0]=A(alpha), byte[1]=B, byte[2]=G, byte[3]=R.
			// In little-endian uint32: byte[0] is LSB → mask 0x000000FF sets alpha=0xFF.
			*(uint32_t*)(l_dst + off)     = l_src[x]     | 0x000000FFu;
			*(uint32_t*)(l_dst + off + 4) = l_src[x + 1] | 0x000000FFu;
			*(uint32_t*)(r_dst + off)     = r_src[x]     | 0x000000FFu;
			*(uint32_t*)(r_dst + off + 4) = r_src[x + 1] | 0x000000FFu;
			c3d_pos += s_mvd_ilx[cnt_x & 3];
			cnt_x++;
		}
		c3d_offset += ily[cnt_y & 7];
		cnt_y++;
	}
}

// ---- Unroll4: 4x unrolled Morton + source prefetch ----
// 8 pixels per iteration; offsets within a 256-byte group are fixed constants.
// Morton row-0 intra-group byte offsets: {0,4, 16,20, 64,68, 80,84}.
// c3d_offset encodes the y-in-tile contribution; group_base = c3d_offset + group_idx*256.

static void vid_sbs_morton_opt_at_half(const uint8_t* mvd, uint32_t full_width, uint32_t half_w, uint32_t height,
                                    C3D_Tex* l_tex, C3D_Tex* r_tex)
{
	uint16_t tex_w  = (uint16_t)l_tex->width;
	uint16_t ily[8] = {8, 24, 8, 88, 8, 24, 8, 0};
	ily[7] = (uint16_t)((uint32_t)tex_w * 32u - 168u);

	uint8_t* l_dst      = (uint8_t*)l_tex->data;
	uint8_t* r_dst      = (uint8_t*)r_tex->data;
	uint32_t c3d_offset = 0;
	uint8_t  cnt_y      = 0;

	for(uint32_t y = 0; y < height; y++)
	{
		const uint32_t* l_src = (const uint32_t*)(mvd + y * full_width * 4);
		const uint32_t* r_src = l_src + half_w;
		uint32_t gb = c3d_offset;

		for(uint32_t x = 0; x < half_w; x += 8, gb += 256)
		{
			__builtin_prefetch(&l_src[x + 16], 0, 0);
			__builtin_prefetch(&r_src[x + 16], 0, 0);
			uint32_t ls0 = l_src[x]   | 0xFFu;
			uint32_t ls1 = l_src[x+1] | 0xFFu;
			uint32_t ls2 = l_src[x+2] | 0xFFu;
			uint32_t ls3 = l_src[x+3] | 0xFFu;
			uint32_t ls4 = l_src[x+4] | 0xFFu;
			uint32_t ls5 = l_src[x+5] | 0xFFu;
			uint32_t ls6 = l_src[x+6] | 0xFFu;
			uint32_t ls7 = l_src[x+7] | 0xFFu;
			*(uint32_t*)(l_dst + gb +  0) = ls0;
			*(uint32_t*)(l_dst + gb +  4) = ls1;
			*(uint32_t*)(l_dst + gb + 16) = ls2;
			*(uint32_t*)(l_dst + gb + 20) = ls3;
			*(uint32_t*)(l_dst + gb + 64) = ls4;
			*(uint32_t*)(l_dst + gb + 68) = ls5;
			*(uint32_t*)(l_dst + gb + 80) = ls6;
			*(uint32_t*)(l_dst + gb + 84) = ls7;
			uint32_t rs0 = r_src[x]   | 0xFFu;
			uint32_t rs1 = r_src[x+1] | 0xFFu;
			uint32_t rs2 = r_src[x+2] | 0xFFu;
			uint32_t rs3 = r_src[x+3] | 0xFFu;
			uint32_t rs4 = r_src[x+4] | 0xFFu;
			uint32_t rs5 = r_src[x+5] | 0xFFu;
			uint32_t rs6 = r_src[x+6] | 0xFFu;
			uint32_t rs7 = r_src[x+7] | 0xFFu;
			*(uint32_t*)(r_dst + gb +  0) = rs0;
			*(uint32_t*)(r_dst + gb +  4) = rs1;
			*(uint32_t*)(r_dst + gb + 16) = rs2;
			*(uint32_t*)(r_dst + gb + 20) = rs3;
			*(uint32_t*)(r_dst + gb + 64) = rs4;
			*(uint32_t*)(r_dst + gb + 68) = rs5;
			*(uint32_t*)(r_dst + gb + 80) = rs6;
			*(uint32_t*)(r_dst + gb + 84) = rs7;
		}
		c3d_offset += ily[cnt_y & 7];
		cnt_y++;
	}
}

static void Vid_mvd_sbs_morton_opt(const uint8_t* mvd, uint32_t full_width, uint32_t height, C3D_Tex* l_tex, C3D_Tex* r_tex)
{
	uint32_t half_w = full_width / 2;
	uint16_t tex_w  = (uint16_t)l_tex->width;
	uint16_t ily[8] = {8, 24, 8, 88, 8, 24, 8, 0};
	ily[7] = (uint16_t)((uint32_t)tex_w * 32u - 168u);

	uint8_t* l_dst      = (uint8_t*)l_tex->data;
	uint8_t* r_dst      = (uint8_t*)r_tex->data;
	uint32_t c3d_offset = 0;
	uint8_t  cnt_y      = 0;

	for(uint32_t y = 0; y < height; y++)
	{
		const uint32_t* l_src = (const uint32_t*)(mvd + y * full_width * 4);
		const uint32_t* r_src = l_src + half_w;
		uint32_t gb = c3d_offset;

		for(uint32_t x = 0; x < half_w; x += 8, gb += 256)
		{
			__builtin_prefetch(&l_src[x + 16], 0, 0);
			__builtin_prefetch(&r_src[x + 16], 0, 0);
			uint32_t ls0 = l_src[x]   | 0xFFu;
			uint32_t ls1 = l_src[x+1] | 0xFFu;
			uint32_t ls2 = l_src[x+2] | 0xFFu;
			uint32_t ls3 = l_src[x+3] | 0xFFu;
			uint32_t ls4 = l_src[x+4] | 0xFFu;
			uint32_t ls5 = l_src[x+5] | 0xFFu;
			uint32_t ls6 = l_src[x+6] | 0xFFu;
			uint32_t ls7 = l_src[x+7] | 0xFFu;
			*(uint32_t*)(l_dst + gb +  0) = ls0;
			*(uint32_t*)(l_dst + gb +  4) = ls1;
			*(uint32_t*)(l_dst + gb + 16) = ls2;
			*(uint32_t*)(l_dst + gb + 20) = ls3;
			*(uint32_t*)(l_dst + gb + 64) = ls4;
			*(uint32_t*)(l_dst + gb + 68) = ls5;
			*(uint32_t*)(l_dst + gb + 80) = ls6;
			*(uint32_t*)(l_dst + gb + 84) = ls7;
			uint32_t rs0 = r_src[x]   | 0xFFu;
			uint32_t rs1 = r_src[x+1] | 0xFFu;
			uint32_t rs2 = r_src[x+2] | 0xFFu;
			uint32_t rs3 = r_src[x+3] | 0xFFu;
			uint32_t rs4 = r_src[x+4] | 0xFFu;
			uint32_t rs5 = r_src[x+5] | 0xFFu;
			uint32_t rs6 = r_src[x+6] | 0xFFu;
			uint32_t rs7 = r_src[x+7] | 0xFFu;
			*(uint32_t*)(r_dst + gb +  0) = rs0;
			*(uint32_t*)(r_dst + gb +  4) = rs1;
			*(uint32_t*)(r_dst + gb + 16) = rs2;
			*(uint32_t*)(r_dst + gb + 20) = rs3;
			*(uint32_t*)(r_dst + gb + 64) = rs4;
			*(uint32_t*)(r_dst + gb + 68) = rs5;
			*(uint32_t*)(r_dst + gb + 80) = rs6;
			*(uint32_t*)(r_dst + gb + 84) = rs7;
		}
		c3d_offset += ily[cnt_y & 7];
		cnt_y++;
	}
}

static void Vid_mvd_2d_morton_opt(const uint8_t* mvd, uint32_t width, uint32_t height, C3D_Tex* tex)
{
	uint16_t tex_w = (uint16_t)tex->width;
	uint16_t ily[8] = {8, 24, 8, 88, 8, 24, 8, 0};
	ily[7] = (uint16_t)((uint32_t)tex_w * 32u - 168u);

	uint8_t* dst        = (uint8_t*)tex->data;
	uint32_t c3d_offset = 0;
	uint8_t  cnt_y      = 0;

	for(uint32_t y = 0; y < height; y++)
	{
		const uint32_t* src = (const uint32_t*)(mvd + y * width * 4);
		uint32_t gb = c3d_offset;

		for(uint32_t x = 0; x < width; x += 8, gb += 256)
		{
			__builtin_prefetch(&src[x + 16], 0, 0);
			uint32_t s0 = src[x]   | 0xFFu;
			uint32_t s1 = src[x+1] | 0xFFu;
			uint32_t s2 = src[x+2] | 0xFFu;
			uint32_t s3 = src[x+3] | 0xFFu;
			uint32_t s4 = src[x+4] | 0xFFu;
			uint32_t s5 = src[x+5] | 0xFFu;
			uint32_t s6 = src[x+6] | 0xFFu;
			uint32_t s7 = src[x+7] | 0xFFu;
			*(uint32_t*)(dst + gb +  0) = s0;
			*(uint32_t*)(dst + gb +  4) = s1;
			*(uint32_t*)(dst + gb + 16) = s2;
			*(uint32_t*)(dst + gb + 20) = s3;
			*(uint32_t*)(dst + gb + 64) = s4;
			*(uint32_t*)(dst + gb + 68) = s5;
			*(uint32_t*)(dst + gb + 80) = s6;
			*(uint32_t*)(dst + gb + 84) = s7;
		}
		c3d_offset += ily[cnt_y & 7];
		cnt_y++;
	}
}

static bool vid_tex_auto_wants_nearest(uint32_t vw, uint32_t vh, bool is_sbs_3d)
{
	if(vw == 0 || vh == 0)
		return false;
	/* 2D (incl. “3d or 2d” auto → 2D): NEAREST if (y=240,x<=400) or (x=400,y<=240); else LINEAR */
	if(!is_sbs_3d)
		return (vh == 240u && vw <= 400u) || (vw == 400u && vh <= 240u);
	/* 3D: NEAREST if (y=240,x<=800) or (x=800,y<=240); else LINEAR */
	return (vh == 240u && vw <= 800u) || (vw == 800u && vh <= 240u);
}

bool Vid_effective_use_linear_texture_filter(uint32_t eye_k)
{
	if(Vid_pixel_perfect_forces_nearest(eye_k))
		return false;

	uint32_t w, h;

	Vid_video_presentation_wh(EYE_LEFT, &w, &h);

	switch(vid_player.texture_filter_mode)
	{
	case VID_TEX_FILTER_NEAREST:
		return false;
	case VID_TEX_FILTER_BILINEAR:
		return true;
	case VID_TEX_FILTER_AUTO:
	default:
		return !vid_tex_auto_wants_nearest(w, h, vid_player.is_sbs_3d);
	}
}

void Vid_update_decoding_statistics_every_100ms(void)
{
	u64 current_ts = osGetTime();

	//Update performance data every 100ms.
	if(current_ts >= (vid_player.previous_ts + 100))
	{
		vid_player.previous_ts = current_ts;

		//Calc buffering progress.
		if(vid_player.state == PLAYER_STATE_BUFFERING)
		{
			uint16_t available_buffer = 0;
			uint16_t raw_l = (vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
				? Util_decoder_mvd_get_available_raw_image_num(DEF_VID_DECORDER_SESSION_ID)
				: Util_decoder_video_get_available_raw_image_num(0, DEF_VID_DECORDER_SESSION_ID);
			uint16_t raw_r = Util_decoder_video_get_available_raw_image_num(1, DEF_VID_DECORDER_SESSION_ID);

			available_buffer = Util_max(raw_l, raw_r);

			if(available_buffer >= VID_FIXED_RESTART_PLAYBACK_THRESHOLD)
				vid_player.buffer_progress = 100;//Done.
			else if(available_buffer == 0)
				vid_player.buffer_progress = 0;
			else
				vid_player.buffer_progress = (((double)available_buffer / VID_FIXED_RESTART_PLAYBACK_THRESHOLD) * 100);
		}
		else
			vid_player.buffer_progress = 0;//Not applicable.
	}
}

double Vid_get_media_duration(double video_track_0_duration, double video_track_1_duration, double audio_track_duration)
{
	//Use the longest duration as duration for this file.
	return Util_max_d(Util_max_d(video_track_0_duration, video_track_1_duration), audio_track_duration);
}

double Vid_get_current_media_pos(double video_track_0_pos, double video_track_1_pos, double audio_track_pos)
{
	//Use the most advanced position as current position.
	return Util_max_d(Util_max_d(video_track_0_pos, video_track_1_pos), audio_track_pos);
}

bool Vid_has_video(uint8_t num_of_video_tracks, double video_frametimes[EYE_MAX])
{
	if(num_of_video_tracks == 0)
		return false;//We don't have any videos.
	else
	{
		for(uint8_t i = 0; i < num_of_video_tracks; i++)
		{
			//We don't treat it "video" if its frametime is unknown (that's usually tagged mp3 with only 1 image).
			if(video_frametimes[i] != 0)
				return true;
		}

		return false;
	}
}

void Vid_log_media_info(void)
{
	//Video.
	DEF_LOG_STRING(" ");//New line.
	DEF_LOG_STRING("Video:");
	DEF_LOG_UINT(vid_player.num_of_video_tracks);
	for(uint32_t i = 0; i < Util_min(vid_player.num_of_video_tracks, EYE_MAX); i++)
	{
		Str_data time = { 0, };

		Util_convert_seconds_to_time(vid_player.video_info[i].duration, &time);

		DEF_LOG_STRING(" ");//New line.
		DEF_LOG_FORMAT("Video[%" PRIu32 "]:", i);
		DEF_LOG_DOUBLE(vid_player.video_frametime[i]);
		DEF_LOG_DOUBLE(vid_player.video_x_offset[i]);
		DEF_LOG_DOUBLE(vid_player.video_y_offset[i]);
		DEF_LOG_DOUBLE(vid_player.video_zoom[i]);
		DEF_LOG_UINT(vid_player.video_info[i].width);
		DEF_LOG_UINT(vid_player.video_info[i].height);
		DEF_LOG_UINT(vid_player.video_info[i].codec_width);
		DEF_LOG_UINT(vid_player.video_info[i].codec_height);
		DEF_LOG_DOUBLE(vid_player.video_info[i].framerate);
		DEF_LOG_STRING(vid_player.video_info[i].format_name);
		DEF_LOG_STRING(vid_player.video_info[i].short_format_name);
		DEF_LOG_STRING(DEF_STR_NEVER_NULL(&time));
		DEF_LOG_STRING(Media_thread_type_get_name(vid_player.video_info[i].thread_type));
		DEF_LOG_DOUBLE(vid_player.video_info[i].sar_width);
		DEF_LOG_DOUBLE(vid_player.video_info[i].sar_height);
		DEF_LOG_STRING(Raw_pixel_get_name(vid_player.video_info[i].pixel_format));

		Util_str_free(&time);
	}

	//Audio.
	DEF_LOG_STRING(" ");//New line.
	DEF_LOG_STRING("Audio:");
	DEF_LOG_UINT(vid_player.num_of_audio_tracks);
	for(uint8_t i = 0; i < Util_min(vid_player.num_of_audio_tracks, DEF_DECODER_MAX_AUDIO_TRACKS); i++)
	{
		Str_data time = { 0, };

		Util_convert_seconds_to_time(vid_player.audio_info[i].duration, &time);

		DEF_LOG_STRING(" ");//New line.
		DEF_LOG_FORMAT("Audio[%" PRIu32 "]:", i);
		DEF_LOG_UINT(vid_player.audio_info[i].bitrate);
		DEF_LOG_UINT(vid_player.audio_info[i].sample_rate);
		DEF_LOG_UINT(vid_player.audio_info[i].ch);
		DEF_LOG_STRING(DEF_STR_NEVER_NULL(&time));
		DEF_LOG_STRING(vid_player.audio_info[i].format_name);
		DEF_LOG_STRING(vid_player.audio_info[i].short_format_name);
		DEF_LOG_STRING(vid_player.audio_info[i].track_lang);
		DEF_LOG_STRING(Raw_sample_get_name(vid_player.audio_info[i].sample_format));

		Util_str_free(&time);
	}


	DEF_LOG_STRING(" ");//New line.
}

void frame_worker_thread_start(const void* frame_handle)
{
	uint8_t index = UINT8_MAX;

	if(!frame_handle)
		return;

	Util_sync_lock(&vid_player.delay_update_lock, UINT64_MAX);
	//Search for stopwatch index using frame handle.
	for(uint8_t i = 0; i < DEBUG_GRAPH_TEMP_ELEMENTS; i++)
	{
		if(vid_player.frame_list[i] == frame_handle)
		{
			index = i;
			break;
		}
	}

	//Not registerd, find free space and register.
	if(index == UINT8_MAX)
	{
		for(uint8_t i = 0; i < DEBUG_GRAPH_TEMP_ELEMENTS; i++)
		{
			if(!vid_player.frame_list[i])
			{
				index = i;
				vid_player.frame_list[i] = frame_handle;
				break;
			}
		}
	}
	Util_sync_unlock(&vid_player.delay_update_lock);

	//No free spaces were found.
	if(index == UINT8_MAX)
		return;
}

void frame_worker_thread_end(const void* frame_handle)
{
	if(!frame_handle)
		return;

	Util_sync_lock(&vid_player.delay_update_lock, UINT64_MAX);
	for(uint8_t i = 0; i < DEBUG_GRAPH_TEMP_ELEMENTS; i++)
	{
		if(vid_player.frame_list[i] == frame_handle)
		{
			vid_player.frame_list[i] = NULL;
			break;
		}
	}
	Util_sync_unlock(&vid_player.delay_update_lock);
}

void dav1d_worker_task_start(const void* frame_handle)
{
	frame_worker_thread_start(frame_handle);
}

void dav1d_worker_task_end(const void* frame_handle)
{
	frame_worker_thread_end(frame_handle);
}

void Vid_decode_video_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	uint32_t result = DEF_ERR_OTHER;
	double skip = 0;
	TickCounter counter = { 0, };

	osTickCounterStart(&counter);

	while (vid_player.thread_run)
	{
		void* message = NULL;
		Vid_command event = NONE_REQUEST;

		if(vid_player.state == PLAYER_STATE_IDLE)
		{
			while (vid_player.thread_suspend)
				Util_sleep(DEF_THREAD_INACTIVE_SLEEP_TIME);
		}

		result = Util_queue_get(&vid_player.decode_video_thread_command_queue, (uint32_t*)&event, &message, DEF_THREAD_ACTIVE_SLEEP_TIME);
		if(result == DEF_SUCCESS)
		{
			switch (event)
			{
				case DECODE_VIDEO_THREAD_DECODE_REQUEST:
				{
					uint8_t packet_index = 0;
					Vid_video_packet_data* packet_info = (Vid_video_packet_data*)message;

					//Do nothing if player state is idle or prepare playing or message is NULL.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING || !packet_info)
						break;

					packet_index = packet_info->packet_index;

					free(packet_info);
					packet_info = NULL;

					(void)skip;
					{
						result = Util_decoder_ready_video_packet(packet_index, DEF_VID_DECORDER_SESSION_ID);

						//Notify we've done copying packet to video decoder buffer
						//so that decode thread can read the next packet.
						//Too noisy.
						// DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, DECODE_VIDEO_THREAD_FINISHED_COPYING_PACKET_NOTIFICATION,
						// NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
						result = Util_queue_add(&vid_player.decode_thread_notification_queue, DECODE_VIDEO_THREAD_FINISHED_COPYING_PACKET_NOTIFICATION,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE);
						if(result != DEF_SUCCESS)
							DEF_LOG_RESULT(Util_queue_add, false, result);

						if(result == DEF_SUCCESS)
						{
							while(true)
							{
								osTickCounterUpdate(&counter);

								if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
									result = Util_decoder_mvd_decode(DEF_VID_DECORDER_SESSION_ID);
								else
									result = Util_decoder_video_decode(packet_index, DEF_VID_DECORDER_SESSION_ID);

								osTickCounterUpdate(&counter);

								if(result == DEF_ERR_DECODER_TRY_AGAIN || result == DEF_ERR_DECODER_TRY_AGAIN_NO_OUTPUT
								|| result == DEF_ERR_TRY_AGAIN)
								{
									if(result == DEF_ERR_DECODER_TRY_AGAIN)//Got a frame.
									{
										if(vid_player.video_info[packet_index].thread_type != MEDIA_THREAD_TYPE_FRAME
										|| (vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING))
										{
											double time = osTickCounterRead(&counter);

											Vid_update_decoding_delay(time, &skip, packet_index);
										}

									}
									else if(result == DEF_ERR_TRY_AGAIN)//Buffer is full.
									{
										if(vid_player.video_frametime[packet_index] == 0)
											Util_sleep(10000);
										else
											Util_sleep(DEF_UTIL_MS_TO_US(vid_player.video_frametime[packet_index]));

										//If we get clear cache or abort request while waiting, break the loop.
										if(Util_queue_check_event_exist(&vid_player.decode_video_thread_command_queue, DECODE_VIDEO_THREAD_CLEAR_CACHE_REQUEST)
										|| Util_queue_check_event_exist(&vid_player.decode_video_thread_command_queue, DECODE_VIDEO_THREAD_ABORT_REQUEST))
											break;
									}

									continue;
								}
								else
									break;
							}

							if(result == DEF_SUCCESS)
							{
								if(vid_player.video_info[packet_index].thread_type != MEDIA_THREAD_TYPE_FRAME
								|| (vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING))
								{
									double time = osTickCounterRead(&counter);

									Vid_update_decoding_delay(time, &skip, packet_index);
								}
							}
							else if(result != DEF_ERR_NEED_MORE_INPUT)
							{
								if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
									DEF_LOG_RESULT(Util_decoder_mvd_decode, false, result);
								else
									DEF_LOG_RESULT(Util_decoder_video_decode, false, result);
							}
						}
						else
						{
							Util_decoder_skip_video_packet(packet_index, DEF_VID_DECORDER_SESSION_ID);
							DEF_LOG_RESULT(Util_decoder_ready_video_packet, false, result);
						}

						if(result == DEF_ERR_OUT_OF_MEMORY || result == DEF_ERR_OUT_OF_LINEAR_MEMORY)
						{
							//Request to increase amount of RAM to keep.
							DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_INCREASE_KEEP_RAM_REQUEST,
							NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
						}
					}

					break;
				}

				case DECODE_VIDEO_THREAD_CLEAR_CACHE_REQUEST:
				{
					//Do nothing if player state is idle or prepare playing.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING)
						break;

					//Flush the decoder.
					while(true)
					{
						if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
							result = Util_decoder_mvd_decode(DEF_VID_DECORDER_SESSION_ID);
						else
						{
							for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
								result = Util_decoder_video_decode(i, DEF_VID_DECORDER_SESSION_ID);
						}

						if(result != DEF_SUCCESS && result != DEF_ERR_DECODER_TRY_AGAIN_NO_OUTPUT && result != DEF_ERR_DECODER_TRY_AGAIN)
							break;
					}

					//Request the same to convert thread.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.convert_thread_command_queue, CONVERT_THREAD_CLEAR_CACHE_REQUEST,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

					break;
				}

				case DECODE_VIDEO_THREAD_ABORT_REQUEST:
				{
					skip = 0;

					//Clear cache.
					if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
						Util_decoder_mvd_clear_raw_image(DEF_VID_DECORDER_SESSION_ID);
					else
					{
						for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
							Util_decoder_video_clear_raw_image(i, DEF_VID_DECORDER_SESSION_ID);
					}

					//Flush the command queue.
					while(true)
					{
						result = Util_queue_get(&vid_player.decode_video_thread_command_queue, (uint32_t*)&event, NULL, 0);
						if(result != DEF_SUCCESS)
							break;
					}

					//Notify we've done aborting.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, DECODE_VIDEO_THREAD_FINISHED_ABORTING_NOTIFICATION,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

					break;
				}

				default:
					break;
			}
		}
	}

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}

void Vid_convert_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	bool should_convert = false;
	uint8_t packet_index = 0;
	uint32_t result = DEF_ERR_OTHER;

	while (vid_player.thread_run)
	{
		uint16_t num_of_cached_raw_images = 0;
		uint64_t timeout_us = DEF_THREAD_ACTIVE_SLEEP_TIME;
		Vid_command event = NONE_REQUEST;

		if(vid_player.state == PLAYER_STATE_IDLE)
		{
			while (vid_player.thread_suspend)
				Util_sleep(DEF_THREAD_INACTIVE_SLEEP_TIME);
		}

		//If player state is playing or seeking, don't wait for commands.
		if((vid_player.state == PLAYER_STATE_PLAYING || vid_player.state == PLAYER_STATE_SEEKING)
		&& should_convert)
			timeout_us = 0;

		result = Util_queue_get(&vid_player.convert_thread_command_queue, (uint32_t*)&event, NULL, timeout_us);
		if(result == DEF_SUCCESS)
		{
			switch (event)
			{
				case CONVERT_THREAD_CONVERT_REQUEST:
				{
					//Do nothing if player state is idle or prepare playing or file doesn't have video tracks.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING
					|| vid_player.num_of_video_tracks <= 0)
						break;

					should_convert = true;
					packet_index = 0;

					break;
				}

				case CONVERT_THREAD_CLEAR_CACHE_REQUEST:
				{
					//Do nothing if player state is idle or prepare playing or file doesn't have video tracks.
					if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING
					|| vid_player.num_of_video_tracks <= 0)
						break;

					//Clear cache.
					if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
						Util_decoder_mvd_clear_raw_image(DEF_VID_DECORDER_SESSION_ID);
					else
					{
						for(uint8_t i = 0; i < vid_player.num_of_video_tracks; i++)
							Util_decoder_video_clear_raw_image(i, DEF_VID_DECORDER_SESSION_ID);
					}

					//Notify we've done clearing cache.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, CONVERT_THREAD_FINISHED_CLEARING_CACHE,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

					break;
				}

				case CONVERT_THREAD_ABORT_REQUEST:
				{
					should_convert = false;
					packet_index = 0;

					//Flush the command queue.
					while(true)
					{
						result = Util_queue_get(&vid_player.convert_thread_command_queue, (uint32_t*)&event, NULL, 0);
						if(result != DEF_SUCCESS)
							break;
					}

					//Notify we've done aborting.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, CONVERT_THREAD_FINISHED_ABORTING_NOTIFICATION,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

					break;
				}

				default:
					break;
			}
		}

		//Do nothing if player state is idle, prepare playing, prepare seeking or should_convert flag is not set.
		if(vid_player.state == PLAYER_STATE_IDLE || vid_player.state == PLAYER_STATE_PREPARE_PLAYING
		|| vid_player.state == PLAYER_STATE_PREPARE_SEEKING || !should_convert)
			continue;

		//Check if we have cached raw images.
		if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
			num_of_cached_raw_images = Util_decoder_mvd_get_available_raw_image_num(DEF_VID_DECORDER_SESSION_ID);
		else
			num_of_cached_raw_images = Util_decoder_video_get_available_raw_image_num(packet_index, DEF_VID_DECORDER_SESSION_ID);

		/* Seeking: drain raw decode queue via skip (not adaptive A/V drop). */
		if(vid_player.state == PLAYER_STATE_SEEKING && vid_player.video_frametime[packet_index] != 0)
		{
			if(num_of_cached_raw_images > 0)
			{
				double pos = 0;

				if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
				{
					//Hardware decoder can't decode 2 tracks at the same time.
					Util_decoder_mvd_skip_image(&pos, DEF_VID_DECORDER_SESSION_ID);
					vid_player.video_current_pos[EYE_LEFT] = pos;
				}
				else
				{
					Util_decoder_video_skip_image(&pos, packet_index, DEF_VID_DECORDER_SESSION_ID);
					vid_player.video_current_pos[packet_index] = pos;
				}
			}
			else
			{
				if(vid_player.state == PLAYER_STATE_SEEKING)
					Util_sleep(3000);

				if(num_of_cached_raw_images == 0 && vid_player.video_frametime[packet_index] != 0 && Util_speaker_get_available_buffer_num(DEF_VID_SPEAKER_SESSION_ID) == 0)
				{
					//Notify we've run out of buffer.
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, CONVERT_THREAD_OUT_OF_BUFFER_NOTIFICATION,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
					Util_sleep(3000);
				}
			}

			//Update current media position.
			vid_player.media_current_pos = Vid_get_current_media_pos(vid_player.video_current_pos[EYE_LEFT], vid_player.video_current_pos[EYE_RIGHT], vid_player.audio_current_pos);

			if((packet_index + 1) < vid_player.num_of_video_tracks)
				packet_index++;
			else
				packet_index = 0;
		}
		else if(vid_player.state == PLAYER_STATE_PLAYING)
		{
			if(num_of_cached_raw_images == 0 && vid_player.video_frametime[packet_index] != 0 && Util_speaker_get_available_buffer_num(DEF_VID_SPEAKER_SESSION_ID) == 0)
			{
				//Notify we've run out of buffer.
				DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, CONVERT_THREAD_OUT_OF_BUFFER_NOTIFICATION,
				NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
				Util_sleep(5000);
				continue;
			}
			else if(num_of_cached_raw_images == 0)
			{
				//We've run out of buffer, but frametime is unknown, this is most likely tagged mp3
				//that has only one picture, so don't send out of buffer notification.
				Util_sleep(5000);
				continue;
			}
			else
			{
				uint8_t buffer_health = 0;
				uint8_t next_store_index = vid_player.next_store_index[packet_index];
				uint8_t next_draw_index = vid_player.next_draw_index[packet_index];
				uint8_t* yuv_video = NULL;
				uint8_t* video = NULL;
				uint32_t width = vid_player.video_info[packet_index].codec_width;
				uint32_t height = vid_player.video_info[packet_index].codec_height;
				double pos = 0;

				if(next_draw_index <= next_store_index)
					buffer_health = (next_store_index - next_draw_index);
				else
					buffer_health = ((VIDEO_BUFFERS - next_draw_index) + next_store_index);

				if((buffer_health + 1) >= VIDEO_BUFFERS)
				{
					//Buffer is full.
					if(vid_player.video_frametime[packet_index] == 0)
						Util_sleep(10000);
					else
						Util_sleep(DEF_UTIL_MS_TO_US(vid_player.video_frametime[packet_index]));

					continue;
				}

				if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)//Hardware decoder only supports 1 track at a time.
					result = Util_decoder_mvd_get_image(&video, &pos, width, height, DEF_VID_DECORDER_SESSION_ID);
				else
					result = Util_decoder_video_get_image(&yuv_video, &pos, width, height, packet_index, DEF_VID_DECORDER_SESSION_ID);

				if(result == DEF_SUCCESS)
				{
					//Update video position.
					vid_player.video_current_pos[packet_index] = (pos - (vid_player.video_frametime[packet_index] * buffer_health));

					//Update current media position.
					vid_player.media_current_pos = Vid_get_current_media_pos(vid_player.video_current_pos[EYE_LEFT], vid_player.video_current_pos[EYE_RIGHT], vid_player.audio_current_pos);

			if(!(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING))
				{
					if(vid_player.video_info[packet_index].is_av1_codec)
					{
						/* AV1 dedicated path: always use software color converter (swscale). */
						Converter_color_parameters color_converter_parameters = { 0, };
						color_converter_parameters.source = yuv_video;
						color_converter_parameters.converted = NULL;
						color_converter_parameters.in_width = width;
						color_converter_parameters.in_height = height;
						color_converter_parameters.in_color_format = vid_player.video_info[packet_index].pixel_format;
						color_converter_parameters.out_width = width;
						color_converter_parameters.out_height = height;
						color_converter_parameters.out_color_format = RAW_PIXEL_ABGR8888;
						result = Util_converter_convert_color(&color_converter_parameters);
						video = color_converter_parameters.converted;
					}
					else
					{
					if(vid_player.sub_state & PLAYER_SUB_STATE_HW_CONVERSION)//Use hardware color converter.
					{
					if(vid_player.is_sbs_3d && vid_player.sbs_right_buf)
					{
					if(vid_player.use_hw_color_conversion == VID_HW_CONV_Y2R_X2)
					{
						C3D_Tex* sbs_l_tex = vid_player.large_image[next_store_index][EYE_LEFT].images[0].c2d.tex;
						C3D_Tex* sbs_r_tex = vid_player.large_image[next_store_index][EYE_RIGHT].images[0].c2d.tex;
						result = Util_converter_y2r_yuv420p_sbs_to_rgba8888(yuv_video, (uint8_t*)sbs_l_tex->data, (uint8_t*)sbs_r_tex->data, (uint16_t)sbs_l_tex->width, width, height);
						}
							else
								result = Util_converter_y2r_yuv420p_to_rgba8888(yuv_video, &video, width, height, true);
						}
				else
				{
					if(vid_player.use_hw_color_conversion == VID_HW_CONV_Y2R_X2)
					{
						Draw_image_data* di2d = &vid_player.large_image[next_store_index][packet_index].images[0];
						result = Util_converter_y2r_yuv420p_to_rgba8888_direct(yuv_video, (uint8_t*)di2d->c2d.tex->data, (uint16_t)di2d->c2d.tex->width, width, height);
					}
					else
						result = Util_converter_y2r_yuv420p_to_rgba8888(yuv_video, &video, width, height, true);
				}
					}
					else//Use software color converter.
					{
						Converter_color_parameters color_converter_parameters = { 0, };
						color_converter_parameters.source = yuv_video;
						color_converter_parameters.converted = NULL;
						color_converter_parameters.in_width = width;
						color_converter_parameters.in_height = height;
						color_converter_parameters.in_color_format = vid_player.video_info[packet_index].pixel_format;
						color_converter_parameters.out_width = width;
						color_converter_parameters.out_height = height;
						color_converter_parameters.out_color_format = RAW_PIXEL_ABGR8888;
						result = Util_converter_convert_color(&color_converter_parameters);
						video = color_converter_parameters.converted;
					}
					}
				}

					//Set texture data.
					if(result == DEF_SUCCESS)
					{
						uint8_t image_index = vid_player.next_store_index[packet_index];

						//We don't need to copy padding area for Y direction.
						height = vid_player.video_info[packet_index].height;

				if(vid_player.is_sbs_3d)
				{
					if(vid_player.video_info[packet_index].is_av1_codec)
					{
						/* AV1 SBS dedicated path: pixel-exact split at visible_width/2. */
						if(vid_player.sbs_right_buf)
						{
							uint32_t vis_half = vid_player.video_info[packet_index].width / 2u;
							C3D_Tex* sbs_l = vid_player.large_image[image_index][EYE_LEFT].images[0].c2d.tex;
							C3D_Tex* sbs_r = vid_player.large_image[image_index][EYE_RIGHT].images[0].c2d.tex;
							if((vis_half % 8u) == 0u)
								vid_sbs_morton_opt_at_half(video, width, vis_half, height, sbs_l, sbs_r);
							else
								vid_sbs_morton_at_half(video, width, vis_half, height, sbs_l, sbs_r);
							C3D_TexFlush(sbs_l);
							C3D_TexFlush(sbs_r);
							Draw_image_data* li_ll = &vid_player.large_image[image_index][EYE_LEFT].images[0];
							Draw_image_data* li_rr = &vid_player.large_image[image_index][EYE_RIGHT].images[0];
							li_ll->subtex->width  = (uint16_t)vis_half;
							li_ll->subtex->height = (uint16_t)height;
							li_ll->subtex->left   = 0.0f;
							li_ll->subtex->top    = 1.0f;
							li_ll->subtex->right  = (float)vis_half / (float)sbs_l->width;
							li_ll->subtex->bottom = 1.0f - (float)height / (float)sbs_l->height;
							li_ll->c2d.subtex     = li_ll->subtex;
							li_rr->subtex->width  = (uint16_t)vis_half;
							li_rr->subtex->height = (uint16_t)height;
							li_rr->subtex->left   = 0.0f;
							li_rr->subtex->top    = 1.0f;
							li_rr->subtex->right  = (float)vis_half / (float)sbs_r->width;
							li_rr->subtex->bottom = 1.0f - (float)height / (float)sbs_r->height;
							li_rr->c2d.subtex     = li_rr->subtex;
							vid_player.large_image[image_index][EYE_LEFT].image_width   = vis_half;
							vid_player.large_image[image_index][EYE_LEFT].image_height  = height;
							vid_player.large_image[image_index][EYE_RIGHT].image_width  = vis_half;
							vid_player.large_image[image_index][EYE_RIGHT].image_height = height;
							Vid_large_texture_crop(&vid_player.large_image[image_index][EYE_LEFT], vis_half, height);
							Vid_large_texture_crop(&vid_player.large_image[image_index][EYE_RIGHT], vis_half, height);
							result = DEF_SUCCESS;
						}
					}
					else
					{
					if(vid_player.sbs_right_buf)
					{
					uint32_t half_w = width / 2;
					bool is_y2r = ((vid_player.sub_state & PLAYER_SUB_STATE_HW_CONVERSION) != 0);
					if(is_y2r)
					{
						if(vid_player.use_hw_color_conversion == VID_HW_CONV_Y2R_X2)
						{
							C3D_Tex* l_tex = vid_player.large_image[image_index][EYE_LEFT].images[0].c2d.tex;
							C3D_Tex* r_tex = vid_player.large_image[image_index][EYE_RIGHT].images[0].c2d.tex;
							C3D_TexFlush(l_tex);
							C3D_TexFlush(r_tex);
							Draw_image_data* li_l = &vid_player.large_image[image_index][EYE_LEFT].images[0];
							Draw_image_data* li_r = &vid_player.large_image[image_index][EYE_RIGHT].images[0];
							li_l->subtex->width  = (uint16_t)half_w;
							li_l->subtex->height = (uint16_t)height;
							li_l->subtex->left   = 0.0f;
							li_l->subtex->top    = 1.0f;
							li_l->subtex->right  = (float)half_w / (float)l_tex->width;
							li_l->subtex->bottom = 1.0f - (float)height / (float)l_tex->height;
							li_l->c2d.subtex     = li_l->subtex;
							li_r->subtex->width  = (uint16_t)half_w;
							li_r->subtex->height = (uint16_t)height;
							li_r->subtex->left   = 0.0f;
							li_r->subtex->top    = 1.0f;
							li_r->subtex->right  = (float)half_w / (float)r_tex->width;
							li_r->subtex->bottom = 1.0f - (float)height / (float)r_tex->height;
							li_r->c2d.subtex     = li_r->subtex;
							vid_player.large_image[image_index][EYE_LEFT].image_width   = half_w;
							vid_player.large_image[image_index][EYE_LEFT].image_height  = height;
							vid_player.large_image[image_index][EYE_RIGHT].image_width  = half_w;
							vid_player.large_image[image_index][EYE_RIGHT].image_height = height;
							result = DEF_SUCCESS;
						}
						else
						{
							C3D_Tex* l_tex = vid_player.large_image[image_index][EYE_LEFT].images[0].c2d.tex;
							C3D_Tex* r_tex = vid_player.large_image[image_index][EYE_RIGHT].images[0].c2d.tex;
							uint32_t half_tile_row  = (half_w / 8u) * 256u;
							uint32_t full_tile_row  = (width  / 8u) * 256u;
							uint32_t l_tex_tile_row = ((uint32_t)l_tex->width / 8u) * 256u;
							uint32_t r_tex_tile_row = ((uint32_t)r_tex->width / 8u) * 256u;
							uint32_t tile_rows      = (uint32_t)height / 8u;
							for(uint32_t tr = 0; tr < tile_rows; tr++)
							{
								memcpy_asm((uint8_t*)l_tex->data + tr * l_tex_tile_row, video + tr * full_tile_row,                  half_tile_row);
								memcpy_asm((uint8_t*)r_tex->data + tr * r_tex_tile_row, video + tr * full_tile_row + half_tile_row,  half_tile_row);
							}
							C3D_TexFlush(l_tex);
							C3D_TexFlush(r_tex);
							Draw_image_data* li_l = &vid_player.large_image[image_index][EYE_LEFT].images[0];
							Draw_image_data* li_r = &vid_player.large_image[image_index][EYE_RIGHT].images[0];
							li_l->subtex->width  = (uint16_t)half_w;
							li_l->subtex->height = (uint16_t)height;
							li_l->subtex->left   = 0.0f;
							li_l->subtex->top    = 1.0f;
							li_l->subtex->right  = (float)half_w / (float)l_tex->width;
							li_l->subtex->bottom = 1.0f - (float)height / (float)l_tex->height;
							li_l->c2d.subtex     = li_l->subtex;
							li_r->subtex->width  = (uint16_t)half_w;
							li_r->subtex->height = (uint16_t)height;
							li_r->subtex->left   = 0.0f;
							li_r->subtex->top    = 1.0f;
							li_r->subtex->right  = (float)half_w / (float)r_tex->width;
							li_r->subtex->bottom = 1.0f - (float)height / (float)r_tex->height;
							li_r->c2d.subtex     = li_r->subtex;
							vid_player.large_image[image_index][EYE_LEFT].image_width   = half_w;
							vid_player.large_image[image_index][EYE_LEFT].image_height  = height;
							vid_player.large_image[image_index][EYE_RIGHT].image_width  = half_w;
							vid_player.large_image[image_index][EYE_RIGHT].image_height = height;
							result = DEF_SUCCESS;
						}
					}
					else
					{
						C3D_Tex* sbs_l = vid_player.large_image[image_index][EYE_LEFT].images[0].c2d.tex;
						C3D_Tex* sbs_r = vid_player.large_image[image_index][EYE_RIGHT].images[0].c2d.tex;
						Vid_mvd_sbs_morton_opt(video, width, height, sbs_l, sbs_r);
						C3D_TexFlush(sbs_l);
						C3D_TexFlush(sbs_r);
						Draw_image_data* li_ll = &vid_player.large_image[image_index][EYE_LEFT].images[0];
						Draw_image_data* li_rr = &vid_player.large_image[image_index][EYE_RIGHT].images[0];
						li_ll->subtex->width  = (uint16_t)half_w;
						li_ll->subtex->height = (uint16_t)height;
						li_ll->subtex->left   = 0.0f;
						li_ll->subtex->top    = 1.0f;
						li_ll->subtex->right  = (float)half_w / (float)sbs_l->width;
						li_ll->subtex->bottom = 1.0f - (float)height / (float)sbs_l->height;
						li_ll->c2d.subtex     = li_ll->subtex;
						li_rr->subtex->width  = (uint16_t)half_w;
						li_rr->subtex->height = (uint16_t)height;
						li_rr->subtex->left   = 0.0f;
						li_rr->subtex->top    = 1.0f;
						li_rr->subtex->right  = (float)half_w / (float)sbs_r->width;
						li_rr->subtex->bottom = 1.0f - (float)height / (float)sbs_r->height;
						li_rr->c2d.subtex     = li_rr->subtex;
						vid_player.large_image[image_index][EYE_LEFT].image_width   = half_w;
						vid_player.large_image[image_index][EYE_LEFT].image_height  = height;
						vid_player.large_image[image_index][EYE_RIGHT].image_width  = half_w;
						vid_player.large_image[image_index][EYE_RIGHT].image_height = height;
						Vid_large_texture_crop(&vid_player.large_image[image_index][EYE_LEFT], half_w, height);
						Vid_large_texture_crop(&vid_player.large_image[image_index][EYE_RIGHT], half_w, height);
						result = DEF_SUCCESS;
					}
					}
					}
				}
					else
					{
					if(vid_player.video_info[packet_index].is_av1_codec)
					{
						/* AV1 2D dedicated path: software Morton tiling + crop to visible size. */
						result = Vid_large_texture_set_data(&vid_player.large_image[image_index][packet_index], video, width, height, false);
						if(result != DEF_SUCCESS)
							DEF_LOG_RESULT(Vid_large_texture_set_data, false, result);
						Vid_large_texture_crop(&vid_player.large_image[image_index][packet_index], vid_player.video_info[packet_index].width, vid_player.video_info[packet_index].height);
					}
					else
					{
				if(!(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING) && (vid_player.sub_state & PLAYER_SUB_STATE_HW_CONVERSION))
				{
					if(vid_player.use_hw_color_conversion == VID_HW_CONV_Y2R_X2)
					{
						//y2r*2 2D: Y2R already wrote directly to texture; flush and update subtex.
						Draw_image_data* di2d = &vid_player.large_image[image_index][packet_index].images[0];
						C3D_TexFlush(di2d->c2d.tex);
						di2d->subtex->width  = (uint16_t)width;
						di2d->subtex->height = (uint16_t)height;
						di2d->subtex->left   = 0.0f;
						di2d->subtex->top    = 1.0f;
						di2d->subtex->right  = (float)width  / (float)di2d->c2d.tex->width;
						di2d->subtex->bottom = 1.0f - (float)height / (float)di2d->c2d.tex->height;
						di2d->c2d.subtex     = di2d->subtex;
						vid_player.large_image[image_index][packet_index].image_width  = width;
						vid_player.large_image[image_index][packet_index].image_height = height;
						result = DEF_SUCCESS;
					}
					else
						//NEONy2r 2D: video buf holds full frame; use standard 30-iter tile-row copy.
						result = Vid_large_texture_set_data(&vid_player.large_image[image_index][packet_index], video, width, height, true);
				}
			else if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
			{
			//MVD 2D: fixed Unroll4-optimized Morton path.
			Draw_image_data* di_mvd = &vid_player.large_image[image_index][packet_index].images[0];
			Vid_mvd_2d_morton_opt(video, width, height, di_mvd->c2d.tex);
			C3D_TexFlush(di_mvd->c2d.tex);
				di_mvd->subtex->width  = (uint16_t)width;
				di_mvd->subtex->height = (uint16_t)height;
				di_mvd->subtex->left   = 0.0f;
				di_mvd->subtex->top    = 1.0f;
				di_mvd->subtex->right  = (float)width  / (float)di_mvd->c2d.tex->width;
				di_mvd->subtex->bottom = 1.0f - (float)height / (float)di_mvd->c2d.tex->height;
				di_mvd->c2d.subtex     = di_mvd->subtex;
				vid_player.large_image[image_index][packet_index].image_width  = width;
				vid_player.large_image[image_index][packet_index].image_height = height;
				result = DEF_SUCCESS;
			}
			else//ABGR8888 non-MVD: CPU Morton tiling.
				result = Vid_large_texture_set_data(&vid_player.large_image[image_index][packet_index], video, width, height, false);

						if(result != DEF_SUCCESS)
							DEF_LOG_RESULT(Vid_large_texture_set_data, false, result);

						//Crop the image so that user won't see glitch on videos.
						Vid_large_texture_crop(&vid_player.large_image[image_index][packet_index], vid_player.video_info[packet_index].width, vid_player.video_info[packet_index].height);
					}
					}
					}
					else
						DEF_LOG_RESULT(Util_converter_convert_color, false, result);
				}
				else
				{
					if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
						DEF_LOG_RESULT(Util_decoder_mvd_get_image, false, result);
					else
						DEF_LOG_RESULT(Util_decoder_video_get_image, false, result);

					if(result == DEF_ERR_OUT_OF_MEMORY || result == DEF_ERR_OUT_OF_LINEAR_MEMORY)
					{
						//Give up on this image on memory allocation failure.
						if(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING)
							Util_decoder_mvd_skip_image(&pos, DEF_VID_DECORDER_SESSION_ID);
						else
							Util_decoder_video_skip_image(&pos, packet_index, DEF_VID_DECORDER_SESSION_ID);
					}
				}

				if(result == DEF_SUCCESS)
				{
					vid_player.video_buffer_pts[next_store_index][packet_index] = pos;
					if(vid_player.is_sbs_3d)
						vid_player.video_buffer_pts[next_store_index][EYE_RIGHT] = pos;

					//Update buffer index.
					if((next_store_index + 1) < VIDEO_BUFFERS)
						next_store_index++;
					else
						next_store_index = 0;

					vid_player.next_store_index[packet_index] = next_store_index;
					//Mirror store index for right eye in SBS 3D mode.
					if(vid_player.is_sbs_3d)
						vid_player.next_store_index[EYE_RIGHT] = next_store_index;

				}
				else
				{
					if(result == DEF_ERR_OUT_OF_MEMORY || result == DEF_ERR_OUT_OF_LINEAR_MEMORY)
					{
						//Request to increase amount of RAM to keep.
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_INCREASE_KEEP_RAM_REQUEST,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
					}

					Util_sleep(1000);
				}

				free(yuv_video);
				free(video);
				yuv_video = NULL;
				video = NULL;

				if((packet_index + 1) < vid_player.num_of_video_tracks)
					packet_index++;
				else
					packet_index = 0;
			}
		}
		else if(vid_player.state == PLAYER_STATE_BUFFERING)
		{
			Util_sync_lock(&vid_player.delay_update_lock, UINT64_MAX);
			Vid_init_desync_data();
			Util_sync_unlock(&vid_player.delay_update_lock);

			if(num_of_cached_raw_images >= VID_FIXED_RESTART_PLAYBACK_THRESHOLD
			|| (Util_speaker_get_available_buffer_num(DEF_VID_SPEAKER_SESSION_ID) + 1) >= DEF_SPEAKER_MAX_BUFFERS)
			{
				//Notify we've finished buffering.
				DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_notification_queue, CONVERT_THREAD_FINISHED_BUFFERING_NOTIFICATION,
				NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
			}
		}
		else
			Util_sleep(1000);
	}

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}
