#include "vid_screen.h"

#include <math.h>
#include <stdbool.h>

#include "system/draw/draw.h"
#include "system/sem.h"

void Vid_video_presentation_wh(Vid_eye eye, uint32_t *out_w, uint32_t *out_h)
{
	uint32_t w = vid_player.video_info[eye].width;
	uint32_t h = vid_player.video_info[eye].height;

	if(w == 0 || h == 0)
	{
		w = vid_player.video_info[eye].codec_width;
		h = vid_player.video_info[eye].codec_height;
	}
	{
		double sw = vid_player.video_info[eye].sar_width;
		double sh = vid_player.video_info[eye].sar_height;
		if(sw <= 0.0)
			sw = 1.0;
		if(sh <= 0.0)
			sh = 1.0;
		*out_w = (uint32_t)llround((double)w * sw);
		*out_h = (uint32_t)llround((double)h * sh);
	}
}

static bool vid_dims_allow_pixel_perfect(Vid_eye eye)
{
	uint32_t vw, vh;

	Vid_video_presentation_wh(eye, &vw, &vh);
	if(vw == 0 || vh == 0)
		return false;
	return (vh <= 240u && (vh % 2u) == 0u && vw <= 400u && (vw % 2u) == 0u);
}

bool Vid_pixel_perfect_forces_nearest(uint32_t eye_k)
{
	if(vid_player.video_scale_mode != VID_SCALE_PIXEL_PERFECT)
		return false;
	if(vid_player.is_sbs_3d)
		return false;
	if(eye_k >= EYE_MAX)
		eye_k = EYE_LEFT;
	return vid_dims_allow_pixel_perfect((Vid_eye)eye_k);
}

void Vid_fit_to_screen(uint16_t screen_width, uint16_t screen_height, Vid_eye eye_index)
{
	if(vid_player.is_sbs_3d && eye_index == EYE_RIGHT)
	{
		vid_player.video_zoom[EYE_RIGHT]     = vid_player.video_zoom[EYE_LEFT];
		vid_player.video_x_offset[EYE_RIGHT] = vid_player.video_x_offset[EYE_LEFT];
		vid_player.video_y_offset[EYE_RIGHT] = vid_player.video_y_offset[EYE_LEFT];
		return;
	}

	/* Mono 2D + single track: one decoded plane; right eye matches left (e.g. 3D slider on, sem 2D channel). */
	if(!vid_player.is_sbs_3d && eye_index == EYE_RIGHT && vid_player.num_of_video_tracks == 1)
	{
		vid_player.video_zoom[EYE_RIGHT]     = vid_player.video_zoom[EYE_LEFT];
		vid_player.video_x_offset[EYE_RIGHT] = vid_player.video_x_offset[EYE_LEFT];
		vid_player.video_y_offset[EYE_RIGHT] = vid_player.video_y_offset[EYE_LEFT];
		return;
	}

	if(vid_player.is_sbs_3d)
	{
		uint32_t vw, vh;

		Vid_video_presentation_wh(eye_index, &vw, &vh);
		if(vw != 0 && vh != 0)
		{
			/* Each half-frame is Video width/2 x Video height; fit to screen with centered aspect-ratio scaling. */
			double half_w = (double)vw / 2.0;
			double h = (double)vh;

			if(half_w / screen_width >= h / screen_height)
				vid_player.video_zoom[eye_index] = screen_width / half_w;
			else
				vid_player.video_zoom[eye_index] = screen_height / h;

			vid_player.video_x_offset[eye_index] = ((double)screen_width - half_w * vid_player.video_zoom[eye_index]) / 2.0;
			vid_player.video_y_offset[eye_index] = ((double)screen_height - h * vid_player.video_zoom[eye_index]) / 2.0;
		}
	}
	else
	{
		uint32_t vw, vh;

		Vid_video_presentation_wh(eye_index, &vw, &vh);
		if(vw != 0 && vh != 0)
		{
			/* vw/vh 已由 Vid_video_presentation_wh 含 SAR，此处不再二次乘 sar。 */
			if(vid_player.video_scale_mode == VID_SCALE_PIXEL_PERFECT && !vid_player.is_sbs_3d
			&& vid_dims_allow_pixel_perfect(eye_index))
			{
				vid_player.video_zoom[eye_index] = 1.0;
				vid_player.video_x_offset[eye_index] = ((double)screen_width - (double)vw) * 0.5;
				vid_player.video_y_offset[eye_index] = ((double)screen_height - (double)vh) * 0.5;
				vid_player.video_x_offset[eye_index] += (TOP_SCREEN_WIDTH - screen_width);
				vid_player.video_y_offset[eye_index] += (TOP_SCREEN_HEIGHT - screen_height);
			}
			else
			{
				/* Fit to screen size (default / 3D / non-matching pixel-perfect). */
				if(((double)vw / screen_width) >= ((double)vh / screen_height))
					vid_player.video_zoom[eye_index] = (1.0 / (((double)vw / screen_width)));
				else
					vid_player.video_zoom[eye_index] = (1.0 / (((double)vh / screen_height)));

				vid_player.video_x_offset[eye_index] = (screen_width - (vw * vid_player.video_zoom[eye_index])) / 2;
				vid_player.video_y_offset[eye_index] = (screen_height - (vh * vid_player.video_zoom[eye_index])) / 2;
				vid_player.video_x_offset[eye_index] += (TOP_SCREEN_WIDTH - screen_width);
				vid_player.video_y_offset[eye_index] += (TOP_SCREEN_HEIGHT - screen_height);
			}
		}
	}

}

