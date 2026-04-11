/*
 * vid_panel_menu.c
 *
 * 播放器底屏 UI 绘制层 —— Player 面板：
 *   - 状态栏 / 编解码信息 / CPU+内存诊断
 *   - 时间条 / seek 进度
 *
 * 所有对 vid_player 的访问通过模块初始化时注入的 VidPanelCtx 完成，
 * 不直接 include vid_state.h，使本文件可整体替换。
 */

#include "vid_panel_menu.h"
#include "vid_panel.h"
#include "vid_state.h"
#include "vid_seek_engine.h"
#include "vid_screen.h"
#include "vid_texture.h"
#include "vid_sync.h"
#include "vid_worker.h"

#include <inttypes.h>
#include <math.h>
#include <malloc.h>
#include <libavutil/cpu.h>

#include "vid_panel_layout.h"

#include "system/draw/draw.h"
#include "system/sem.h"
#include "system/util/cpu_usage.h"
#include "system/util/converter.h"
#include "system/util/decoder.h"
#include "system/util/err_types.h"
#include "system/util/speaker.h"
#include "system/util/str.h"
#include "system/util/sync.h"
#include "system/util/util.h"
#include "video_player.h"

/* CPU 占用条：仅在布局有效化时读一次可用核数（2→只画 C0 C1；否则画 C0–C3），非每帧。 */
static bool s_cpu_bar_layout_ready;
static int  s_cpu_bar_core_slots; /* 2 或 4 */

