/* Settings engine: config/state persistence, LCD HW thread, worker (log dump).
 * Full-screen Sem UI (Sem_main / Sem_hid) removed — use player Settings panel only. */

#include "system/sem.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "3ds.h"

#include "system/menu.h"
#include "system/draw/draw.h"
#include "system/util/err.h"
#include "system/util/file.h"
#include "system/util/hid.h"
#include "system/util/hw_config.h"
#include "system/util/log.h"
#include "system/util/str.h"
#include "system/util/sync.h"
#include "system/util/thread_types.h"
#include "system/util/util.h"
#include "system/util/watch.h"

typedef struct {
	Sem_model fake_model;
} Sem_internal_state;

static void Sem_get_system_info(void);
static void Sem_worker_callback(void);
void Sem_hw_config_thread(void* arg);
static uint32_t Sem_persist_fake_model_to_disk(void);

static bool sem_main_run = false;
static bool sem_already_init = false;
static bool sem_thread_run = false;
static Sem_internal_state sem_internal_state = { 0, };
static Sem_state sem_state = { 0, };
static Sem_config sem_config = { 0, };
static Sync_data sem_config_state_mutex = { 0, };
static Thread sem_hw_config_thread = NULL;

static const char* sem_model_name[2] = { "O3DS", "N3DS", };

bool Sem_query_init_flag(void)
{
	return sem_already_init;
}

bool Sem_query_running_flag(void)
{
	return sem_main_run;
}

void Sem_resume(void) { }

void Sem_suspend(void) { }

void Sem_get_config(Sem_config* config)
{
	if(!sem_already_init || !config)
		return;
	Util_sync_lock(&sem_config_state_mutex, UINT64_MAX);
	memcpy(config, &sem_config, sizeof(Sem_config));
	Util_sync_unlock(&sem_config_state_mutex);
}

void Sem_set_config(Sem_config* new_config)
{
	if(!sem_already_init || !new_config)
		return;

	Util_sync_lock(&sem_config_state_mutex, UINT64_MAX);

	if(new_config->screen_mode >= DEF_SEM_SCREEN_MODE_MAX)
		new_config->screen_mode = DEF_SEM_SCREEN_MODE_AUTO;
	memcpy(&sem_config, new_config, sizeof(Sem_config));

	Util_sync_unlock(&sem_config_state_mutex);
}

void Sem_get_state(Sem_state* state)
{
	if(!sem_already_init || !state)
		return;
	Util_sync_lock(&sem_config_state_mutex, UINT64_MAX);
	memcpy(state, &sem_state, sizeof(Sem_state));
	Util_sync_unlock(&sem_config_state_mutex);
}

static uint32_t Sem_persist_fake_model_to_disk(void)
{
	uint32_t result = DEF_ERR_OTHER;

	if(sem_internal_state.fake_model < DEF_SEM_MODEL_MAX)
	{
		uint8_t fake_buf[2] = { 1, sem_internal_state.fake_model };
		DEF_LOG_RESULT_SMART(result, Util_file_save_to_file("fake_model.txt", DEF_MENU_MAIN_DIR, fake_buf, sizeof(fake_buf), true), (result == DEF_SUCCESS), result);
	}
	else
		DEF_LOG_RESULT_SMART(result, Util_file_delete_file("fake_model.txt", DEF_MENU_MAIN_DIR), (result == DEF_SUCCESS), result);

	return result;
}

uint8_t Sem_query_fake_model(void)
{
	if(!sem_already_init)
		return 255;
	return sem_internal_state.fake_model;
}

