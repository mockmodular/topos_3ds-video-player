//Includes.
#include "vid_settings.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "vid_state.h"
#include "video_player.h"
#include "vid_panel_settings.h"
#include "system/menu.h"
#include "system/topos_setting.h"
#include "system/sem.h"
#include "system/util/decoder.h"
#include "system/util/file.h"
#include "system/util/hw_config.h"
#include "system/util/err.h"
#include "system/util/log.h"
#include "system/util/str.h"
#include "system/util/util.h"

uint8_t Vid_get_default_num_of_threads(void)
{
	/* 按 Util_init 时冻结的核心数，与 Sem/Fake 机型无关。 */
	if (Util_boot_cpu_core_count() == 4)
		return NUM_OF_THREADS_N3DS;
	return NUM_OF_THREADS_O3DS;
}

void Vid_init_settings(void)
{
	vid_player.auto_dim_5s = false;
	vid_player.fs_browser_root_mode = VID_FS_BROWSER_ROOT_TF;
	vid_player.ui_mod = true;
	vid_player.texture_filter_mode = VID_TEX_FILTER_AUTO;
	vid_player.video_scale_mode = VID_SCALE_FIT;
	vid_player.seek_duration = 10;
	/* Default software volume (0–100); missing <7> in save file also falls back to 100 in Vid_load_settings. */
	vid_player.volume = 100;

	vid_player.disable_audio = false;
	vid_player.disable_video = false;
	vid_player.use_hw_decoding = true;
	vid_player.use_hw_color_conversion = VID_HW_CONV_Y2R_X2;
	vid_player.use_hw_decoding_pending = true;
	vid_player.use_hw_color_conversion_pending = VID_HW_CONV_Y2R_X2;
	vid_player.use_multi_threaded_decoding = true;
	vid_player.num_of_threads = Vid_get_default_num_of_threads();
}

void Vid_init_hidden_settings(void)
{
	bool frame_cores[4] = { false, false, false, false, };
	bool slice_cores[4] = { false, false, false, false, };
	Sem_state state = { 0, };

	Sem_get_state(&state);

	vid_player.thread_mode = MEDIA_THREAD_TYPE_AUTO;
	vid_player.ram_to_keep_base = RAM_TO_KEEP_BASE;

	if(DEF_SEM_MODEL_IS_NEW(state.console_model))
	{
		//N3DS: decode workers on C0, C1, C2; omit C3 (system). fake_pthread order 0→2→1, start 0 → …021021…
		frame_cores[0] = Util_is_core_available(0);
		frame_cores[1] = Util_is_core_available(1);
		frame_cores[2] = Util_is_core_available(2);
		frame_cores[3] = false;
		slice_cores[0] = Util_is_core_available(0);
		slice_cores[1] = Util_is_core_available(1);
		slice_cores[2] = Util_is_core_available(2);
		slice_cores[3] = false;
		Util_decoder_video_set_enabled_cores(frame_cores, slice_cores, 0);
	}
	else
	{
		//O3DS: use C0 and C1 for decode workers; rotate C1 → C0 → C1 → ...
		frame_cores[0] = Util_is_core_available(0);
		frame_cores[1] = Util_is_core_available(1);
		slice_cores[0] = Util_is_core_available(0);
		slice_cores[1] = Util_is_core_available(1);
		Util_decoder_video_set_enabled_cores(frame_cores, slice_cores, 1);
	}
}

