#include "vid_sync.h"

#include "vid_state.h"

void Vid_init_desync_data(void)
{
	for(uint32_t i = 0; i < EYE_MAX; i++)
	{
		vid_player.wait_threshold_exceeded_ts[i] = 0;
		vid_player.drop_threshold_exceeded_ts[i] = 0;
		vid_player.last_video_frame_updated_ts[i] = 0;
		vid_player.video_delay_avg_ms[i] = 0;
		for(uint16_t k = 0; k < DELAY_SAMPLES; k++)
			vid_player.video_delay_ms[i][k] = 0;
	}

	for(uint8_t b = 0; b < VIDEO_BUFFERS; b++)
	{
		for(uint32_t e = 0; e < EYE_MAX; e++)
			vid_player.video_buffer_pts[b][e] = -1.0;
	}
}

void Vid_update_video_delay(Vid_eye eye_index)
{
	uint8_t array_size = DELAY_SAMPLES;
	uint8_t buffer_health = 0;
	uint16_t next_store_index = vid_player.next_store_index[eye_index];
	uint16_t next_draw_index = vid_player.next_draw_index[eye_index];
	uint64_t current_ts = osGetTime();
	double buffered_video_ms = 0;
	double total_delay = 0;

	if(next_draw_index <= next_store_index)
		buffer_health = (next_store_index - next_draw_index);
	else
		buffer_health = ((VIDEO_BUFFERS - next_draw_index) + next_store_index);

	buffered_video_ms = (buffer_health * vid_player.video_frametime[eye_index]);

	if((vid_player.last_video_frame_updated_ts[eye_index] + vid_player.video_frametime[eye_index]) > current_ts)
		buffered_video_ms += ((vid_player.last_video_frame_updated_ts[eye_index] + vid_player.video_frametime[eye_index]) - current_ts);

	for(uint8_t i = 0; i < array_size - 1; i++)
		vid_player.video_delay_ms[eye_index][i] = vid_player.video_delay_ms[eye_index][i + 1];

	/* Prefer PTS of the frame actually shown (slot before next_draw); avoids mixing
	 * "next-to-display" head with a buffered_ms correction. */
	{
		uint8_t nd    = (uint8_t)vid_player.next_draw_index[eye_index];
		uint8_t shown = (nd > 0) ? (uint8_t)(nd - 1) : (uint8_t)(VIDEO_BUFFERS - 1);
		double vpts   = vid_player.video_buffer_pts[shown][eye_index];

		if(vpts >= 0.0)
			vid_player.video_delay_ms[eye_index][array_size - 1] =
			    (vid_player.audio_current_pos - vpts);
		else
			vid_player.video_delay_ms[eye_index][array_size - 1] =
			    (vid_player.audio_current_pos
			     - (vid_player.video_current_pos[eye_index] - buffered_video_ms));
	}

	for(uint8_t i = 0; i < array_size; i++)
		total_delay += vid_player.video_delay_ms[eye_index][i];

	vid_player.video_delay_avg_ms[eye_index] = (total_delay / array_size);
}

void Vid_update_decoding_delay(double decoding_time, double* total_delay, Vid_eye eye_index)
{
	(void)decoding_time;
	(void)total_delay;
	(void)eye_index;
}
