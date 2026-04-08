#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

#include "vid_cmd.h"
#include "vid_hid.h"
#include "vid_hid_macros.h"
#include "vid_panel.h"

/* File-list nav: 300ms initial delay, then 83ms interval (12 times/sec).
 *
 * State is tracked with an absolute timestamp (osGetTime ms) rather than a
 * "range slot" counter.  This makes the repeat logic immune to how many times
 * the key was tapped before entering hold: as soon as the key has been held
 * for more than INITIAL_DELAY the first repeat fires, and then every
 * REPEAT_INTERVAL ms regardless of prior tap history.
 *
 * Two independent state structs (one per direction) avoid any cross-direction
 * contamination.
 */
#define VID_HID_NAV_INITIAL_DELAY_MS   300u
#define VID_HID_NAV_REPEAT_INTERVAL_MS  83u

typedef struct {
	bool     active;       /* key is currently being held */
	uint64_t next_fire_ms; /* absolute time of next repeat event */
} NavRepeatState;

static NavRepeatState s_nav_up    = { false, 0 };
static NavRepeatState s_nav_down  = { false, 0 };

/* Settings panel: 独立的四方向重复状态 */
static NavRepeatState s_set_up    = { false, 0 };
static NavRepeatState s_set_down  = { false, 0 };
static NavRepeatState s_set_left  = { false, 0 };
static NavRepeatState s_set_right = { false, 0 };

static void vid_hid_nav_interval(Hid_key key_val,
                                 NavRepeatState *st,
                                 bool *fire_now)
{
	*fire_now = false;

	/* Use the physical hold state (was_active && is_active) instead of the
	 * logical HID_STATE_HELD.  The HID state machine goes through intermediate
	 * states (PRESSED_WAIT_RELEASE → NONE → PRESSED_WAIT_HOLD_THRESHOLD) when
	 * the user taps rapidly and then holds without releasing, so DEF_HID_HE
	 * would falsely report "not held" for several frames and reset st->active,
	 * preventing the repeat from ever firing.  DEF_HID_PHY_HE only looks at
	 * was_active && is_active and is completely immune to state-machine resets. */
	if(!DEF_HID_PHY_HE(key_val))
	{
		/* Key physically released — reset so next hold starts fresh. */
		st->active       = false;
		st->next_fire_ms = 0;
		return;
	}

	/* Key is physically held. */
	uint64_t now = osGetTime();

	if(!st->active)
	{
		/* Transition into hold: schedule first repeat after initial delay. */
		st->active       = true;
		st->next_fire_ms = now + VID_HID_NAV_INITIAL_DELAY_MS;
		return;
	}

	/* Already in hold — fire if we've reached the next scheduled time. */
	if(now >= st->next_fire_ms)
	{
		*fire_now = true;
		st->next_fire_ms += VID_HID_NAV_REPEAT_INTERVAL_MS;
		/* Guard against falling too far behind (e.g. after a very long frame). */
		if(st->next_fire_ms < now)
			st->next_fire_ms = now + VID_HID_NAV_REPEAT_INTERVAL_MS;
	}
}

static bool vid_push(VidCmdId id)
{
	return Vid_cmd_push((VidCmd){ .id = id, .iarg = 0, .uarg = 0, .darg0 = 0, .darg1 = 0 });
}

static bool vid_push_i(VidCmdId id, int32_t iarg)
{
	return Vid_cmd_push((VidCmd){ .id = id, .iarg = iarg, .uarg = 0, .darg0 = 0, .darg1 = 0 });
}

