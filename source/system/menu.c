/* Minimal launcher: init platform, Sem engine, then video player only (no main menu UI). */

#include "system/menu.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "3ds.h"

#include "system/sem.h"
#include "system/draw/draw.h"
#include "system/draw/exfont.h"
#include "system/util/cpu_usage.h"
#include "system/util/err.h"
#include "system/util/fake_pthread.h"
#include "system/util/file.h"
#include "system/util/hid.h"
#include "system/util/log.h"
#include "system/util/queue.h"
#include "system/util/sync.h"
#include "system/util/thread_types.h"
#include "system/util/util.h"
#include "system/util/watch.h"
#include "video_player.h"

#define NUM_OF_CALLBACKS (uint16_t)(32)

static void Menu_hid_callback(void);
void Menu_worker_thread(void* arg);

static bool menu_thread_run = false;
static bool menu_must_exit = false;
static void (*menu_worker_thread_callbacks[NUM_OF_CALLBACKS])(void) = { 0, };
static Thread menu_worker_thread = NULL;
static Sync_data menu_callback_mutex = { 0, };

static bool Menu_top_screen_3d_from_config(const Sem_config* cfg)
{
	if(cfg->screen_mode == DEF_SEM_SCREEN_MODE_3D)
		return true;
	if(cfg->screen_mode == DEF_SEM_SCREEN_MODE_AUTO)
		return osGet3DSliderState() != 0;
	return false;
}

static uint32_t Menu_update_main_directory(void)
{
	const char* old_main_dir = "/Video_player";
	char new_main_dir[] = DEF_MENU_MAIN_DIR;
	Handle fs_handle = 0;
	FS_Archive archive = 0;
	uint32_t result = DEF_ERR_OTHER;

	new_main_dir[sizeof(new_main_dir) - 1] = 0x00;

	result = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
	if(result != DEF_SUCCESS)
	{
		result = FSUSER_OpenDirectory(&fs_handle, archive, fsMakePath(PATH_ASCII, old_main_dir));
		if(result != DEF_SUCCESS)
		{
			FSDIR_Close(fs_handle);
			result = FSUSER_RenameDirectory(archive, fsMakePath(PATH_ASCII, old_main_dir), archive, fsMakePath(PATH_ASCII, new_main_dir));
			if(result != DEF_SUCCESS)
				DEF_LOG_RESULT(FSUSER_RenameDirectory, false, result);
		}
		else
			result = DEF_SUCCESS;
	}
	else
		DEF_LOG_RESULT(FSUSER_OpenArchive, false, result);

	FSUSER_CloseArchive(archive);
	return result;
}

bool Menu_query_must_exit_flag(void)
{
	return menu_must_exit;
}

void Menu_set_must_exit_flag(bool flag)
{
	menu_must_exit = flag;
}

void Menu_resume(void) { }

void Menu_suspend(void) { }

bool Menu_add_worker_thread_callback(void (*const callback)(void))
{
	Util_sync_lock(&menu_callback_mutex, UINT64_MAX);
	for(uint16_t i = 0; i < NUM_OF_CALLBACKS; i++)
	{
		if(menu_worker_thread_callbacks[i] == callback)
			goto success;
	}
	for(uint16_t i = 0; i < NUM_OF_CALLBACKS; i++)
	{
		if(!menu_worker_thread_callbacks[i])
		{
			menu_worker_thread_callbacks[i] = callback;
			goto success;
		}
	}
	Util_sync_unlock(&menu_callback_mutex);
	return false;
success:
	Util_sync_unlock(&menu_callback_mutex);
	return true;
}

void Menu_remove_worker_thread_callback(void (*const callback)(void))
{
	Util_sync_lock(&menu_callback_mutex, UINT64_MAX);
	for(uint16_t i = 0; i < NUM_OF_CALLBACKS; i++)
	{
		if(menu_worker_thread_callbacks[i] == callback)
		{
			menu_worker_thread_callbacks[i] = NULL;
			break;
		}
	}
	Util_sync_unlock(&menu_callback_mutex);
}