void Vid_change_video_size(double change_px, Vid_eye eye_index)
{
	uint32_t vw, vh;

	Vid_video_presentation_wh(eye_index, &vw, &vh);
	double current_width = (double)vw * vid_player.video_zoom[eye_index];

	if(vw != 0 && vh != 0)
		vid_player.video_zoom[eye_index] = (1.0 / ((double)vw / (current_width + change_px)));
}

bool Vid_bottom_lcd_lit(void)
{
	Sem_config c = { 0, };

	Sem_get_config(&c);
	return c.is_bottom_lcd_on;
}

bool Vid_player_top_should_fill_black(void)
{
	if(vid_player.panel != VID_PANEL_PLAYER)
		return false;
	return !Vid_bottom_lcd_lit();
}

void Vid_enter_full_screen(void)
{
	Sem_config config = { 0, };

	Sem_get_config(&config);
	config.is_bottom_lcd_on = false;
	Sem_set_config(&config);
	Draw_set_refresh_needed(true);
}

void Vid_exit_full_screen(void)
{
	Sem_config config = { 0, };

	Sem_get_config(&config);
	config.is_bottom_lcd_on = true;
	Sem_set_config(&config);
	Draw_set_refresh_needed(true);
}

void Vid_toggle_bottom_lcd_player(void)
{
	Sem_config cfg = { 0, };

	Sem_get_config(&cfg);
	cfg.is_bottom_lcd_on = !cfg.is_bottom_lcd_on;
	Sem_set_config(&cfg);
	vid_player.auto_full_screen_count = 0;

	for(uint32_t i = 0; i < EYE_MAX; i++)
		Vid_fit_to_screen(VID_PLAYER_TOP_FIT_W, VID_PLAYER_TOP_FIT_H, i);

	Draw_set_refresh_needed(true);
}

void Vid_control_full_screen(void)
{
	Sem_config cfg = { 0, };
	Sem_get_config(&cfg);

	/* 自动息屏：只把 Sem `is_bottom_lcd_on` 置 false，与 Select/空白同一开关 */
	if(vid_player.auto_dim_5s && vid_player.panel == VID_PANEL_PLAYER && cfg.is_bottom_lcd_on)
	{
		vid_player.auto_full_screen_count++;
		if(vid_player.auto_full_screen_count >= 300)
		{
			vid_player.auto_full_screen_count = 0;
			for(uint32_t i = 0; i < EYE_MAX; i++)
				Vid_fit_to_screen(VID_PLAYER_TOP_FIT_W, VID_PLAYER_TOP_FIT_H, i);
			Vid_enter_full_screen();
		}
	}
	else
		vid_player.auto_full_screen_count = 0;
}
