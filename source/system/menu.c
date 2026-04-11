/* 应用壳：平台与服务初始化，每帧调用 Vid_main；无主菜单 UI（底屏为 vid_panel）。 */

#include "system/menu.h"
#include "system/topos_setting.h" /* DEF_TOPOS_SETTING_DIR */

#include <stdbool.h>
#include <stdint.h>

#include "3ds.h"

#include "system/sem.h"
#include "system/draw/draw.h"
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
#include "app_version.h"
#include "video_player.h"

static void Menu_hid_callback(void);

static bool menu_must_exit = false;

static bool Menu_top_screen_3d_from_config(const Sem_config* cfg)
{
	if(cfg->screen_mode == DEF_SEM_SCREEN_MODE_3D)
		return true;
	if(cfg->screen_mode == DEF_SEM_SCREEN_MODE_AUTO)
		return osGet3DSliderState() != 0;
	return false;
}

bool Menu_query_must_exit_flag(void)
{
	return menu_must_exit;
}

void Menu_set_must_exit_flag(bool flag)
{
	menu_must_exit = flag;
}

void Menu_init(void)
{
	bool is_3d = false;
	uint8_t dummy = 0;
	uint32_t sync_init_result = DEF_ERR_OTHER;
	uint32_t queue_init_result = DEF_ERR_OTHER;
	uint32_t watch_init_result = DEF_ERR_OTHER;
	uint32_t result = DEF_ERR_OTHER;
	Sem_config config = { 0, };
	Sem_state state = { 0, };

	sync_init_result = Util_sync_init();
	queue_init_result = Util_queue_init();
	watch_init_result = Util_watch_init();
	result = Util_log_init();

	DEF_LOG_RESULT(Util_sync_init, (sync_init_result == DEF_SUCCESS), sync_init_result);
	DEF_LOG_RESULT(Util_queue_init, (queue_init_result == DEF_SUCCESS), queue_init_result);
	DEF_LOG_RESULT(Util_watch_init, (watch_init_result == DEF_SUCCESS), watch_init_result);
	DEF_LOG_RESULT(Util_log_init, (result == DEF_SUCCESS), result);
	DEF_LOG_FORMAT("Initializing...v%s", DEF_APP_VER_STRING);

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

	Util_file_save_to_file(".", DEF_TOPOS_SETTING_DIR, &dummy, 1, true);

	DEF_LOG_RESULT_SMART(result, Util_init(), (result == DEF_SUCCESS), result);

	Sem_init();
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
	DEF_LOG_RESULT_SMART(result, Util_err_init(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_fake_pthread_init(), (result == DEF_SUCCESS), result);
	DEF_LOG_RESULT_SMART(result, Util_cpu_usage_init(), (result == DEF_SUCCESS), result);

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

#ifdef DEF_VID_ENABLE
	if(Vid_query_init_flag())
		Vid_exit(draw);
#endif

	if(Sem_query_init_flag())
		Sem_exit();

	Util_hid_remove_callback(Menu_hid_callback);
	Util_hid_exit();
	Util_err_exit();
	Util_exit();
	Util_cpu_usage_exit();
	Util_fake_pthread_exit();

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
}