void Menu_init(void)
{
	bool is_3d = false;
	uint8_t dummy = 0;
	uint32_t sync_init_result = DEF_ERR_OTHER;
	uint32_t queue_init_result = DEF_ERR_OTHER;
	uint32_t watch_init_result = DEF_ERR_OTHER;
	uint32_t result = DEF_ERR_OTHER;
	uint32_t update_main_dir_result = DEF_ERR_OTHER;
	Sem_config config = { 0, };
	Sem_state state = { 0, };

	for(uint16_t i = 0; i < NUM_OF_CALLBACKS; i++)
		menu_worker_thread_callbacks[i] = NULL;

	sync_init_result = Util_sync_init();
	queue_init_result = Util_queue_init();
	watch_init_result = Util_watch_init();
	result = Util_log_init();

	DEF_LOG_RESULT(Util_sync_init, (sync_init_result == DEF_SUCCESS), sync_init_result);
	DEF_LOG_RESULT(Util_queue_init, (queue_init_result == DEF_SUCCESS), queue_init_result);
	DEF_LOG_RESULT(Util_watch_init, (watch_init_result == DEF_SUCCESS), watch_init_result);
	DEF_LOG_RESULT(Util_log_init, (result == DEF_SUCCESS), result);
	DEF_LOG_FORMAT("Initializing...v%s", DEF_MENU_CURRENT_APP_VER);

	DEF_LOG_RESULT_SMART(result, Util_sync_create(&menu_callback_mutex, SYNC_TYPE_NON_RECURSIVE_MUTEX), (result == DEF_SUCCESS), result);

	osSetSpeedupEnable(true);
	aptSetSleepAllowed(true);
	svcSetThreadPriority(CUR_THREAD_HANDLE, (DEF_THREAD_PRIORITY_HIGH - 1));

	DEF_LOG_RESULT_SMART(result, fsInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, acInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, aptInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, mcuHwcInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, ptmuInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, romfsInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, cfguInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, amInit(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, APT_SetAppCpuTimeLimit(80), (result == DEF_SUCCESS), result);

	update_main_dir_result = Menu_update_main_directory();

	Util_file_save_to_file(".", DEF_MENU_MAIN_DIR, &dummy, 1, true);
	Util_file_save_to_file(".", DEF_MENU_MAIN_DIR "screen_recording/", &dummy, 1, true);
	Util_file_save_to_file(".", DEF_MENU_MAIN_DIR "error/", &dummy, 1, true);
	Util_file_save_to_file(".", DEF_MENU_MAIN_DIR "logs/", &dummy, 1, true);
	Util_file_save_to_file(".", DEF_MENU_MAIN_DIR "ver/", &dummy, 1, true);

	DEF_LOG_RESULT_SMART(result, Util_init(), (result == DEF_SUCCESS), result);

	Sem_init();
	Sem_suspend();
	Sem_get_config(&config);
	Sem_get_state(&state);

	is_3d = Menu_top_screen_3d_from_config(&config);

	DEF_LOG_RESULT_SMART(result, Draw_init(is_3d), (result == DEF_SUCCESS), result);

	Draw_frame_ready();
	Draw_screen_ready(DRAW_SCREEN_TOP_LEFT, DEF_DRAW_BLACK);
	if (Draw_is_3d_mode())
		Draw_screen_ready(DRAW_SCREEN_TOP_RIGHT, DEF_DRAW_BLACK);
	Draw_screen_ready(DRAW_SCREEN_BOTTOM, DEF_DRAW_BLACK);
	Draw_apply_draw();

	DEF_LOG_RESULT_SMART(result, Util_hid_init(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_hid_add_callback(Menu_hid_callback), result, result);
	DEF_LOG_RESULT_SMART(result, Exfont_init(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_err_init(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_fake_pthread_init(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_cpu_usage_init(), (result == DEF_SUCCESS), result);

	if(update_main_dir_result != DEF_SUCCESS)
	{
		const char* msg = ("/Video_player/ -> " DEF_MENU_MAIN_DIR "\nMaybe destination directory already exist?");
		Util_err_set_error_message("Failed to move app data directory.", msg, DEF_LOG_GET_SYMBOL(), update_main_dir_result);
		Util_err_set_show_flag(true);
	}

	for(uint16_t i = 0; i < DEF_EXFONT_NUM_OF_FONT_NAME; i++)
		Exfont_set_external_font_request_state(i, true);
	Exfont_request_load_external_font();

	menu_thread_run = true;
	menu_worker_thread = threadCreate(Menu_worker_thread, NULL, DEF_THREAD_STACKSIZE * 2, DEF_THREAD_PRIORITY_ABOVE_NORMAL, 0, false);

	Util_watch_add(WATCH_HANDLE_MAIN_MENU, &menu_must_exit, sizeof(menu_must_exit));

#ifdef DEF_VID_ENABLE
	Vid_init(true);
	Vid_resume();
#endif

	DEF_LOG_STRING("Initialized.");
}

void Menu_exit(void)
{
	DEF_LOG_STRING("Exiting...");
	bool draw = !aptShouldClose();
	uint32_t result = DEF_ERR_OTHER;

	menu_thread_run = false;

#ifdef DEF_VID_ENABLE
	if(Vid_query_init_flag())
		Vid_exit(draw);
#endif

	if(Sem_query_init_flag())
		Sem_exit();

	Util_hid_remove_callback(Menu_hid_callback);
	Util_hid_exit();
	Exfont_exit();
	Util_err_exit();
	Util_exit();
	Util_cpu_usage_exit();
	Util_fake_pthread_exit();

	DEF_LOG_RESULT_SMART(result, threadJoin(menu_worker_thread, DEF_THREAD_WAIT_TIME), (result == DEF_SUCCESS), result);
	threadFree(menu_worker_thread);

	Util_watch_remove(WATCH_HANDLE_MAIN_MENU, &menu_must_exit);
	Util_watch_exit();
	Util_log_exit();

	fsExit();
	acExit();
	aptExit();
	mcuHwcExit();
	ptmuExit();
	romfsExit();
	cfguExit();
	amExit();
	Draw_exit();

	Util_sync_destroy(&menu_callback_mutex);
	Util_queue_exit();
	Util_sync_exit();

	DEF_LOG_STRING("Exited.");
}

void Menu_main(void)
{
	Sem_config config = { 0, };
	Sem_get_config(&config);

	{
		bool is_3d = Menu_top_screen_3d_from_config(&config);
		if(is_3d != Draw_is_3d_mode())
		{
			uint32_t result = DEF_ERR_OTHER;
			DEF_LOG_RESULT_SMART(result, Draw_reinit(is_3d), (result == DEF_SUCCESS), result);
			Draw_set_refresh_needed(true);
		}
	}

#ifdef DEF_VID_ENABLE
	if(Vid_query_running_flag())
		Vid_main();
	else
#endif
		Menu_set_must_exit_flag(true);
}

static void Menu_hid_callback(void)
{
	Hid_info key = { 0, };
	Util_hid_query_key_state(&key);

#ifdef DEF_VID_ENABLE
	if(Vid_query_running_flag())
		Vid_hid(&key);
#endif

	/* 屏上日志已禁用
	if(Util_log_query_show_flag())
		Util_log_main(&key);
	*/
}

void Menu_worker_thread(void* arg)
{
	(void)arg;
	DEF_LOG_STRING("Thread started.");

	while(menu_thread_run)
	{
		Util_sync_lock(&menu_callback_mutex, UINT64_MAX);
		for(uint16_t i = 0; i < NUM_OF_CALLBACKS; i++)
		{
			if(menu_worker_thread_callbacks[i])
				menu_worker_thread_callbacks[i]();
		}
		Util_sync_unlock(&menu_callback_mutex);
		gspWaitForVBlank();
	}

	DEF_LOG_STRING("Thread exit.");
	threadExit(0);
}
