#include "vid_panel_settings.h"
#include "vid_panel.h"
#include "vid_panel_layout.h"
#include "vid_panel_menu.h"
#include "vid_state.h"
#include "video_player.h"
#include "vid_settings.h"
#include "vid_screen.h"
#include "vid_texture.h"
#include "vid_worker.h"
#include "system/sem.h"
#include "system/util/decoder.h"
#include "system/util/err.h"
#include "system/util/queue.h"

/* ── Sub-page row tables ────────────────────────────────────────────────
 * ROOT   : volume, custom, video, advanced, ui mod (UI labels all lowercase; acronyms e.g. SW/HW stay caps)
 * CUSTOM : seek step, browse root (movies / SD root), auto dim (5s), eco
 * VIDEO  : 3d or 2d / tex filter / scale / 3d eyes swap
 * ADVANCED: HW decode(auto) / HW color / fake model / multi decode / no audio / no video (MVD upload = Unroll4 fixed)
 * ────────────────────────────────────────────────────────────────────── */

/* Sem_screen_mode ↔ settings UI chip index (0=3d, 1=2d, 2=auto) */
static int screen_mode_ui_from_sem(Sem_screen_mode sm)
{
    if (sm == DEF_SEM_SCREEN_MODE_3D)     return 0;
    if (sm == DEF_SEM_SCREEN_MODE_400PX)  return 1;
    return 2;
}

static Sem_screen_mode screen_mode_sem_from_ui(int ui)
{
    if (ui == 0) return DEF_SEM_SCREEN_MODE_3D;
    if (ui == 1) return DEF_SEM_SCREEN_MODE_400PX;
    return DEF_SEM_SCREEN_MODE_AUTO;
}

void vid_panel_refit_layout_refresh_tex(void)
{
    /* 上屏呈现尺寸与 `VID_PLAYER_TOP_FIT_*` 一致（与底屏亮灭无关）。 */
    uint32_t i;
    uint8_t  bi;

    for (i = 0; i < EYE_MAX; i++)
        Vid_fit_to_screen(VID_PLAYER_TOP_FIT_W, VID_PLAYER_TOP_FIT_H, (Vid_eye)i);
    for (bi = 0; bi < VIDEO_BUFFERS; bi++)
        for (i = 0; i < EYE_MAX; i++)
            Vid_large_texture_set_filter(&vid_player.large_image[bi][i], Vid_effective_use_linear_texture_filter(i));
}

static const VpSettingRowDesc s_root_rows[] = {
    {
        VP_SETTING_ROW_SLIDER, VP_SETTING_ID_SW_VOLUME,
        "volume",
        0, {""}, 0, 100
    },
    {
        VP_SETTING_ROW_SUBMENU, VP_SETTING_ID_ENTER_CUSTOM,
        "custom",
        0, {""}, 0, 0
    },
    {
        VP_SETTING_ROW_SUBMENU, VP_SETTING_ID_ENTER_VIDEO,
        "video",
        0, {""}, 0, 0
    },
    {
        VP_SETTING_ROW_SUBMENU, VP_SETTING_ID_ENTER_ADVANCED,
        "advanced",
        0, {""}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_UI_MOD,
        "ui mod",
        2, {"off", "on"}, 0, 0
    },
};

