#if !defined(DEF_VID_HID_MACROS_H)
#define DEF_VID_HID_MACROS_H

#include <stdlib.h>

#include "vid_seekbar.h"

#define VID_HID_ENTER_FULL_CFM(k)						(bool)(DEF_HID_PR_EM((k).select, 1) || DEF_HID_HD((k).select))
#define VID_HID_TOGGLE_PLAYBACK_CFM(k)					(bool)(DEF_HID_PR_EM((k).a, 1) || DEF_HID_HD((k).a))
#define VID_HID_ABORT_PLAYBACK_CFM(k)					(bool)(DEF_HID_PR_EM((k).b, 1) || DEF_HID_HD((k).b))
#define VID_HID_PANEL_GO_FILES_CFM(k)					(bool)(DEF_HID_PR_EM((k).x, 1) || DEF_HID_HD((k).x))
#define VID_HID_PANEL_GO_SETTING_CFM(k)					(bool)(DEF_HID_PR_EM((k).start, 1) || DEF_HID_HD((k).start))
#define VID_HID_PANEL_QUICK_MENU_CFM(k)					(bool)(DEF_HID_PR_EM((k).y, 1) || DEF_HID_HD((k).y))
#define VID_HID_SEEK_BAR_SEL(k, L)							(bool)(DEF_HID_PHY_PR((k).touch) && VidSeekBar_hit_test((k).touch_x, (k).touch_y))
#define VID_HID_SEEK_BAR_PRE_CFM(k, L, locks)						(bool)(VID_HID_SEEK_BAR_SEL((k), (L)) || (DEF_HID_PHY_HE((k).touch) && (locks).seek_bar_selected))
#define VID_HID_SEEK_BAR_CFM(k, L, locks)							(bool)(DEF_HID_PHY_RE((k).touch) && (locks).seek_bar_selected)
#define VID_HID_SEEK_BAR_DESEL(k)						(bool)(DEF_HID_PHY_NP((k).touch))
#define VID_HID_FULL_EXIT_CFM(k)						(bool)(DEF_HID_PR_EM((k).select, 1) || DEF_HID_HD((k).select) || DEF_HID_PR_EM((k).touch, 1) || DEF_HID_HD((k).touch))
#define VID_HID_FULL_TOGGLE_PLAYBACK_CFM(k)				(bool)(DEF_HID_PR_EM((k).a, 1) || DEF_HID_HD((k).a))
#define VID_HID_FULL_SEEK_BACK_CFM(k)					(bool)(DEF_HID_PR_EM((k).d_left, 1) || DEF_HID_HD((k).d_left))
#define VID_HID_FULL_SEEK_FWD_CFM(k)					(bool)(DEF_HID_PR_EM((k).d_right, 1) || DEF_HID_HD((k).d_right))

#endif
