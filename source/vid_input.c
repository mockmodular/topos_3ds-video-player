//Includes.
#include "video_player.h"
#include "vid_state.h"
#include "vid_screen.h"
#include "vid_texture.h"
#include "vid_worker.h"
#include "vid_cmd.h"
#include "vid_hid.h"
#include "vid_seekbar.h"
#include "vid_seek_engine.h"
#include "vid_panel_layout.h"

#include <3ds.h>

#include "system/draw/draw.h"
#include "system/sem.h"
#include "system/util/err.h"
#include "system/util/hid.h"
#include "system/util/log.h"
#include "system/util/util.h"
#include <stdlib.h>

/* 底屏进度条命中（用于 fullscreen 拖条时 locks.seek_bar_selected） */
#define HID_SEEK_BAR_SEL(k)							(bool)(DEF_HID_PHY_PR((k).touch) && VidSeekBar_hit_test((k).touch_x, (k).touch_y))
#define HID_SEEK_BAR_DESEL(k)						(bool)(DEF_HID_PHY_NP((k).touch))

static void vid_fill_layout(VidHidLayout *out)
{
	out->_unused = 0;
}

static void vid_fill_router_state(VidHidRouterState *out, bool is_new_3ds, double scroll_speed)
{
	out->is_full_screen = vid_player.is_full_screen;
	out->state = vid_player.state;
	out->sub_state = vid_player.sub_state;
	out->scroll_speed = scroll_speed;
	out->is_new_3ds = is_new_3ds;
	out->panel = vid_player.panel;
}

static void vid_fill_locks(VidHidUiLocks *out)
{
	out->seek_bar_selected = vid_player.seek_bar.selected;
}