void Sem_set_fake_model(uint8_t fake_model)
{
	if(!sem_already_init)
		return;

	Util_sync_lock(&sem_config_state_mutex, UINT64_MAX);

	sem_internal_state.fake_model = fake_model;
	if(fake_model < DEF_SEM_MODEL_MAX)
		sem_state.console_model = fake_model;
	else
	{
		uint8_t model = 0;
		if(CFGU_GetSystemModel(&model) == DEF_SUCCESS)
		{
			if(model == CFG_MODEL_N3DS || model == CFG_MODEL_N3DSXL || model == CFG_MODEL_N2DSXL)
				sem_state.console_model = DEF_SEM_MODEL_N3DS;
			else
				sem_state.console_model = DEF_SEM_MODEL_O3DS;
		}
		else
			sem_state.console_model = DEF_SEM_MODEL_O3DS;
	}

	if(!DEF_SEM_MODEL_IS_NEW(sem_state.console_model))
		osSetSpeedupEnable(false);
	else
		osSetSpeedupEnable(true);

	Util_sync_unlock(&sem_config_state_mutex);

	Draw_set_refresh_needed(true);
	Sem_persist_fake_model_to_disk();
}

void Sem_request_log_dump(void)
{
	/* Dump logs 设置项已移除，不再排队导出
	if(!sem_already_init)
		return;
	if(sem_dump_log_request)
		return;
	sem_dump_log_request = true;
	*/
	(void)0;
}

