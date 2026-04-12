#pragma once

#include <stddef.h>
#include <stdbool.h>

/* ── Row kinds ── */
typedef enum {
    VP_SETTING_ROW_TOGGLE,    /* 2 chips: Off / On */
    VP_SETTING_ROW_CHOICE,    /* 2..4 chips: multi-choice */
    VP_SETTING_ROW_SLIDER,    /* horizontal drag bar */
    VP_SETTING_ROW_SUBMENU,   /* label + ">" arrow — enters a sub-page */
} VpSettingRowKind;

/* ── Sub-page IDs ── */
typedef enum {
    VP_SUBPAGE_ROOT     = 0,  /* top-level: volume, custom, video, advanced, ui mod */
    VP_SUBPAGE_VIDEO    = 1,
    VP_SUBPAGE_ADVANCED = 2,
    VP_SUBPAGE_CUSTOM   = 3,
} VpSubpage;

/* ── Stable row IDs ── */
typedef enum {
    VP_SETTING_ID_AUTO_DIM_5S       = 0,
    VP_SETTING_ID_SCREEN_MODE       = 6,
    VP_SETTING_ID_ECO_MODE          = 7,
    VP_SETTING_ID_ADV_HW_COLOR      = 8, /* Advanced only: VID_HW_CONV_* */
    VP_SETTING_ID_ADV_HW_DECODE     = 9, /* Advanced: 允许尝试 MVD（自动回退软解）；不在 Switch 子菜单 */
    VP_SETTING_ID_FAKE_MODEL        = 10,
    VP_SETTING_ID_ENTER_VIDEO       = 20,
    VP_SETTING_ID_ENTER_ADVANCED    = 21,
    VP_SETTING_ID_ENTER_CUSTOM      = 23,

    VP_SETTING_ID_SW_VOLUME           = 30,
    VP_SETTING_ID_SW_SEEK_DURATION    = 31,
    VP_SETTING_ID_SW_TEX_FILTER       = 33,
    VP_SETTING_ID_SW_MULTI_DECODE     = 34, /* 开/关多线程解码；线程数等数值项不在此菜单 */
    VP_SETTING_ID_SW_SBS_SWAP         = 35,
    VP_SETTING_ID_VIDEO_SCALE         = 36, /* Video submenu: pixel-perfect vs fit */
    VP_SETTING_ID_SW_SE1_NO_AUDIO     = 40,
    VP_SETTING_ID_SW_SE1_NO_VIDEO     = 41,
    VP_SETTING_ID_FS_BROWSER_ROOT     = 42, /* Custom: browse root — movies folder vs SD root */
    VP_SETTING_ID_UI_MOD              = 43, /* Root: full bottom info vs minimal */
} VpSettingId;

#define VP_SETTING_MAX_OPTS  4
#define VP_SETTINGS_MAX_ROWS 16

/* Static row descriptor — data only, no coordinates */
typedef struct {
    VpSettingRowKind  kind;
    VpSettingId       id;
    const char       *label;
    int               num_opts;
    const char       *opt_labels[VP_SETTING_MAX_OPTS];
    int               slider_min;
    int               slider_max;
} VpSettingRowDesc;

/* Live view snapshot passed to the renderer each frame */
typedef struct {
    int         row_count;
    int         selected_row;
    int         values[VP_SETTINGS_MAX_ROWS];
    int         scroll_offset;    /* pixels scrolled down from top of content */
    int         total_height;     /* virtual height of all rows in pixels */
    int         content_h;        /* visible content height (VP_SET_CONTENT_H) */
    VpSubpage   subpage;          /* which page is showing */
} VpSettingsView;

/* ── Public API ── */
void vid_panel_settings_init(void);

int                      vid_panel_settings_row_count(void);
const VpSettingRowDesc  *vid_panel_settings_row(int i);

void vid_panel_settings_fill_view(VpSettingsView *out);

/* Apply an explicit value to a row (from touch chip/slider) */
void vid_panel_settings_apply_change(int row, int value);

/* Slider track: map screen X to value (geometry matches draw + resolve_touch). */
int vid_panel_settings_slider_value_at_x(int px, int row);

/* Continuous drag: defer Vid_save_settings until end/abort (volume / seek step sliders). */
void vid_panel_settings_slider_drag_begin(void);
void vid_panel_settings_slider_drag_end(void);
void vid_panel_settings_slider_drag_abort(void);

/* Step current value by ±delta (keyboard left/right) */
void vid_panel_settings_adjust(int row, int delta);

/* Keyboard up/down navigation */
void vid_panel_settings_move_selection(int delta);
void vid_panel_settings_reset_selection(void);
int  vid_panel_settings_get_selected(void);

/* Navigate into / out of a submenu */
void vid_panel_settings_enter_subpage(VpSubpage page);
/* If row is VP_SETTING_ROW_SUBMENU, enter the corresponding sub-page; no-op otherwise */
void vid_panel_settings_open_submenu_row(int row);
void vid_panel_settings_go_back(void);
bool vid_panel_settings_can_go_back(void);

/* Scroll control (called from touch handlers in vid_panel.c) */
void vid_panel_settings_scroll_by(int dy_pixels);
void vid_panel_settings_scroll_set(int abs_pixels);  /* absolute scroll position, clamped */
void vid_panel_settings_scroll_to_bar(int touch_y);  /* drag scrollbar thumb */

/* Touch hit test: returns 1 if (px, py) hits a widget; fills row/value.
 * py is in SCREEN coordinates; scroll is applied internally. */
int vid_panel_settings_resolve_touch(int px, int py, int *row_out, int *value_out);

/* After Sem screen_mode changes (load from disk or settings UI). */
void vid_panel_refit_layout_refresh_tex(void);