static void Vid_process_hid_cmd_queue(void)
{
	uint32_t result = DEF_ERR_OTHER;
	VidCmd cmd;
	bool any_cmd = false;

	while(Vid_cmd_pop(&cmd))
	{
		any_cmd = true;
		switch(cmd.id)
		{
			case VID_CMD_FULLSCREEN_EXIT:
				for(uint32_t i = 0; i < EYE_MAX; i++)
					Vid_fit_to_screen(NON_FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i);
				Vid_exit_full_screen();
				Util_hid_reset_key_state(HID_KEY_BIT_ALL);
				break;

			case VID_CMD_FULLSCREEN_TOGGLE_PLAY:
			case VID_CMD_TOGGLE_PLAY:
				if(vid_player.state == PLAYER_STATE_IDLE)
				{
					if(vid_player.file.name[0] != '\0')
					{
						DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_PLAY_REQUEST,
						NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
					}
				}
				else if(vid_player.state == PLAYER_STATE_PLAYING
				|| ((vid_player.sub_state & PLAYER_SUB_STATE_RESUME_LATER) && (vid_player.state == PLAYER_STATE_BUFFERING
				|| vid_player.state == PLAYER_STATE_PREPARE_SEEKING || vid_player.state == PLAYER_STATE_SEEKING)))
				{
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_PAUSE_REQUEST,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
				}
				else if(vid_player.state == PLAYER_STATE_PAUSE
				|| (!(vid_player.sub_state & PLAYER_SUB_STATE_RESUME_LATER) && (vid_player.state == PLAYER_STATE_BUFFERING
				|| vid_player.state == PLAYER_STATE_PREPARE_SEEKING || vid_player.state == PLAYER_STATE_SEEKING)))
				{
					DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_RESUME_REQUEST,
					NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_DO_NOT_ADD_IF_EXIST), (result == DEF_SUCCESS), result);
				}
				break;

			case VID_CMD_ENTER_FULLSCREEN:
			case VID_CMD_TOUCH_EMPTY_FULLSCREEN:
				for(uint32_t i = 0; i < EYE_MAX; i++)
					Vid_fit_to_screen(FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i);
				Vid_enter_full_screen(1);
				Util_hid_reset_key_state(HID_KEY_BIT_ALL);
				break;

			case VID_CMD_ABORT:
				DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_ABORT_REQUEST,
				NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_SEND_TO_FRONT), (result == DEF_SUCCESS), result);
				break;

			case VID_CMD_SEEK_BAR_DRAG:
				VidSeekEngine_on_bar_drag((int16_t)cmd.iarg);
				break;

			case VID_CMD_SEEK_BAR_COMMIT:
				VidSeekEngine_on_bar_commit();
				break;

			case VID_CMD_SEEK_BUTTON_FWD:
				VidSeekEngine_on_step_fwd();
				if(vid_player.is_full_screen)
					Vid_exit_full_screen();
				break;

			case VID_CMD_SEEK_BUTTON_BACK:
				VidSeekEngine_on_step_back();
				if(vid_player.is_full_screen)
					Vid_exit_full_screen();
				break;

			/* ── Three-panel UI ── */
			case VID_CMD_PANEL_GO_FILES:
				Vid_panel_go_files();
				Util_hid_reset_key_state(HID_KEY_BIT_ALL);
				break;

			case VID_CMD_PANEL_GO_PLAYER:
				Vid_panel_go_player();
				Util_hid_reset_key_state(HID_KEY_BIT_ALL);
				break;

			case VID_CMD_PANEL_GO_SETTING:
				Vid_panel_go_settings();
				Util_hid_reset_key_state(HID_KEY_BIT_ALL);
				break;

			case VID_CMD_PANEL_LEAVE_SETTING:
				Vid_panel_leave_settings();
				Util_hid_reset_key_state(HID_KEY_BIT_ALL);
				break;

			case VID_CMD_PANEL_BACK:
				Vid_panel_back();
				Util_hid_reset_key_state(HID_KEY_BIT_ALL);
				break;

			case VID_CMD_PANEL_LIST_PRESS:
			{
				int px, py;
				vid_panel_unpack_xy(cmd.uarg, &px, &py);
				Vid_panel_list_press(px, py);
				Vid_panel_list_press_flash(px, py);
				break;
			}

			case VID_CMD_PANEL_LIST_SCROLL:
			{
				int px, py;
				vid_panel_unpack_xy(cmd.uarg, &px, &py);
				Vid_panel_list_scroll(px, py);
				break;
			}

			case VID_CMD_PANEL_LIST_RELEASE:
			{
				int px, py;
				vid_panel_unpack_xy(cmd.uarg, &px, &py);
				Vid_panel_list_release(px, py);
				break;
			}

		case VID_CMD_PANEL_TOGGLE_QUICK_MENU:
			Vid_panel_toggle_quick_menu();
			break;

		case VID_CMD_PANEL_TOGGLE_PLAYER_FILES:
			Vid_panel_toggle_player_files();
			Util_hid_reset_key_state(HID_KEY_BIT_ALL);
			break;

		case VID_CMD_PANEL_NAV_UP:
			Vid_panel_nav_select(-1);
			break;

		case VID_CMD_PANEL_NAV_DOWN:
			Vid_panel_nav_select(1);
			break;

		case VID_CMD_PANEL_NAV_PAGE_UP:
			Vid_panel_nav_select(-11);
			break;

		case VID_CMD_PANEL_NAV_PAGE_DOWN:
			Vid_panel_nav_select(11);
			break;

		case VID_CMD_PANEL_CONFIRM:
			Vid_panel_confirm();
			break;

		/* ── Settings panel keyboard navigation ── */
		case VID_CMD_SET_NAV_UP:
			Vid_panel_settings_kbd_nav(-1);
			break;

		case VID_CMD_SET_NAV_DOWN:
			Vid_panel_settings_kbd_nav(1);
			break;

		case VID_CMD_SET_VALUE_LEFT:
			Vid_panel_settings_kbd_adjust(-1);
			break;

		case VID_CMD_SET_VALUE_RIGHT:
			Vid_panel_settings_kbd_adjust(1);
			break;

		case VID_CMD_SET_CONFIRM:
			Vid_panel_settings_kbd_submenu_a();
			break;

		default:
			break;
		}
	}

	if(any_cmd)
		Draw_set_refresh_needed(true);
}

