#if !defined(DEF_VID_CMD_H)
#define DEF_VID_CMD_H

#include <stdbool.h>
#include <stdint.h>

#define VID_CMD_QUEUE_CAP		(uint32_t)(64)

typedef enum
{
	VID_CMD_NONE = 0,

	VID_CMD_FULLSCREEN_EXIT,
	VID_CMD_FULLSCREEN_TOGGLE_PLAY,
	VID_CMD_FULLSCREEN_SEEK_BACK,
	VID_CMD_FULLSCREEN_SEEK_FWD,

	VID_CMD_ENTER_FULLSCREEN,
	VID_CMD_TOGGLE_PLAY,
	VID_CMD_ABORT,
	VID_CMD_TOUCH_EMPTY_FULLSCREEN,

	VID_CMD_SEEK_BAR_DRAG,
	VID_CMD_SEEK_BAR_COMMIT,
	VID_CMD_SEEK_BUTTON_BACK,
	VID_CMD_SEEK_BUTTON_FWD,

	VID_CMD_SCROLL_BAR_JUMP,
	VID_CMD_SCROLL_TOUCH_DELTA,

	/* ── Three-panel UI commands ── */
	VID_CMD_PANEL_GO_FILES,
	VID_CMD_PANEL_GO_PLAYER,
	VID_CMD_PANEL_GO_SETTING,
	VID_CMD_PANEL_LEAVE_SETTING, /* Start / toggle：回到进入设置前的面板 */
	VID_CMD_PANEL_BACK,
	VID_CMD_PANEL_LIST_PRESS,         /* uarg = packed (py<<16|px) */
	VID_CMD_PANEL_LIST_SCROLL,        /* uarg = packed (py<<16|px) */
	VID_CMD_PANEL_LIST_RELEASE,       /* uarg = packed (py<<16|px) */
	VID_CMD_PANEL_TOGGLE_QUICK_MENU,  /* Y key quick-menu overlay */
	VID_CMD_PANEL_NAV_UP,             /* iarg = step (1 or accel) */
	VID_CMD_PANEL_NAV_DOWN,           /* iarg = step (1 or accel) */
	VID_CMD_PANEL_NAV_PAGE_UP,        /* jump up   11 rows */
	VID_CMD_PANEL_NAV_PAGE_DOWN,      /* jump down 11 rows */
	VID_CMD_PANEL_CONFIRM,            /* A key: open selected item */

	/* ── Settings panel keyboard navigation ── */
	VID_CMD_SET_NAV_UP,               /* 上键：移动选中行向上 */
	VID_CMD_SET_NAV_DOWN,             /* 下键：移动选中行向下 */
	VID_CMD_SET_VALUE_LEFT,           /* 左键：当前行值 -1（循环/滑块） */
	VID_CMD_SET_VALUE_RIGHT,          /* 右键：当前行值 +1（循环/滑块） */
	VID_CMD_SET_CONFIRM,              /* 设置面板 A：仅打开子菜单行；改值用左右键 */
} VidCmdId;

typedef struct
{
	VidCmdId id;
	int32_t iarg;
	uint32_t uarg;
	double darg0;
	double darg1;
} VidCmd;

void Vid_cmd_queue_reset(void);
bool Vid_cmd_push(VidCmd cmd);
bool Vid_cmd_pop(VidCmd *out);
uint32_t Vid_cmd_count(void);

#endif