void Vid_hid_enqueue(const Hid_info *key, const VidHidLayout *layout, const VidHidRouterState *rs, const VidHidUiLocks *locks)
{
	const Hid_info *k = key;
	const VidHidLayout *L = layout;

	(void)L;
	(void)locks;

	if(rs->is_full_screen)
	{
		if(VID_HID_FULL_EXIT_CFM(*k) || aptShouldJumpToHome())
			(void)vid_push(VID_CMD_FULLSCREEN_EXIT);
		else if(VID_HID_FULL_TOGGLE_PLAYBACK_CFM(*k))
			(void)vid_push(VID_CMD_FULLSCREEN_TOGGLE_PLAY);
	}
	else if(rs->panel == VID_PANEL_FILES && Vid_panel_files_play_error_is_visible())
	{
		Vid_panel_files_play_error_hid(k);
	}
	else if(rs->panel != VID_PANEL_PLAYER)
	{
		/* ── Panel UI input routing (FILES / SETTING panels) ──
		 * 触摸事件对所有非Player面板统一处理，放在最前面；
		 * 键盘在触摸之后：与三界面对齐（X/Y/Start），不经过 R 键菜单。
		 * 两者互不干扰：触摸和键盘在同一帧不会同时触发。 */

		/* Touch: same routing for FILES and SETTING */
		if(DEF_HID_PHY_PR(k->touch))
		{
			(void)Vid_cmd_push((VidCmd){
				.id   = VID_CMD_PANEL_LIST_PRESS,
				.uarg = vid_panel_pack_xy(k->touch_x, k->touch_y)
			});
		}
		else if(DEF_HID_PHY_HE(k->touch))
		{
			(void)Vid_cmd_push((VidCmd){
				.id   = VID_CMD_PANEL_LIST_SCROLL,
				.uarg = vid_panel_pack_xy(k->touch_x, k->touch_y)
			});
		}
		/* 必须与 Player 面板一致用物理松手：长按拖滚动条后状态为 HELD_DONE 等，
		 * DEF_HID_PR_EM(PRESSED) 不会触发，导致 LIST_RELEASE 丢失、s_fs_drag/s_set_drag 不归零。 */
		else if(DEF_HID_PHY_RE(k->touch))
		{
			(void)Vid_cmd_push((VidCmd){
				.id   = VID_CMD_PANEL_LIST_RELEASE,
				.uarg = vid_panel_pack_xy(k->touch_x, k->touch_y)
			});
		}
		/* Keyboard: B key goes back on both FILES and SETTING */
		else if(VID_HID_ABORT_PLAYBACK_CFM(*k))
		{
			(void)vid_push(VID_CMD_PANEL_BACK);
		}
		/* Keyboard：三界面对齐 X — FILES 上 X 回 Player */
		else if(rs->panel == VID_PANEL_FILES && VID_HID_PANEL_GO_FILES_CFM(*k))
		{
			(void)vid_push(VID_CMD_PANEL_GO_PLAYER);
		}
		/* Start：设置 -> 进入设置前的面板；文件管理 -> 设置 */
		else if(rs->panel == VID_PANEL_SETTING && VID_HID_PANEL_GO_SETTING_CFM(*k))
		{
			(void)vid_push(VID_CMD_PANEL_LEAVE_SETTING);
		}
		else if(rs->panel == VID_PANEL_FILES && VID_HID_PANEL_GO_SETTING_CFM(*k))
		{
			(void)vid_push(VID_CMD_PANEL_GO_SETTING);
		}
		/* 设置面板 A：仅进入子菜单行；开关/选项/滑块须用左右键改值 */
		else if(rs->panel == VID_PANEL_SETTING && (DEF_HID_PR_EM(k->a, 1) || DEF_HID_HD(k->a)))
		{
			(void)vid_push(VID_CMD_SET_CONFIRM);
		}
		/* ── Settings panel: 十字键 ──
		 * 上下：移动选中行（press-down 即发 + 长按重复）
		 * 左右：当前行值 ±1（press-down 即发 + 长按重复） */
		else if(rs->panel == VID_PANEL_SETTING)
		{
			bool set_up_fire    = false;
			bool set_down_fire  = false;
			bool set_left_fire  = false;
			bool set_right_fire = false;

			vid_hid_nav_interval(k->d_up,    &s_set_up,    &set_up_fire);
			vid_hid_nav_interval(k->d_down,  &s_set_down,  &set_down_fire);
			vid_hid_nav_interval(k->d_left,  &s_set_left,  &set_left_fire);
			vid_hid_nav_interval(k->d_right, &s_set_right, &set_right_fire);

			if(DEF_HID_PHY_PR(k->d_up) || set_up_fire)
				(void)vid_push(VID_CMD_SET_NAV_UP);
			else if(DEF_HID_PHY_PR(k->d_down) || set_down_fire)
				(void)vid_push(VID_CMD_SET_NAV_DOWN);
			else if(DEF_HID_PHY_PR(k->d_left) || set_left_fire)
				(void)vid_push(VID_CMD_SET_VALUE_LEFT);
			else if(DEF_HID_PHY_PR(k->d_right) || set_right_fire)
				(void)vid_push(VID_CMD_SET_VALUE_RIGHT);
		}
		else if(rs->panel == VID_PANEL_FILES && (DEF_HID_PR_EM(k->a, 1) || DEF_HID_HD(k->a)))
		{
			/* A key: open selected item */
			(void)vid_push(VID_CMD_PANEL_CONFIRM);
		}
		else if(rs->panel == VID_PANEL_FILES)
		{
			bool nav_up_repeat   = false;
			bool nav_down_repeat = false;
			vid_hid_nav_interval(k->d_up,   &s_nav_up,   &nav_up_repeat);
			vid_hid_nav_interval(k->d_down, &s_nav_down, &nav_down_repeat);

			if(DEF_HID_PHY_PR(k->d_up) || nav_up_repeat)
				(void)vid_push(VID_CMD_PANEL_NAV_UP);
			else if(DEF_HID_PHY_PR(k->d_down) || nav_down_repeat)
				(void)vid_push(VID_CMD_PANEL_NAV_DOWN);
			else if(DEF_HID_PHY_PR(k->d_left) || DEF_HID_HE_MT(k->d_left, 300))
				(void)vid_push(VID_CMD_PANEL_NAV_PAGE_UP);
			else if(DEF_HID_PHY_PR(k->d_right) || DEF_HID_HE_MT(k->d_right, 300))
				(void)vid_push(VID_CMD_PANEL_NAV_PAGE_DOWN);
		}
	}
	else
	{
		/* ── PLAYER：触控走 panel；键盘保留三界面对齐快捷键（无 L/R 肩键菜单）── */
		if(DEF_HID_PHY_PR(k->touch))
		{
			(void)Vid_cmd_push((VidCmd){
				.id   = VID_CMD_PANEL_LIST_PRESS,
				.uarg = vid_panel_pack_xy(k->touch_x, k->touch_y)
			});
		}
		else if(DEF_HID_PHY_HE(k->touch))
		{
			(void)Vid_cmd_push((VidCmd){
				.id   = VID_CMD_PANEL_LIST_SCROLL,
				.uarg = vid_panel_pack_xy(k->touch_x, k->touch_y)
			});
		}
		else if(DEF_HID_PHY_RE(k->touch))
		{
			(void)Vid_cmd_push((VidCmd){
				.id   = VID_CMD_PANEL_LIST_RELEASE,
				.uarg = vid_panel_pack_xy(k->touch_x, k->touch_y)
			});
		}
		else
		{
			if(VID_HID_ENTER_FULL_CFM(*k))
				(void)vid_push(VID_CMD_ENTER_FULLSCREEN);
			else if(VID_HID_TOGGLE_PLAYBACK_CFM(*k))
				(void)vid_push(VID_CMD_TOGGLE_PLAY);
			else if(VID_HID_ABORT_PLAYBACK_CFM(*k))
				(void)vid_push(VID_CMD_ABORT);
			else if(VID_HID_PANEL_GO_FILES_CFM(*k))
				(void)vid_push(VID_CMD_PANEL_GO_FILES);
			else if(VID_HID_PANEL_GO_SETTING_CFM(*k))
				(void)vid_push(VID_CMD_PANEL_GO_SETTING);
			else if(VID_HID_PANEL_QUICK_MENU_CFM(*k))
				(void)vid_push(VID_CMD_PANEL_TOGGLE_QUICK_MENU);
		}
	}
}