void Vid_hid(const Hid_info* key)
{
	uint32_t result = DEF_ERR_OTHER;
	Sem_config config = { 0, };
	Sem_state state = { 0, };

	if(!key)
		return;

	if(vid_player.is_setting_volume || vid_player.is_setting_seek_duration)
		return;

	Sem_get_config(&config);
	Sem_get_state(&state);

	/* Select：任意面板下立刻关底屏；再按 Select 或触摸底屏唤醒 */
	if(DEF_HID_PHY_PR(key->select))
	{
		if(config.is_bottom_lcd_on)
		{
			config.is_bottom_lcd_on = false;
			vid_player.bottom_lcd_select_sleep = true;
		}
		else
		{
			config.is_bottom_lcd_on = true;
			vid_player.bottom_lcd_select_sleep = false;
		}
		Sem_set_config(&config);
		vid_player.auto_full_screen_count = 0;
		vid_player.turn_off_bottom_screen_count = 0;
		Draw_set_refresh_needed(true);
		Util_hid_reset_key_state(HID_KEY_BIT_SELECT);
		return;
	}

	if(vid_player.bottom_lcd_select_sleep && !config.is_bottom_lcd_on)
	{
		if((DEF_HID_PHY_PR(key->touch) || DEF_HID_PHY_HE(key->touch))
		&& key->touch_x >= 0 && key->touch_y >= 0
		&& key->touch_x < VP_SCREEN_W && key->touch_y < VP_SCREEN_H)
		{
			config.is_bottom_lcd_on = true;
			Sem_set_config(&config);
			vid_player.bottom_lcd_select_sleep = false;
			Draw_set_refresh_needed(true);
			return;
		}
	}

	if(aptShouldJumpToHome())
	{
		if(vid_player.is_waiting_home_menu)
			return;//Nothing to do.

		vid_player.is_waiting_home_menu = true;

		if(vid_player.state == PLAYER_STATE_PREPARE_PLAYING || vid_player.state == PLAYER_STATE_PLAYING
		|| (vid_player.sub_state & PLAYER_SUB_STATE_RESUME_LATER))
		{
			//Only resume video if we are playing, about to start playing or resume flag is set.
			vid_player.must_resume_after_home_menu = true;
		}

		//Always pause the video just in case.
		DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_PAUSE_REQUEST,
		NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);

		if(vid_player.is_full_screen)
		{
			//Exit full-screen to avoid bottom LCD blackout.
			for(uint32_t i = 0; i < EYE_MAX; i++)
				Vid_fit_to_screen(NON_FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, i);

			Vid_exit_full_screen();
		}

		//Wait for it.
		while(vid_player.state == PLAYER_STATE_PREPARE_PLAYING || vid_player.state == PLAYER_STATE_PLAYING)
			Util_sleep(10000);

		return;
	}
	else
	{
		if(vid_player.must_resume_after_home_menu)
		{
			//Resume the video.
			DEF_LOG_RESULT_SMART(result, Util_queue_add(&vid_player.decode_thread_command_queue, DECODE_THREAD_RESUME_REQUEST,
			NULL, QUEUE_OP_TIMEOUT_US, QUEUE_OPTION_NONE), (result == DEF_SUCCESS), result);
		}

		vid_player.is_waiting_home_menu = false;
		vid_player.must_resume_after_home_menu = false;
	}

	if(Util_err_query_show_flag())
		Util_err_main(key);
	else
	{
		if(!vid_player.is_full_screen && vid_player.panel == VID_PANEL_PLAYER
		&& vid_player.state != PLAYER_STATE_IDLE && vid_player.state != PLAYER_STATE_PREPARE_PLAYING)
		{
			if(HID_SEEK_BAR_SEL(*key))
				vid_player.seek_bar.selected = true;
		}

		//Execute commands via enqueue→process queue.
		/* 自动 5 秒息屏：仅 Player 下屏 chrome/进度条/拖进度中的触摸，或十字键，才清零计时 */
		if(!vid_player.is_full_screen && vid_player.panel == VID_PANEL_PLAYER)
		{
			if(DEF_HID_PHY_PR(key->d_up) || DEF_HID_PHY_PR(key->d_down)
			|| DEF_HID_PHY_PR(key->d_left) || DEF_HID_PHY_PR(key->d_right))
				vid_player.auto_full_screen_count = 0;
			else if((DEF_HID_PHY_HE(key->touch) || DEF_HID_PHY_PR(key->touch))
			&& Vid_panel_player_touch_resets_auto_dim(key->touch_x, key->touch_y))
				vid_player.auto_full_screen_count = 0;
		}

		{
			VidHidLayout layout = { 0, };
			VidHidRouterState rs = { 0, };
			VidHidUiLocks locks = { 0, };

			vid_fill_layout(&layout);
			vid_fill_router_state(&rs, DEF_SEM_MODEL_IS_NEW(state.console_model), config.scroll_speed);
			vid_fill_locks(&locks);

			Vid_cmd_queue_reset();
			Vid_hid_enqueue(key, &layout, &rs, &locks);
			Vid_process_hid_cmd_queue();

			vid_fill_router_state(&rs, DEF_SEM_MODEL_IS_NEW(state.console_model), config.scroll_speed);
			vid_fill_locks(&locks);
			Vid_cmd_queue_reset();
			Vid_hid_enqueue_seek(key, &layout, &rs, &locks);
			Vid_process_hid_cmd_queue();
		}
	}

	if(HID_SEEK_BAR_DESEL(*key))
		vid_player.seek_bar.selected = false;

	/* 屏上日志输入已禁用
	if(Util_log_query_show_flag())
		Util_log_main(key);
	*/
}