uint32_t Vid_load_settings(void)
{
	uint8_t settings_valid_until = 0;
	uint8_t* cache = NULL;
	uint32_t result = DEF_ERR_OTHER;
	Str_data out_data[SETTINGS_ELEMENTS_V15] = { 0, };

	{
		char* boot_vid = NULL;
		uint32_t boot_len = 0;
		Topos_md_bundle b = { 0, };
		bool from_boot = Topos_md_boot_take_vid(&boot_vid, &boot_len);

		if(from_boot)
		{
			cache = (uint8_t*)boot_vid;
			result = (boot_len > 0) ? DEF_SUCCESS : DEF_ERR_OTHER;
		}
		else
		{
			(void)Topos_md_read_bundle(&b);
			if(b.vid_len > 0 && b.vid_text)
			{
				cache = (uint8_t*)b.vid_text;
				result = DEF_SUCCESS;
			}
			else
				result = DEF_ERR_OTHER;
		}

		if(result == DEF_SUCCESS)
		{
		const uint8_t settings_element_list[] =
		{
			SETTINGS_ELEMENTS_V15,
			SETTINGS_ELEMENTS_V14,
			SETTINGS_ELEMENTS_V13,
			SETTINGS_ELEMENTS_V12,
			SETTINGS_ELEMENTS_V11,
			SETTINGS_ELEMENTS_V10,
			SETTINGS_ELEMENTS_V9,
			SETTINGS_ELEMENTS_V8,
			SETTINGS_ELEMENTS_V7,
			SETTINGS_ELEMENTS_V6,
			SETTINGS_ELEMENTS_V5,
			SETTINGS_ELEMENTS_V4,
			SETTINGS_ELEMENTS_V3,
			SETTINGS_ELEMENTS_V2,
			SETTINGS_ELEMENTS_V1,
			SETTINGS_ELEMENTS_V0,
		};

		//Try to load settings.
		for(uint8_t i = 0; i < DEF_UTIL_ARRAY_NUM_OF_ELEMENTS(settings_element_list); i++)
		{
			DEF_LOG_RESULT_SMART(result, Util_parse_file((char*)cache, settings_element_list[i], out_data), (result == DEF_SUCCESS), result);
			if(result == DEF_SUCCESS)
			{
				settings_valid_until = settings_element_list[i];
				DEF_LOG_INT(settings_valid_until);
				break;
			}
		}
		}
		if(from_boot)
			free(boot_vid);
		else
			Topos_md_bundle_free(&b);
	}

	if(result != DEF_SUCCESS)
		DEF_LOG_STRING("Couldn't read settings file, applying default settings!!!!!");

	vid_player.texture_filter_mode = VID_TEX_FILTER_AUTO;
	if(settings_valid_until > 0)
	{
		uint8_t z0 = (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[0]), NULL, 10);
		vid_player.texture_filter_mode = (z0 != 0) ? VID_TEX_FILTER_BILINEAR : VID_TEX_FILTER_NEAREST;
	}
	if(settings_valid_until >= SETTINGS_ELEMENTS_V10)
	{
		uint8_t tm = (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[20]), NULL, 10);
		if(tm <= VID_TEX_FILTER_AUTO)
			vid_player.texture_filter_mode = tm;
	}
	vid_player.video_scale_mode = VID_SCALE_FIT;
	if(settings_valid_until >= SETTINGS_ELEMENTS_V11)
	{
		uint8_t sm = (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[21]), NULL, 10);
		if(sm <= VID_SCALE_FIT)
			vid_player.video_scale_mode = sm;
	}
	vid_player.fs_browser_root_mode = VID_FS_BROWSER_ROOT_TF;
	if(settings_valid_until >= SETTINGS_ELEMENTS_V12)
	{
		uint8_t br = (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[22]), NULL, 10);
		if(br <= 1)
			vid_player.fs_browser_root_mode = br;
	}
	vid_player.ui_mod = true;
	if(settings_valid_until >= SETTINGS_ELEMENTS_V13)
		vid_player.ui_mod = (strtoul(DEF_STR_NEVER_NULL(&out_data[23]), NULL, 10) != 0);
	if(settings_valid_until >= SETTINGS_ELEMENTS_V14)
	{
		Sem_config sem_cfg = { 0, };
		uint8_t sm  = (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[24]), NULL, 10);
		uint8_t eco = (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[25]), NULL, 10);
		uint8_t fmu = (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[26]), NULL, 10);

		Sem_get_config(&sem_cfg);
		if(sm < DEF_SEM_SCREEN_MODE_MAX)
			sem_cfg.screen_mode = sm;
		sem_cfg.is_eco = (eco != 0);
		Sem_set_config(&sem_cfg);
		if(fmu <= 1)
			Sem_set_fake_model(fmu == 0 ? DEF_SEM_MODEL_N3DS : DEF_SEM_MODEL_O3DS);
		else
			Sem_set_fake_model(255);
		vid_panel_refit_layout_refresh_tex();
	}
	vid_player.sbs_swap_eyes = false;
	if(settings_valid_until >= SETTINGS_ELEMENTS_V15)
		vid_player.sbs_swap_eyes = (strtoul(DEF_STR_NEVER_NULL(&out_data[27]), NULL, 10) != 0);
	vid_player.use_hw_decoding = ((settings_valid_until > 3) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[3]), NULL, 10) != 0) : true);
	vid_player.use_hw_color_conversion = ((settings_valid_until > 4) ? (uint8_t)strtoul(DEF_STR_NEVER_NULL(&out_data[4]), NULL, 10) : VID_HW_CONV_Y2R_X2);
	vid_player.use_hw_decoding_pending = vid_player.use_hw_decoding;
	vid_player.use_hw_color_conversion_pending = vid_player.use_hw_color_conversion;
	vid_player.use_multi_threaded_decoding = ((settings_valid_until > 5) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[5]), NULL, 10) != 0) : true);
	vid_player.volume = ((settings_valid_until > 7) ? (uint16_t)Util_max(strtoul(DEF_STR_NEVER_NULL(&out_data[7]), NULL, 10), 0) : 100);
	vid_player.seek_duration = ((settings_valid_until > 8) ? (uint8_t)Util_max(strtoul(DEF_STR_NEVER_NULL(&out_data[8]), NULL, 10), 0) : 10);
	(void)((settings_valid_until > 9) ? strtoul(DEF_STR_NEVER_NULL(&out_data[9]), NULL, 10) : 1); /* 旧版 aspect ratio，已忽略 */
	if(settings_valid_until >= SETTINGS_ELEMENTS_V9)
	{
		vid_player.disable_audio = ((settings_valid_until > 12) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[12]), NULL, 10) != 0) : false);
		vid_player.disable_video = ((settings_valid_until > 13) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[13]), NULL, 10) != 0) : false);
		vid_player.num_of_threads = ((settings_valid_until > 15) ? (uint8_t)Util_max(strtoul(DEF_STR_NEVER_NULL(&out_data[15]), NULL, 10), 0) : Vid_get_default_num_of_threads());
		vid_player.auto_dim_5s = ((settings_valid_until > 16) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[16]), NULL, 10) != 0) : false);
		(void)((settings_valid_until > 17) ? strtoul(DEF_STR_NEVER_NULL(&out_data[17]), NULL, 10) : 0); /* MVD upload: fixed Unroll4 */
		(void)((settings_valid_until > 18) ? strtoul(DEF_STR_NEVER_NULL(&out_data[18]), NULL, 10) : 0);
		(void)((settings_valid_until > 19) ? strtoul(DEF_STR_NEVER_NULL(&out_data[19]), NULL, 10) : 0);
	}
	else if(settings_valid_until >= SETTINGS_ELEMENTS_V8)
	{
		vid_player.disable_audio = ((settings_valid_until > 12) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[12]), NULL, 10) != 0) : false);
		vid_player.disable_video = ((settings_valid_until > 13) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[13]), NULL, 10) != 0) : false);
		vid_player.num_of_threads = ((settings_valid_until > 15) ? (uint8_t)Util_max(strtoul(DEF_STR_NEVER_NULL(&out_data[15]), NULL, 10), 0) : Vid_get_default_num_of_threads());
		vid_player.auto_dim_5s = ((settings_valid_until > 16) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[16]), NULL, 10) != 0) : false);
	}
	else if(settings_valid_until >= SETTINGS_ELEMENTS_V7)
	{
		vid_player.disable_audio = ((settings_valid_until > 12) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[12]), NULL, 10) != 0) : false);
		vid_player.disable_video = ((settings_valid_until > 13) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[13]), NULL, 10) != 0) : false);
		vid_player.num_of_threads = ((settings_valid_until > 15) ? (uint8_t)Util_max(strtoul(DEF_STR_NEVER_NULL(&out_data[15]), NULL, 10), 0) : Vid_get_default_num_of_threads());
		vid_player.auto_dim_5s = false;
	}
	else
	{
		vid_player.disable_audio = ((settings_valid_until > 13) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[13]), NULL, 10) != 0) : false);
		vid_player.disable_video = ((settings_valid_until > 14) ? (strtoul(DEF_STR_NEVER_NULL(&out_data[14]), NULL, 10) != 0) : false);
		vid_player.num_of_threads = ((settings_valid_until > 17) ? (uint8_t)Util_max(strtoul(DEF_STR_NEVER_NULL(&out_data[17]), NULL, 10), 0) : Vid_get_default_num_of_threads());
		vid_player.auto_dim_5s = false;
	}

	if(vid_player.use_hw_color_conversion == VID_HW_CONV_NEON_Y2R)
		vid_player.use_hw_color_conversion = VID_HW_CONV_Y2R_X2;
	vid_player.use_hw_color_conversion_pending = vid_player.use_hw_color_conversion;

	for(uint8_t i = 0; i < DEF_UTIL_ARRAY_NUM_OF_ELEMENTS(out_data); i++)
		Util_str_free(&out_data[i]);

	/* use_hw_decoding：仅表示「允许尝试 MVD」；真实 O3DS / fake O3DS 均保留用户选择。
	 * 能否走硬解由 vid_decode 按片源与硬件能力决定，不可用则自动软解。 */

	if(vid_player.volume > 999)
		vid_player.volume = 100;

	if(vid_player.seek_duration > 99 || vid_player.seek_duration < 1)
		vid_player.seek_duration = 10;

	//	if(vid_player.num_of_threads > NUM_OF_THREADS_MAX || vid_player.num_of_threads < NUM_OF_THREADS_MIN)
	//		vid_player.num_of_threads = Vid_get_default_num_of_threads();
	vid_player.num_of_threads = Vid_get_default_num_of_threads();

	Vid_log_settings();

	return DEF_SUCCESS;//Settings (or default one) has been loaded.
}

