#if !defined(DEF_VID_HID_H)
#define DEF_VID_HID_H

#include <stdbool.h>
#include <stdint.h>

#include "system/util/hid_types.h"
#include "vid_state.h"
#include "vid_panel.h"

typedef struct
{
	uint8_t _unused;
} VidHidLayout;

typedef struct
{
	bool seek_bar_selected;
} VidHidUiLocks;

typedef struct
{
	/* Player 且底屏息：与 `!Sem_config.is_bottom_lcd_on` 同义，供输入路由；非第二份状态。 */
	bool player_bottom_off;
	Vid_player_main_state state;
	Vid_player_sub_state sub_state;
	double scroll_speed;
	bool is_new_3ds;
	Vid_panel panel;
} VidHidRouterState;

void Vid_hid_enqueue(const Hid_info *key, const VidHidLayout *layout, const VidHidRouterState *rs, const VidHidUiLocks *locks);

void Vid_hid_enqueue_seek(const Hid_info *key, const VidHidLayout *layout, const VidHidRouterState *rs, const VidHidUiLocks *locks);

#endif