void Sem_init(void)
{
	DEF_LOG_STRING("Initializing...");
	uint8_t model = 0;
	uint8_t* read_cache = NULL;
	uint32_t read_size = 0;
	uint32_t result = DEF_ERR_OTHER;
	Str_data data[14] = { 0, };
	Sem_config config = { 0, };
	Sem_state state = { 0, };

	config.is_debug = false;
	config.is_night = true;
	config.is_eco = true;
	config.is_wifi_on = false;
	config.is_top_lcd_on = true;
	config.is_bottom_lcd_on = true;
	config.time_to_turn_off_lcd = 0;
	config.time_to_enter_sleep = 0;
	config.scroll_speed = 0.5;
	config.screen_mode = DEF_SEM_SCREEN_MODE_AUTO;
	sem_config = config;

	state.console_model = DEF_SEM_MODEL_O3DS;
	sem_state = state;

	result = Util_sync_create(&sem_config_state_mutex, SYNC_TYPE_NON_RECURSIVE_MUTEX);
	if(result != DEF_SUCCESS)
	{
		DEF_LOG_RESULT(Util_sync_create, false, result);
		return;
	}

	if(CFGU_GetSystemModel(&model) == DEF_SUCCESS)
	{
		if(model == CFG_MODEL_N3DS || model == CFG_MODEL_N3DSXL || model == CFG_MODEL_N2DSXL)
			state.console_model = DEF_SEM_MODEL_N3DS;
		else
			state.console_model = DEF_SEM_MODEL_O3DS;
		DEF_LOG_FORMAT("Model : %s", sem_model_name[state.console_model]);
	}

	if(Util_file_load_from_file("fake_model.txt", DEF_MENU_MAIN_DIR, &read_cache, 8, 0, &read_size) == DEF_SUCCESS)
	{
		uint8_t fm = 255;
		if(read_size >= 2 && read_cache[0] == 1 && read_cache[1] < DEF_SEM_MODEL_MAX)
			fm = read_cache[1];
		else if(read_size >= 1)
		{
			fm = read_cache[0];
			if(fm < 4)
				fm = (fm <= 1) ? DEF_SEM_MODEL_O3DS : DEF_SEM_MODEL_N3DS;
			else if(fm < 6)
				fm = (fm <= 2) ? DEF_SEM_MODEL_O3DS : DEF_SEM_MODEL_N3DS;
			else
				fm = 255;
		}
		if(fm < DEF_SEM_MODEL_MAX)
		{
			state.console_model = fm;
			sem_internal_state.fake_model = state.console_model;
			DEF_LOG_FORMAT("Using fake model : %s", sem_model_name[state.console_model]);
		}
		else
			sem_internal_state.fake_model = 255;
		free(read_cache);
	}
	else
		sem_internal_state.fake_model = 255;

	if(!DEF_SEM_MODEL_IS_NEW(state.console_model))
		osSetSpeedupEnable(false);

	DEF_LOG_RESULT_SMART(result, Util_file_load_from_file("settings.txt", DEF_MENU_MAIN_DIR, &read_cache, 0x1000, 0, &read_size), (result == DEF_SUCCESS), result);
	if(result == DEF_SUCCESS)
	{
		DEF_LOG_RESULT_SMART(result, Util_parse_file((char*)read_cache, 14, data), (result == DEF_SUCCESS), result);
		if(result != DEF_SUCCESS)
			DEF_LOG_RESULT_SMART(result, Util_parse_file((char*)read_cache, 13, data), (result == DEF_SUCCESS), result);
		if(result != DEF_SUCCESS)
			DEF_LOG_RESULT_SMART(result, Util_parse_file((char*)read_cache, 12, data), (result == DEF_SUCCESS), result);
		if(result != DEF_SUCCESS)
			DEF_LOG_RESULT_SMART(result, Util_parse_file((char*)read_cache, 11, data), (result == DEF_SUCCESS), result);

		if(!Util_str_has_data(&data[11]))
		{
			Util_str_init(&data[11]);
			Util_str_set(&data[11], "175");
		}
		if(!Util_str_has_data(&data[12]))
		{
			Util_str_init(&data[12]);
			Util_str_set(&data[12], "175");
		}
		if(!Util_str_has_data(&data[11]) || !Util_str_has_data(&data[12]))
			result = DEF_ERR_OUT_OF_MEMORY;

		if(result == DEF_SUCCESS)
		{
			config.time_to_turn_off_lcd = 0;
			config.scroll_speed = 0.5;
			/* <4> 曾为 Send app info，已移除；读档时忽略 */
			state.num_of_launch = (uint32_t)strtoul(DEF_STR_NEVER_NULL(&data[5]), NULL, 10);
			config.is_night = true;
			config.is_eco = (strtoul(DEF_STR_NEVER_NULL(&data[7]), NULL, 10) != 0);
			if(Util_str_has_data(&data[13]))
				(void)strtoul(DEF_STR_NEVER_NULL(&data[13]), NULL, 10); /* 忽略：Debug 已取消 */
			config.is_debug = false;
			{
				uint32_t screen_schema = 0;
				Sem_screen_mode sm = (Sem_screen_mode)strtoul(DEF_STR_NEVER_NULL(&data[11]), NULL, 10);
				if(Util_str_has_data(&data[8]))
					screen_schema = (uint32_t)strtoul(DEF_STR_NEVER_NULL(&data[8]), NULL, 10);
				if(screen_schema < 1)
				{
					if(sm == (Sem_screen_mode)3)
						sm = DEF_SEM_SCREEN_MODE_3D;
					else if(sm == (Sem_screen_mode)2)
						sm = DEF_SEM_SCREEN_MODE_AUTO;
					else if(sm > (Sem_screen_mode)3)
						sm = DEF_SEM_SCREEN_MODE_AUTO;
				}
				else if(sm >= DEF_SEM_SCREEN_MODE_MAX)
					sm = DEF_SEM_SCREEN_MODE_AUTO;
				config.screen_mode = sm;
			}
			config.time_to_enter_sleep = 0;
			sem_config = config;
		}
		free(read_cache);
		read_cache = NULL;
	}

	for(uint8_t i = 0; i < 14; i++)
		Util_str_free(&data[i]);

	config = sem_config;
	sem_config = config;
	sem_state = state;

	sem_thread_run = true;
	sem_hw_config_thread = threadCreate(Sem_hw_config_thread, NULL, DEF_THREAD_STACKSIZE, (DEF_THREAD_PRIORITY_HIGH - 1), 1, false);

	Util_watch_add(WATCH_HANDLE_SETTINGS_MENU, &sem_config.screen_mode, sizeof(sem_config.screen_mode));
	Util_watch_add(WATCH_HANDLE_SETTINGS_MENU, &sem_config.is_eco, sizeof(sem_config.is_eco));

	DEF_LOG_RESULT_SMART(result, Menu_add_worker_thread_callback(Sem_worker_callback), result, result);

	sem_main_run = false;
	Util_hid_reset_key_state(HID_KEY_BIT_ALL);
	Draw_set_refresh_needed(true);
	sem_already_init = true;
	Sem_get_system_info();

	DEF_LOG_STRING("Initialized.");
}