uint32_t Vid_save_settings(void)
{
	uint32_t result = DEF_ERR_OTHER;
	Str_data data = { 0, };
	Sem_config sem_cfg = { 0, };
	uint8_t fake_ui_save = 2;
	uint8_t fq;

	Vid_log_settings();

	Sem_get_config(&sem_cfg);
	fq = Sem_query_fake_model();
	if(fq < DEF_SEM_MODEL_MAX)
		fake_ui_save = (fq == DEF_SEM_MODEL_N3DS) ? (uint8_t)0 : (uint8_t)1;

	Util_str_init(&data);
	Util_str_format_append(&data, "<0>%" PRIu8 "</0>",
		(uint8_t)(vid_player.texture_filter_mode != VID_TEX_FILTER_NEAREST));
	Util_str_format_append(&data, "<1>%" PRIu8 "</1>", (uint8_t)0);
	Util_str_format_append(&data, "<2>%" PRIu8 "</2>", (uint8_t)0);
	Util_str_format_append(&data, "<3>%" PRIu8 "</3>", (uint8_t)vid_player.use_hw_decoding_pending);
	Util_str_format_append(&data, "<4>%" PRIu8 "</4>", vid_player.use_hw_color_conversion_pending);
	Util_str_format_append(&data, "<5>%" PRIu8 "</5>", vid_player.use_multi_threaded_decoding);
	Util_str_format_append(&data, "<6>%" PRIu8 "</6>", (uint8_t)0);
	Util_str_format_append(&data, "<7>%" PRIu16 "</7>", vid_player.volume);
	Util_str_format_append(&data, "<8>%" PRIu8 "</8>", vid_player.seek_duration);
	Util_str_format_append(&data, "<9>1</9>");
	Util_str_format_append(&data, "<10>0</10>");
	Util_str_format_append(&data, "<11>%" PRIu32 "</11>", (uint32_t)0);
	Util_str_format_append(&data, "<12>%" PRIu8 "</12>", vid_player.disable_audio);
	Util_str_format_append(&data, "<13>%" PRIu8 "</13>", vid_player.disable_video);
	Util_str_format_append(&data, "<14>%" PRIu16 "</14>", VID_FIXED_RESTART_PLAYBACK_THRESHOLD);
	Util_str_format_append(&data, "<15>%" PRIu8 "</15>", vid_player.num_of_threads);
	Util_str_format_append(&data, "<16>%" PRIu8 "</16>", vid_player.auto_dim_5s);
	/* <17> legacy: MVD upload mode; fixed Unroll4 (=1) for old parsers. */
	Util_str_format_append(&data, "<17>%" PRIu8 "</17>", (uint8_t)1);
	Util_str_format_append(&data, "<18>0</18>");
	Util_str_format_append(&data, "<19>0</19>");
	Util_str_format_append(&data, "<20>%" PRIu8 "</20>", vid_player.texture_filter_mode);
	Util_str_format_append(&data, "<21>%" PRIu8 "</21>", vid_player.video_scale_mode);
	Util_str_format_append(&data, "<22>%" PRIu8 "</22>", vid_player.fs_browser_root_mode);
	Util_str_format_append(&data, "<23>%" PRIu8 "</23>", (uint8_t)vid_player.ui_mod);
	Util_str_format_append(&data, "<24>%" PRIu8 "</24>", (uint8_t)sem_cfg.screen_mode);
	Util_str_format_append(&data, "<25>%" PRIu8 "</25>", (uint8_t)(sem_cfg.is_eco ? 1 : 0));
	Util_str_format_append(&data, "<26>%" PRIu8 "</26>", fake_ui_save);
	Util_str_format_append(&data, "<27>%" PRIu8 "</27>", (uint8_t)vid_player.sbs_swap_eyes);

	{
		Topos_md_bundle b = { 0, };
		(void)Topos_md_read_bundle(&b);
		DEF_LOG_RESULT_SMART(result, Topos_md_write_bundle(
			b.sem_text ? b.sem_text : "", b.sem_len,
			data.buffer, data.length,
			b.fake_model), (result == DEF_SUCCESS), result);
		Topos_md_bundle_free(&b);
	}

	Util_str_free(&data);
	return result;
}

void Vid_log_settings(void)
{
	DEF_LOG_UINT(vid_player.texture_filter_mode);
	DEF_LOG_UINT(vid_player.video_scale_mode);
	DEF_LOG_BOOL(vid_player.use_hw_decoding);
	DEF_LOG_UINT(vid_player.use_hw_color_conversion);
	DEF_LOG_BOOL(vid_player.use_multi_threaded_decoding);
	DEF_LOG_UINT(vid_player.volume);
	DEF_LOG_UINT(vid_player.seek_duration);
	DEF_LOG_UINT(vid_player.fs_browser_root_mode);
	DEF_LOG_BOOL(vid_player.ui_mod);
	DEF_LOG_BOOL(vid_player.sbs_swap_eyes);
	DEF_LOG_BOOL(vid_player.disable_audio);
	DEF_LOG_BOOL(vid_player.disable_video);
	DEF_LOG_UINT(vid_player.num_of_threads);
	DEF_LOG_BOOL(vid_player.auto_dim_5s);
}