void Vid_panel_player_invalidate_cpu_bar_layout(void)
{
	s_cpu_bar_layout_ready = false;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 状态栏 + 编解码信息 + CPU/内存诊断区（PLAYER 面板底屏上部）
 * ───────────────────────────────────────────────────────────────────────────*/
void Vid_panel_player_draw_status(uint32_t color,
                                   uint32_t back_color,
                                   uint64_t current_ts,
                                   double   video_x_offset_bottom,
                                   double   video_y_offset_bottom,
                                   double   image_width_left,
                                   double   image_height_left,
                                   uint8_t  image_index_left)
{
	Str_data format_str = { 0, };
	Draw_image_data background = Draw_get_empty_image();
	(void)back_color;
	(void)current_ts;

	Util_str_init(&format_str);

	// ——— VIDEO codec ————————————————————————————————————————
	Util_str_format(&format_str, "V:%-6s",
		vid_player.video_info[EYE_LEFT].format_name);
	Draw(&format_str, VP_PLAYER_UI_STATUS_LEFT_X, (float)VP_PLAYER_STATUS_ROW_V_Y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, color);

	// ——— AUDIO codec ————————————————————————————————————————
	Util_str_format(&format_str, "A:%-6s",
		vid_player.audio_info[0].format_name);
	Draw(&format_str, VP_PLAYER_UI_STATUS_LEFT_X, (float)VP_PLAYER_STATUS_ROW_A_Y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, color);

	// ——— resolution（左）与 fps（固定列）分开绘制；SBS/3D 靠右固定列 —————————
	// AV1: show visible width/height (decoder context), not 128-padded codec_*.
	{
		const float res_scale = DEF_DRAW_TEXT_SCALE;
		Media_v_info* vi      = &vid_player.video_info[EYE_LEFT];
		uint32_t rw = vi->codec_width;
		uint32_t rh = vi->codec_height;

		if(vi->is_av1_codec && vi->width > 0 && vi->height > 0)
		{
			rw = vi->width;
			rh = vi->height;
		}
		/* 无视频/未打开文件时为 0x0；有流后为例如 400x240 */
		Util_str_format(&format_str, "%" PRIu32 "x%" PRIu32, rw, rh);
		Draw(&format_str, VP_PLAYER_UI_STATUS_RES_X, (float)VP_PLAYER_STATUS_ROW_RES_Y, res_scale, res_scale, color);

		{
			unsigned fps_i = 0;
			if(vi->framerate > 0.0)
				fps_i = (unsigned)(vi->framerate + 0.5);
			Util_str_format(&format_str, "fps%uhz", fps_i);
			Draw(&format_str, VP_PLAYER_UI_STATUS_FPS_X, (float)VP_PLAYER_STATUS_ROW_RES_Y, res_scale, res_scale, color);
		}

		Util_str_format(&format_str, "SBS:%d  3D:%d",
			(int)vid_player.is_sbs_3d, (int)Draw_is_3d_mode());
		Draw(&format_str, VP_PLAYER_UI_RES_SBS_X, (float)VP_PLAYER_STATUS_ROW_RES_Y, res_scale, res_scale, DEF_DRAW_WEAK_WHITE);
	}

	// ——— DECODE（两行：解码/纹理一行，SAR/ASM 一行，避免单行过长）——————————————
	{
		static int cached_cpu_flags = -1;
		if(cached_cpu_flags < 0)
			cached_cpu_flags = av_get_cpu_flags();
		const char* asm_tag =
			(cached_cpu_flags & AV_CPU_FLAG_ARMV6)   ? "A6"  :
			(cached_cpu_flags & AV_CPU_FLAG_ARMV5TE) ? "A5"  : "--";
		const char* dec_tag =
			(vid_player.sub_state & PLAYER_SUB_STATE_HW_DECODING) ? "MVD/HW" :
			(vid_player.sub_state & PLAYER_SUB_STATE_HW_CONVERSION) ?
				(vid_player.use_hw_color_conversion == VID_HW_CONV_NEON_Y2R ? "NEONy2r" :
				vid_player.is_sbs_3d ? "Y2Rx2" : "SW/Y2R") : "SW/CPU";
		const char* tex_tag =
			Vid_effective_use_linear_texture_filter(EYE_LEFT) ? "LINEAR" : "NEAR";

		Util_str_format(&format_str, "Dec:%s  Tex:%s", dec_tag, tex_tag);
		Draw(&format_str, VP_PLAYER_UI_STATUS_LEFT_X, (float)VP_PLAYER_STATUS_ROW_DEC_Y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, DEF_DRAW_GREEN);

		Util_str_format(&format_str, "SAR:%d/%d  ASM:%s",
			(int)vid_player.video_info[EYE_LEFT].sar_width,
			(int)vid_player.video_info[EYE_LEFT].sar_height,
			asm_tag);
		Draw(&format_str, VP_PLAYER_UI_STATUS_LEFT_X, (float)VP_PLAYER_STATUS_ROW_DEC2_Y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, DEF_DRAW_GREEN);
	}

	// ——— Lin / Heap（分列）+ CPU 标题 + CPU 条（布局在首次绘制或 invalidate 时定一次：2 核档=仅 C0 C1，否则 C0–C3）
	{
		static uint64_t diag_ts = 0;
		static unsigned long diag_lin_kb = 0, diag_heap_free_kb = 0, diag_heap_tot_kb = 0;
		static double diag_c[4] = {0.0, 0.0, 0.0, 0.0};
		static u32 diag_cpu_mhz = 0;
		int k_draw_max;

		if(!s_cpu_bar_layout_ready)
		{
			s_cpu_bar_core_slots = (Util_available_cpu_core_count() == 2) ? 2 : 4;
			s_cpu_bar_layout_ready = true;
		}
		k_draw_max = s_cpu_bar_core_slots;

		uint64_t now = osGetTime();
		if(now - diag_ts >= 1000)
		{
			int k;
			diag_ts = now;
			diag_lin_kb = linearSpaceFree() / 1024;
			struct mallinfo mi = mallinfo();
			diag_heap_free_kb = mi.fordblks / 1024;
			diag_heap_tot_kb  = (mi.uordblks + mi.fordblks) / 1024;
			for(k = 0; k < k_draw_max; k++)
			{
				if(!Util_is_core_available((uint8_t)k))
				{
					diag_c[k] = 0.0;
					continue;
				}
				{
					double v = (double)Util_cpu_usage_get_cpu_usage((uint8_t)k);
					diag_c[k] = isnan(v) ? 0.0 : v;
				}
			}
			for(k = k_draw_max; k < 4; k++)
				diag_c[k] = 0.0;
			u32 tref_idx = OS_SharedConfig->timeref_cnt & 1;
			diag_cpu_mhz = (OS_SharedConfig->timeref[tref_idx].sysclock_hz > 0)
				? (u32)(OS_SharedConfig->timeref[tref_idx].sysclock_hz / 1000000) : 0;
		}

		Util_str_format(&format_str, "Lin:%luKB", diag_lin_kb);
		Draw(&format_str, VP_PLAYER_UI_DIAG_LIN_X, (float)VP_PLAYER_STATUS_ROW_DIAG_LIN_Y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, DEF_DRAW_YELLOW);

		Util_str_format(&format_str, "Heap:%lu/%luKB",
			diag_heap_free_kb, diag_heap_tot_kb);
		Draw(&format_str, VP_PLAYER_UI_DIAG_HEAP_X, (float)VP_PLAYER_STATUS_ROW_DIAG_LIN_Y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, DEF_DRAW_YELLOW);

		Util_str_format(&format_str, "CPU  %luMHz", (unsigned long)diag_cpu_mhz);
		Draw(&format_str, VP_PLAYER_UI_STATUS_LEFT_X, VP_PLAYER_STATUS_ROW_CPUHDR_Y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, DEF_DRAW_YELLOW);

		{
			int k;
			int row = 0;
			for(k = 0; k < k_draw_max; k++)
			{
				int    bar_y  = VP_PLAYER_STATUS_BAR0_Y + row * VP_PLAYER_STATUS_BAR_STRIDE;
				double pct    = diag_c[k] > 100.0 ? 100.0 : diag_c[k];
				int    fill_w = (int)(265.0 * pct / 100.0);

				Util_str_format(&format_str, "C%d", k);
				Draw(&format_str, VP_PLAYER_UI_STATUS_LEFT_X, bar_y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, DEF_DRAW_YELLOW);
				Draw_texture(&background, DEF_DRAW_WEAK_WHITE, VP_PLAYER_UI_STATUS_BAR_X, bar_y + 2, 265, 7);
				if(fill_w > 0)
					Draw_texture(&background, DEF_DRAW_YELLOW, VP_PLAYER_UI_STATUS_BAR_X, bar_y + 2, fill_w, 7);
				Util_str_format(&format_str, "%.0f%%", pct);
				Draw(&format_str, VP_PLAYER_UI_STATUS_PCT_X, bar_y, DEF_DRAW_TEXT_SCALE, DEF_DRAW_TEXT_SCALE, DEF_DRAW_YELLOW);
				row++;
			}
		}
	}

	// video preview thumbnail
	if(vid_player.state != PLAYER_STATE_IDLE)
	{
		if(Util_sync_lock(&vid_player.texture_init_free_lock, 0) == DEF_SUCCESS)
		{
			Vid_large_texture_draw(&vid_player.large_image[image_index_left][EYE_LEFT],
				video_x_offset_bottom + (double)VP_PLAYER_UI_STATUS_THUMB_SHIFT_X, video_y_offset_bottom,
				image_width_left, image_height_left);
			Util_sync_unlock(&vid_player.texture_init_free_lock);
		}
	}

	Util_str_free(&format_str);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 时间条 / seek 进度
 * 字号与 Vid_panel_player_draw_status 相同：均为 DEF_DRAW_TEXT_SCALE（Draw 内再 ×1.2 → C2D）。
 * ───────────────────────────────────────────────────────────────────────────*/
void Vid_panel_player_draw_timebar(uint32_t color, uint64_t current_ts)
{
	Str_data format_str  = { 0, };
	Str_data time_str[2] = { 0, };
	const float txt_sc = DEF_DRAW_TEXT_SCALE;

	Util_str_init(&format_str);
	Util_str_init(&time_str[0]);
	Util_str_init(&time_str[1]);

	VidSeekEngineView sv = VidSeekEngine_get_view();
	double current_bar_pos = DEF_UTIL_MS_TO_S_D(sv.display_pos_ms);

	Util_convert_seconds_to_time(current_bar_pos, &time_str[0]);
	Util_convert_seconds_to_time(DEF_UTIL_MS_TO_S_D(sv.duration_ms), &time_str[1]);

	const float timebar_text_y = (float)VP_PROGRESS_Y - VP_TIMEBAR_TEXT_ABOVE_PROGRESS;

	Util_str_format(&format_str, "%s/%s", DEF_STR_NEVER_NULL(&time_str[0]), DEF_STR_NEVER_NULL(&time_str[1]));
	Draw(&format_str, VP_TIMEBAR_TIME_X, timebar_text_y, txt_sc, txt_sc, color);

	if(sv.is_seeking)
	{
		Util_str_format(&format_str, "%s", "seeking...");
		Draw(&format_str, VP_TIMEBAR_SEEK_X, timebar_text_y, txt_sc, txt_sc, color);
	}
	else if(vid_player.state == PLAYER_STATE_BUFFERING)
	{
		Util_str_format(&format_str, "buffering... (%.2f%%)", vid_player.buffer_progress);
		Draw(&format_str, VP_TIMEBAR_SEEK_X, timebar_text_y, txt_sc, txt_sc, color);
	}

	(void)current_bar_pos;
	(void)current_ts;

	Util_str_free(&format_str);
	Util_str_free(&time_str[0]);
	Util_str_free(&time_str[1]);
}