void Sem_exit(void)
{
	DEF_LOG_STRING("Exiting...");
	uint32_t result = DEF_ERR_OTHER;
	Str_data data = { 0, };
	Sem_config config = { 0, };
	Sem_state state = { 0, };

	Sem_get_config(&config);
	Sem_get_state(&state);
	Util_str_init(&data);

	state.num_of_launch++;

	Util_str_format_append(&data, "<0>%s</0>", config.lang);
	Util_str_format_append(&data, "<1>0</1>");
	Util_str_format_append(&data, "<2>%" PRIu16 "</2>", config.time_to_turn_off_lcd);
	Util_str_format_append(&data, "<3>%f</3>", config.scroll_speed);
	Util_str_format_append(&data, "<4>0</4>");
	Util_str_format_append(&data, "<5>%" PRIu32 "</5>", state.num_of_launch);
	Util_str_format_append(&data, "<6>%" PRIu8 "</6>", config.is_night);
	Util_str_format_append(&data, "<7>%" PRIu8 "</7>", config.is_eco);
	Util_str_format_append(&data, "<8>1</8>");
	Util_str_format_append(&data, "<9>0</9><10>0</10>");
	Util_str_format_append(&data, "<11>%" PRIu8 "</11>", config.screen_mode);
	Util_str_format_append(&data, "<12>%" PRIu16 "</12>", config.time_to_enter_sleep);
	Util_str_format_append(&data, "<13>0</13>");

	sem_already_init = false;
	sem_thread_run = false;
	Menu_remove_worker_thread_callback(Sem_worker_callback);

	DEF_LOG_RESULT_SMART(result, Util_file_save_to_file("settings.txt", DEF_MENU_MAIN_DIR, (uint8_t*)data.buffer, data.length, true), (result == DEF_SUCCESS), result);
	Util_str_free(&data);

	DEF_LOG_RESULT_SMART(result, Sem_persist_fake_model_to_disk(), (result == DEF_SUCCESS), result);

	DEF_LOG_RESULT_SMART(result, threadJoin(sem_hw_config_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	threadFree(sem_hw_config_thread);

	Util_watch_remove(WATCH_HANDLE_SETTINGS_MENU, &sem_config.screen_mode);
	Util_watch_remove(WATCH_HANDLE_SETTINGS_MENU, &sem_config.is_eco);

	Util_sync_destroy(&sem_config_state_mutex);

	DEF_LOG_STRING("Exited.");
}

static void Sem_get_system_info(void)
{
	uint8_t is_charging = 0;
	uint32_t result = DEF_ERR_OTHER;
	time_t unix_time = time(NULL);
	const struct tm* tm_info = gmtime(&unix_time);
	Sem_config config = { 0, };
	Sem_state state = { 0, };

	if(!tm_info)
		return;

	Sem_get_config(&config);
	Sem_get_state(&state);

	PTMU_GetBatteryChargeState(&is_charging);
	state.is_charging = (bool)is_charging;

	result = MCUHWC_GetBatteryLevel(&state.battery_level);
	if(result == DEF_SUCCESS)
	{
		uint8_t battery_voltage = 0;
		MCUHWC_GetBatteryVoltage(&battery_voltage);
		MCUHWC_ReadRegister(0x0A, &state.battery_temp, 1);
		state.battery_voltage = (5.0 * (battery_voltage / 256.0));
	}
	else
	{
		uint8_t ptmu_battery_level = 0;
		PTMU_GetBatteryLevel(&ptmu_battery_level);
		if(ptmu_battery_level == 0) state.battery_level = 0;
		else if(ptmu_battery_level == 1) state.battery_level = 5;
		else if(ptmu_battery_level == 2) state.battery_level = 10;
		else if(ptmu_battery_level == 3) state.battery_level = 30;
		else if(ptmu_battery_level == 4) state.battery_level = 60;
		else if(ptmu_battery_level == 5) state.battery_level = 100;
	}

	state.wifi_signal = DEF_SEM_WIFI_SIGNAL_DISABLED;

	state.time.years = (uint16_t)(tm_info->tm_year + 1900);
	state.time.months = (uint8_t)(tm_info->tm_mon + 1);
	state.time.days = (uint8_t)tm_info->tm_mday;
	state.time.hours = (uint8_t)tm_info->tm_hour;
	state.time.minutes = (uint8_t)tm_info->tm_min;
	state.time.seconds = (uint8_t)tm_info->tm_sec;

	snprintf(state.msg, sizeof(state.msg), "%02" PRIu32 "fps %04" PRIu16 "/%02" PRIu8 "/%02" PRIu8 " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 " ",
		(uint32_t)Draw_query_fps(), state.time.years, state.time.months, state.time.days, state.time.hours, state.time.minutes, state.time.seconds);

	Util_sync_lock(&sem_config_state_mutex, UINT64_MAX);
	sem_state = state;
	Util_sync_unlock(&sem_config_state_mutex);
}

static void Sem_worker_callback(void)
{
	if(!sem_already_init)
		return;

	/* Dump logs 已禁用（原 sem_dump_log_request + Util_log_dump）
	if(sem_dump_log_request) { ... }
	*/
}

void Sem_hw_config_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");
	uint64_t previous_system_info_ts = 0;
	uint64_t previous_draw_ts = 0;

	while(!sem_already_init)
		Util_sleep(DEF_THREAD_ACTIVE_SLEEP_TIME);

	while(sem_thread_run)
	{
		uint32_t result = DEF_ERR_OTHER;
		uint64_t current_ts = osGetTime();
		Sem_config config = { 0, };
		static Hid_info hid_info = { 0, };

		Sem_get_config(&config);
		Util_hid_query_key_state(&hid_info);

		if((previous_system_info_ts + 250) <= current_ts)
		{
			Sem_get_system_info();
			if((previous_system_info_ts + (250 * 1.5)) >= current_ts)
				previous_system_info_ts += 250;
			else
				previous_system_info_ts = current_ts;
		}

		if((previous_draw_ts + 1000) <= current_ts)
		{
			Draw_set_refresh_needed(true);
			if((previous_draw_ts + (1000 * 1.5)) >= current_ts)
				previous_draw_ts += 1000;
			else
				previous_draw_ts = current_ts;
		}

		if(config.time_to_turn_off_lcd > 0 && DEF_UTIL_MS_TO_S(hid_info.afk_time_ms) > config.time_to_turn_off_lcd)
		{
			result = Util_hw_config_set_screen_state(true, true, false);
			if(result != DEF_SUCCESS)
				DEF_LOG_RESULT(Util_hw_config_set_screen_state, false, result);
		}
		else
		{
			result = Util_hw_config_set_screen_state(true, false, config.is_top_lcd_on);
			if(result != DEF_SUCCESS)
				DEF_LOG_RESULT(Util_hw_config_set_screen_state, false, result);
			result = Util_hw_config_set_screen_state(false, true, config.is_bottom_lcd_on);
			if(result != DEF_SUCCESS)
				DEF_LOG_RESULT(Util_hw_config_set_screen_state, false, result);
		}

		if(config.time_to_enter_sleep > 0 && DEF_UTIL_MS_TO_S(hid_info.afk_time_ms) > config.time_to_enter_sleep)
		{
			result = Util_hw_config_sleep_system((HW_CONFIG_WAKEUP_BIT_OPEN_SHELL | HW_CONFIG_WAKEUP_BIT_PRESS_HOME_BUTTON));
			if(result == DEF_SUCCESS)
				Util_hid_reset_afk_time();
			else
				DEF_LOG_RESULT(Util_hw_config_sleep_system, false, result);
		}

		Util_sleep(DEF_THREAD_ACTIVE_SLEEP_TIME);
	}

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}