static const VpSettingRowDesc s_custom_rows[] = {
    {
        VP_SETTING_ROW_SLIDER, VP_SETTING_ID_SW_SEEK_DURATION,
        "seek step (s)",
        0, {""}, 1, 99
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_FS_BROWSER_ROOT,
        "browse root",
        2, {"movies", "SD root"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_AUTO_DIM_5S,
        "auto dim (5s)",
        2, {"on", "off"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_ECO_MODE,
        "eco mode",
        2, {"off", "on"}, 0, 0
    },
};

static const VpSettingRowDesc s_video_rows[] = {
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_SCREEN_MODE,
        "3d or 2d",
        3, {"3d", "2d", "auto"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_SW_TEX_FILTER,
        "tex filter",
        3, {"bilinear", "nearest", "auto"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_VIDEO_SCALE,
        "scale",
        2, {"pixel", "fit"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_SW_SBS_SWAP,
        "3d eyes swap",
        2, {"L<>R", "normal"}, 0, 0
    },
};

static const VpSettingRowDesc s_advanced_rows[] = {
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_ADV_HW_DECODE,
        "hw decode",
        2, {"SW", "HW(auto)"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_ADV_HW_COLOR,
        "hw color",
        2, {"CPU", "Y2R(auto)"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_FAKE_MODEL,
        "fake model",
        3, {"N3DS", "O3DS", "off"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_SW_MULTI_DECODE,
        "multi decode",
        2, {"off", "on"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_SW_SE1_NO_AUDIO,
        "no audio",
        2, {"on", "off"}, 0, 0
    },
    {
        VP_SETTING_ROW_CHOICE, VP_SETTING_ID_SW_SE1_NO_VIDEO,
        "no video",
        2, {"on", "off"}, 0, 0
    },
};

#define ROOT_ROW_COUNT     ((int)(sizeof(s_root_rows)     / sizeof(s_root_rows[0])))
#define VIDEO_ROW_COUNT    ((int)(sizeof(s_video_rows)    / sizeof(s_video_rows[0])))
#define CUSTOM_ROW_COUNT   ((int)(sizeof(s_custom_rows)   / sizeof(s_custom_rows[0])))
#define ADVANCED_ROW_COUNT ((int)(sizeof(s_advanced_rows) / sizeof(s_advanced_rows[0])))

/* ── Runtime state ─────────────────────────────────────────────────────── */
static VpSubpage s_subpage      = VP_SUBPAGE_ROOT;
static int       s_selected     = 0;
static int       s_scroll_px    = 0;   /* pixels scrolled from top */
static bool      s_slider_defer_disk_save = false;

/* Returns current page's row table and count */
static const VpSettingRowDesc *current_rows(int *count_out)
{
    switch (s_subpage) {
        case VP_SUBPAGE_VIDEO:    *count_out = VIDEO_ROW_COUNT;    return s_video_rows;
        case VP_SUBPAGE_CUSTOM:   *count_out = CUSTOM_ROW_COUNT;   return s_custom_rows;
        case VP_SUBPAGE_ADVANCED: *count_out = ADVANCED_ROW_COUNT; return s_advanced_rows;
        default:                  *count_out = ROOT_ROW_COUNT;     return s_root_rows;
    }
}

static int current_total_h(int row_count)
{
    /* VP_SET_TOTAL_H(n) = first_row_y + n*stride - content_y + 4 */
    return VP_SET_TOTAL_H(row_count);
}

static void clamp_scroll(int row_count)
{
    int max_scroll = current_total_h(row_count) - VP_SET_CONTENT_H;
    if (max_scroll < 0) max_scroll = 0;
    if (s_scroll_px < 0)          s_scroll_px = 0;
    if (s_scroll_px > max_scroll) s_scroll_px = max_scroll;
}

/* ── Init ────────────────────────────────────────────────────────────── */
void vid_panel_settings_init(void)
{
    s_slider_defer_disk_save = false;
    s_subpage   = VP_SUBPAGE_ROOT;
    s_selected  = 0;
    s_scroll_px = 0;
}

/* ── Row access ──────────────────────────────────────────────────────── */
int vid_panel_settings_row_count(void)
{
    int n;
    current_rows(&n);
    return n;
}

const VpSettingRowDesc *vid_panel_settings_row(int i)
{
    int n;
    const VpSettingRowDesc *rows = current_rows(&n);
    if (i < 0 || i >= n) return (void*)0;
    return &rows[i];
}

/* ── Value read ──────────────────────────────────────────────────────── */
static int get_value(VpSettingId id)
{
    Sem_config cfg = { 0, };
    switch (id) {
        case VP_SETTING_ID_AUTO_DIM_5S:
            /* Custom UI: 0=on, 1=off */
            return vid_player.auto_dim_5s ? 0 : 1;
        case VP_SETTING_ID_ADV_HW_DECODE:
            /* 0=SW, 1=HW — 与存档一致的是 pending（已载入时下一文件才进解码） */
            return vid_player.use_hw_decoding_pending ? 1 : 0;
        case VP_SETTING_ID_ADV_HW_COLOR: {
            if (vid_player.use_hw_color_conversion_pending == VID_HW_CONV_CPU)
                return 0;
            return 1;
        }
        case VP_SETTING_ID_SCREEN_MODE:
            Sem_get_config(&cfg);
            {
                Sem_screen_mode sm = cfg.screen_mode;
                if (sm >= DEF_SEM_SCREEN_MODE_MAX) sm = DEF_SEM_SCREEN_MODE_AUTO;
                return screen_mode_ui_from_sem(sm);
            }
        case VP_SETTING_ID_ECO_MODE:
            Sem_get_config(&cfg); return cfg.is_eco ? 1 : 0;
        case VP_SETTING_ID_FAKE_MODEL: {
            uint8_t fm = Sem_query_fake_model();
            if (fm >= DEF_SEM_MODEL_MAX) return 2;
            if (fm == DEF_SEM_MODEL_N3DS) return 0;
            return 1;
        }

        case VP_SETTING_ID_SW_VOLUME:
            return (vid_player.volume > 100) ? 100 : (int)vid_player.volume;
        case VP_SETTING_ID_SW_SEEK_DURATION:
            return (int)vid_player.seek_duration;
        case VP_SETTING_ID_FS_BROWSER_ROOT:
            return (vid_player.fs_browser_root_mode == VID_FS_BROWSER_ROOT_MOVIE) ? 0 : 1;
        case VP_SETTING_ID_UI_MOD:
            return vid_player.ui_mod ? 1 : 0;
        case VP_SETTING_ID_SW_TEX_FILTER: {
            int m = (int)vid_player.texture_filter_mode;
            if (m < 0 || m > 2) m = (int)VID_TEX_FILTER_AUTO;
            return m;
        }
        case VP_SETTING_ID_VIDEO_SCALE: {
            int s = (int)vid_player.video_scale_mode;
            if (s < 0 || s > 1) s = (int)VID_SCALE_FIT;
            return s;
        }
        case VP_SETTING_ID_SW_MULTI_DECODE:
            return vid_player.use_multi_threaded_decoding ? 1 : 0;
        case VP_SETTING_ID_SW_SBS_SWAP:
            /* UI chip 0 = L<>R (swap on), 1 = normal */
            return vid_player.sbs_swap_eyes ? 0 : 1;

        case VP_SETTING_ID_SW_SE1_NO_AUDIO:
            /* 0=On (no audio), 1=Off */
            return vid_player.disable_audio ? 0 : 1;
        case VP_SETTING_ID_SW_SE1_NO_VIDEO:
            return vid_player.disable_video ? 0 : 1;

        default: return 0;
    }
}

/* ── Value write ─────────────────────────────────────────────────────── */
static void set_value(VpSettingId id, int v)
{
    Sem_config cfg = { 0, };
    switch (id) {
        case VP_SETTING_ID_AUTO_DIM_5S:
            vid_player.auto_dim_5s = (v == 0);
            Vid_save_settings();
            break;
        case VP_SETTING_ID_ADV_HW_DECODE:
            Vid_set_use_hw_decoding(v != 0);
            Vid_save_settings();
            break;
        case VP_SETTING_ID_ADV_HW_COLOR: {
            uint8_t m = (v == 0) ? VID_HW_CONV_CPU : VID_HW_CONV_Y2R_X2;
            Vid_set_use_hw_color_conversion(m);
            Vid_save_settings();
            break;
        }
        case VP_SETTING_ID_SCREEN_MODE:
            Sem_get_config(&cfg);
            if (v < 0) v = 0;
            if (v > 2) v = 2;
            cfg.screen_mode = screen_mode_sem_from_ui(v);
            Sem_set_config(&cfg);
            vid_panel_refit_layout_refresh_tex();
            Vid_save_settings();
            break;
        case VP_SETTING_ID_ECO_MODE:
            Sem_get_config(&cfg);
            cfg.is_eco = (v != 0);
            Sem_set_config(&cfg);
            Vid_save_settings();
            break;
        case VP_SETTING_ID_FAKE_MODEL:
            if (v == 2) Sem_set_fake_model(255);
            else if (v == 0) Sem_set_fake_model(DEF_SEM_MODEL_N3DS);
            else Sem_set_fake_model(DEF_SEM_MODEL_O3DS);
            Vid_save_settings();
            break;

        case VP_SETTING_ID_SW_VOLUME: {
            int vol = v;
            if (vol < 0) vol = 0;
            if (vol > 100) vol = 100;
            vid_player.volume = (uint16_t)vol;
            if (!s_slider_defer_disk_save)
                Vid_save_settings();
            break;
        }
        case VP_SETTING_ID_SW_SEEK_DURATION: {
            int s = v;
            if (s < 1) s = 1;
            if (s > 99) s = 99;
            vid_player.seek_duration = (uint8_t)s;
            if (!s_slider_defer_disk_save)
                Vid_save_settings();
            break;
        }
        case VP_SETTING_ID_FS_BROWSER_ROOT: {
            int m = v;
            if (m < 0) m = 0;
            if (m > 1) m = 1;
            vid_player.fs_browser_root_mode = (m == 0) ? VID_FS_BROWSER_ROOT_MOVIE : VID_FS_BROWSER_ROOT_TF;
            Vid_save_settings();
            Vid_panel_files_on_root_mode_changed();
            break;
        }
        case VP_SETTING_ID_UI_MOD: {
            int m = v;
            if (m < 0) m = 0;
            if (m > 1) m = 1;
            vid_player.ui_mod = (m == 1);
            Vid_panel_player_invalidate_cpu_bar_layout();
            Vid_save_settings();
            break;
        }
        case VP_SETTING_ID_SW_TEX_FILTER: {
            int m = v;
            if (m < 0) m = 0;
            if (m > 2) m = 2;
            vid_player.texture_filter_mode = (uint8_t)m;
            for (uint8_t i = 0; i < VIDEO_BUFFERS; i++)
                for (uint32_t k = 0; k < EYE_MAX; k++)
                    Vid_large_texture_set_filter(&vid_player.large_image[i][k], Vid_effective_use_linear_texture_filter(k));
            Vid_save_settings();
            break;
        }
        case VP_SETTING_ID_VIDEO_SCALE: {
            int s = v;
            if (s < 0) s = 0;
            if (s > 1) s = 1;
            vid_player.video_scale_mode = (uint8_t)s;
            Vid_save_settings();
            vid_panel_refit_layout_refresh_tex();
            break;
        }
        case VP_SETTING_ID_SW_MULTI_DECODE:
            vid_player.use_multi_threaded_decoding = (v != 0);
            Vid_save_settings();
            break;
        case VP_SETTING_ID_SW_SBS_SWAP:
            vid_player.sbs_swap_eyes = (v == 0);
            Vid_save_settings();
            break;

        case VP_SETTING_ID_SW_SE1_NO_AUDIO:
            vid_player.disable_audio = (v == 0);
            Vid_save_settings();
            break;
        case VP_SETTING_ID_SW_SE1_NO_VIDEO:
            vid_player.disable_video = (v == 0);
            Vid_save_settings();
            break;

        default: (void)v; break;
    }
}

/* ── Fill view ───────────────────────────────────────────────────────── */
void vid_panel_settings_fill_view(VpSettingsView *out)
{
    int i, n;
    const VpSettingRowDesc *rows = current_rows(&n);
    out->row_count    = n;
    out->selected_row = s_selected;
    out->scroll_offset = s_scroll_px;
    out->total_height  = current_total_h(n);
    out->content_h     = VP_SET_CONTENT_H;
    out->subpage       = s_subpage;
    for (i = 0; i < n && i < VP_SETTINGS_MAX_ROWS; i++)
        out->values[i] = get_value(rows[i].id);
}

/* ── Apply / Adjust ──────────────────────────────────────────────────── */
void vid_panel_settings_apply_change(int row, int value)
{
    int n;
    const VpSettingRowDesc *rows = current_rows(&n);
    if (row < 0 || row >= n) return;
    /* Submenu navigation is handled separately; ignore here */
    if (rows[row].kind == VP_SETTING_ROW_SUBMENU) return;
    set_value(rows[row].id, value);
}

int vid_panel_settings_slider_value_at_x(int px, int row)
{
    int n;
    const VpSettingRowDesc *rows = current_rows(&n);
    if (row < 0 || row >= n) return 0;
    const VpSettingRowDesc *d = &rows[row];
    if (d->kind != VP_SETTING_ROW_SLIDER) return 0;

    int slider_right = VP_SET_SCROLLBAR_X - 26;
    int slider_w     = slider_right - VP_SET_CTRL_X;
    if (slider_w < 8) slider_w = 8;

    int range = d->slider_max - d->slider_min;
    int v     = d->slider_min;
    if (range > 0) {
        int rx = px - VP_SET_CTRL_X;
        if (rx < 0) rx = 0;
        if (rx > slider_w) rx = slider_w;
        v = d->slider_min + rx * range / slider_w;
        if (v < d->slider_min) v = d->slider_min;
        if (v > d->slider_max) v = d->slider_max;
    }
    return v;
}

void vid_panel_settings_slider_drag_begin(void)
{
    s_slider_defer_disk_save = true;
}

void vid_panel_settings_slider_drag_end(void)
{
    if (s_slider_defer_disk_save) {
        s_slider_defer_disk_save = false;
        Vid_save_settings();
    }
}

void vid_panel_settings_slider_drag_abort(void)
{
    if (s_slider_defer_disk_save) {
        s_slider_defer_disk_save = false;
        Vid_save_settings();
    }
}

void vid_panel_settings_adjust(int row, int delta)
{
    int n;
    const VpSettingRowDesc *rows = current_rows(&n);
    if (row < 0 || row >= n) return;
    const VpSettingRowDesc *d = &rows[row];
    int cur = get_value(d->id);
    switch (d->kind) {
        case VP_SETTING_ROW_TOGGLE:
        case VP_SETTING_ROW_CHOICE: {
            int opts = d->num_opts;
            if (opts > 0)
                set_value(d->id, (cur + delta % opts + opts) % opts);
            break;
        }
        case VP_SETTING_ROW_SLIDER: {
            int v = cur + delta;
            if (v < d->slider_min) v = d->slider_min;
            if (v > d->slider_max) v = d->slider_max;
            set_value(d->id, v);
            break;
        }
        case VP_SETTING_ROW_SUBMENU:
            break;
    }
}

/* ── Selection ───────────────────────────────────────────────────────── */
void vid_panel_settings_move_selection(int delta)
{
    int n;
    current_rows(&n);
    s_selected += delta;
    if (s_selected < 0)    s_selected = 0;
    if (s_selected >= n)   s_selected = n - 1;

    /* Auto-scroll selected row into view */
    int row_top = VP_SETTINGS_ROW_Y(s_selected) - VP_SET_CONTENT_Y;
    int row_bot = row_top + VP_SET_OPT_H;
    if (row_top < s_scroll_px)
        s_scroll_px = row_top;
    if (row_bot > s_scroll_px + VP_SET_CONTENT_H)
        s_scroll_px = row_bot - VP_SET_CONTENT_H;
    clamp_scroll(n);
}

void vid_panel_settings_reset_selection(void)
{
    vid_panel_settings_slider_drag_abort();
    s_selected = 0;
    s_scroll_px = 0;
}
int  vid_panel_settings_get_selected(void)    { return s_selected; }

/* ── Submenu navigation ──────────────────────────────────────────────── */
void vid_panel_settings_enter_subpage(VpSubpage page)
{
    vid_panel_settings_slider_drag_abort();
    s_subpage   = page;
    s_selected  = 0;
    s_scroll_px = 0;
}

void vid_panel_settings_open_submenu_row(int row)
{
    int n;
    const VpSettingRowDesc *rows = current_rows(&n);
    if (row < 0 || row >= n) return;
    const VpSettingRowDesc *d = &rows[row];
    if (d->kind != VP_SETTING_ROW_SUBMENU) return;
    if (d->id == VP_SETTING_ID_ENTER_VIDEO)
        vid_panel_settings_enter_subpage(VP_SUBPAGE_VIDEO);
    else if (d->id == VP_SETTING_ID_ENTER_ADVANCED)
        vid_panel_settings_enter_subpage(VP_SUBPAGE_ADVANCED);
    else if (d->id == VP_SETTING_ID_ENTER_CUSTOM)
        vid_panel_settings_enter_subpage(VP_SUBPAGE_CUSTOM);
}

void vid_panel_settings_go_back(void)
{
    vid_panel_settings_slider_drag_abort();
    s_subpage   = VP_SUBPAGE_ROOT;
    s_selected  = 0;
    s_scroll_px = 0;
}

bool vid_panel_settings_can_go_back(void)
{
    return s_subpage != VP_SUBPAGE_ROOT;
}

/* ── Scroll ──────────────────────────────────────────────────────────── */
void vid_panel_settings_scroll_by(int dy_pixels)
{
    int n;
    current_rows(&n);
    s_scroll_px += dy_pixels;
    clamp_scroll(n);
}

void vid_panel_settings_scroll_set(int abs_pixels)
{
    int n;
    current_rows(&n);
    s_scroll_px = abs_pixels;
    clamp_scroll(n);
}

void vid_panel_settings_scroll_to_bar(int touch_y)
{
    int n;
    current_rows(&n);
    int total_h  = current_total_h(n);
    int track_h  = VP_SET_CONTENT_H;
    if (total_h <= track_h) return;

    /* thumb_h proportional to visible / total */
    int thumb_h = track_h * track_h / total_h;
    if (thumb_h < 12) thumb_h = 12;
    int track_travel = track_h - thumb_h;
    if (track_travel <= 0) return;

    /* Map touch_y (screen coords) to scroll position */
    int rel = touch_y - VP_SET_CONTENT_Y - thumb_h / 2;
    if (rel < 0) rel = 0;
    if (rel > track_travel) rel = track_travel;
    s_scroll_px = rel * (total_h - track_h) / track_travel;
    clamp_scroll(n);
}

/* ── Touch hit test ──────────────────────────────────────────────────── */
int vid_panel_settings_resolve_touch(int px, int py, int *row_out, int *value_out)
{
    int i, n;
    const VpSettingRowDesc *rows = current_rows(&n);

    /* Reject touches outside content area */
    if (py < VP_SET_CONTENT_Y || py >= VP_SET_CONTENT_BOT) return 0;
    /* Reject scrollbar column */
    if (px >= VP_SET_SCROLLBAR_X) return 0;

    /* Convert screen-y → virtual-y */
    int vy = py + s_scroll_px;

    for (i = 0; i < n; i++) {
        int ry = VP_SETTINGS_ROW_Y(i);
        if (vy < ry || vy >= ry + VP_SET_OPT_H) continue;

        switch (rows[i].kind) {
            case VP_SETTING_ROW_TOGGLE:
            case VP_SETTING_ROW_CHOICE: {
                int nc = rows[i].num_opts;
                int cw = vp_set_chip_w(nc);
                int j;
                for (j = 0; j < nc; j++) {
                    int cx = vp_set_chip_x(nc, j);
                    if (px >= cx && px < cx + cw) {
                        *row_out = i; *value_out = j; return 1;
                    }
                }
                break;
            }
            case VP_SETTING_ROW_SLIDER: {
                /* Slider touch area matches the rendered track:
                 * from VP_SET_CTRL_X to (VP_SET_SCROLLBAR_X - 26) */
                int slider_right = VP_SET_SCROLLBAR_X - 26;
                int slider_w     = slider_right - VP_SET_CTRL_X;
                if (slider_w < 8) slider_w = 8;
                if (px >= VP_SET_CTRL_X && px < VP_SET_CTRL_X + slider_w) {
                    int range = rows[i].slider_max - rows[i].slider_min;
                    int v     = rows[i].slider_min;
                    if (range > 0) {
                        int rx = px - VP_SET_CTRL_X;
                        v = rows[i].slider_min + rx * range / slider_w;
                        if (v < rows[i].slider_min) v = rows[i].slider_min;
                        if (v > rows[i].slider_max) v = rows[i].slider_max;
                    }
                    *row_out = i; *value_out = v; return 1;
                }
                break;
            }
            case VP_SETTING_ROW_SUBMENU:
                *row_out = i; *value_out = 0; return 1;
        }
    }
    return 0;
}