void Vid_hid_enqueue_seek(const Hid_info *key, const VidHidLayout *layout, const VidHidRouterState *rs, const VidHidUiLocks *locks)
{
	const Hid_info *k = key;

	if(rs->state == PLAYER_STATE_IDLE || rs->state == PLAYER_STATE_PREPARE_PLAYING)
		return;

	/* Touch seek on the Player panel bottom screen is handled entirely by
	 * vid_panel.c (PANEL_LIST_PRESS / SCROLL / RELEASE).
	 * This function only handles fullscreen d-pad seek and the legacy
	 * locks-based seek path for fullscreen mode. */
	if(rs->is_full_screen)
	{
		if(VID_HID_SEEK_BAR_PRE_CFM(*k, *layout, *locks))
			(void)vid_push_i(VID_CMD_SEEK_BAR_DRAG, (int32_t)k->touch_x);
		else if(VID_HID_SEEK_BAR_CFM(*k, *layout, *locks))
			(void)vid_push(VID_CMD_SEEK_BAR_COMMIT);
		else if(VID_HID_FULL_SEEK_FWD_CFM(*k))
			(void)vid_push(VID_CMD_SEEK_BUTTON_FWD);
		else if(VID_HID_FULL_SEEK_BACK_CFM(*k))
			(void)vid_push(VID_CMD_SEEK_BUTTON_BACK);
	}
	else if(rs->panel == VID_PANEL_PLAYER)
	{
		/* d-pad seek buttons only (touch is handled by panel system) */
		if(VID_HID_FULL_SEEK_FWD_CFM(*k))
			(void)vid_push(VID_CMD_SEEK_BUTTON_FWD);
		else if(VID_HID_FULL_SEEK_BACK_CFM(*k))
			(void)vid_push(VID_CMD_SEEK_BUTTON_BACK);
	}

	(void)layout;
	(void)locks;
}
