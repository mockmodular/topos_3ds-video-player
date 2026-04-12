#include "vid_panel.h"
#include "vid_panel_fs.h"
#include "vid_panel_menu.h"
#include "vid_panel_theme.h"
#include "vid_panel_layout.h"
#include "vid_panel_settings.h"
#include "vid_state.h"
#include "vid_cmd.h"

#include <citro2d.h>
#include <citro3d.h>
#include <3ds.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "system/draw/draw.h"
#include "system/sem.h"
#include "system/util/err.h"
#include "system/util/hid.h"
#include "system/util/hid_types.h"
#include "system/util/log.h"
#include "system/util/util.h"
#include "system/util/sync.h"

/* ──────────────────────────────────────────────────────────────────────────────
 * Static state (private to this module)
 * ────────────────────────────────────────────────────────────────────────────*/
static C2D_Font    s_font = NULL;
static C2D_TextBuf s_tbuf = NULL;

/* 依赖注入上下文（由 Vid_panel_init 写入，之后只读） */
static VidPanelCtx s_ctx = { 0, };

/* Back-reference to previous panel (for Settings "Back" action) */
static Vid_panel   s_panel_before_setting = VID_PANEL_PLAYER;

/* Lawvere 信息覆盖层（仅设置面板） */
static bool        s_quick_menu_open = false;

/* File browser */
static FsListing s_listing       = { { { {0}, 0 } }, 0 };
static char      s_path[FS_PATH_LEN];
static int       s_selected      = 0;
static int       s_scroll_off    = 0;
static int       s_flash_row     = -1;
static u64       s_flash_until   = 0;   /* osGetTime() ms，到期后清除闪 */
static int       s_touch_x0      = -1;  /* press x – used for hit-test on release */
static int       s_touch_y0      = -1;
static int       s_touch_off0    = 0;
static int       s_touch_state   = 0;   /* 0=idle 1=press(may tap) 2=dragging */
static int       s_press_row     = -1;  /* row highlighted on press-down */

/* Files panel scrollbar drag state (mirrors Settings drag state) */
/* s_fs_drag: 0=idle 1=tentative 2=dragging-content 3=dragging-scrollbar */
static int       s_fs_drag       = 0;
static int       s_fs_touch_y0   = 0;
static int       s_fs_scroll0    = 0;   /* s_scroll_off (rows) at drag start */

/* Settings panel touch state */
/* s_set_drag: -1=press-consumed 0=idle 1=tentative-scroll 2=drag-content 3=scrollbar
 *             4=slider-drag 5=slider-press（待区分横向拖滑块 vs 纵向滚动） */
static int       s_set_drag      = 0;
static int       s_set_touch_y0  = 0;
static int       s_set_scroll0   = 0;   /* scroll_px at drag start */
static int       s_slider_row    = -1;  /* valid when s_set_drag is 4 or 5 */

/* Player panel seek state: -1 = not seeking, >=0 = current seek-line screen X */
static int       s_seek_x        = -1;

/* FILES：从列表打开失败（非空路径但无法播放） */
static bool            s_files_play_err_visible = false;
static Draw_image_data s_files_play_err_ok      = { 0, };

/* ──────────────────────────────────────────────────────────────────────────────
 * Internal drawing helpers  (same visual language as topos.ui0.04)
 * ────────────────────────────────────────────────────────────────────────────*/

/* citro2d 的实际 z 行为不可靠用于层叠，全部使用 0.5f，完全依赖绘制顺序决定覆盖关系。
 * 文件打开失败弹窗在底屏流程最后绘制，盖住 chrome。
 * Lawvere 层必须 z > VP_Z_CHROME，否则会被设置顶/底栏盖住（曾用 0.4f 导致看不见）。 */
#define VP_Z_BG       0.5f
#define VP_Z_ROW_BG   0.5f
#define VP_Z_ROW_BDR  0.5f
#define VP_Z_ROW_TEXT 0.5f
#define VP_Z_CHROME   0.5f
#define VP_Z_CHR_TEXT 0.5f
#define VP_Z_PROGRESS 0.5f
#define VP_Z_OVERLAY_DIM     0.60f
#define VP_Z_OVERLAY_LAWVERE 0.62f

static void p_draw_text(const char *str, float x, float y, u32 color)
{
    if (!str || str[0] == '\0' || !s_tbuf) return;
    C2D_Text t;
    if (s_font)
        C2D_TextFontParse(&t, s_font, s_tbuf, str);
    else
        C2D_TextParse(&t, s_tbuf, str);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, VP_Z_ROW_TEXT,
                 VP_FONT_SCALE, VP_FONT_SCALE, color);
}

/* 文件列表文件名：整串绘制后在 [max_right, scrollbar) 用 clip_bg 矩形盖住超出部分（像素级硬切，可切半字） */
static void p_draw_text_clipped(const char *str, float x, float y, float max_right, u32 color,
                                u32 clip_bg, float clip_top, float clip_h)
{
    if (!str || str[0] == '\0' || !s_tbuf) return;
    float avail = max_right - x;
    if (avail <= 0.0f) return;

    C2D_TextBufClear(s_tbuf);
    C2D_Text td;
    if (s_font) C2D_TextFontParse(&td, s_font, s_tbuf, str);
    else        C2D_TextParse(&td, s_tbuf, str);
    C2D_TextOptimize(&td);
    float tw, th;
    C2D_TextGetDimensions(&td, VP_FONT_SCALE, VP_FONT_SCALE, &tw, &th);
    (void)th;

    C2D_DrawText(&td, C2D_WithColor, x, y, VP_Z_ROW_TEXT,
                 VP_FONT_SCALE, VP_FONT_SCALE, color);

    if (tw > avail) {
        float mw = (float)VP_SCROLLBAR_X - max_right;
        if (mw > 0.0f)
            C2D_DrawRectSolid(max_right, clip_top, VP_Z_ROW_TEXT, mw, clip_h, clip_bg);
    }
}

/* 顶栏文字区右界 = max_right（VP_CHROME_TOP_TEXT_RIGHT_X），不是屏宽 320。
 * 在 [max_right, 屏右) 用 path 条色盖住，避免字宽测量小于实际笔画导致越过「路径/标题可显示区」贴向右上按钮。 */
static void p_chrome_top_mask_right_of_field(float max_right)
{
    float mw = (float)VP_SCREEN_W - max_right;
    if (mw > 0.0f)
        C2D_DrawRectSolid(max_right, 0.0f, VP_Z_CHR_TEXT, mw,
                          (float)VP_PATH_BAR_H, VP_COL_PATH_BG);
}

/* 顶栏 path bar 文字（路径 / Player 标题 / 设置顶栏标题）：
 * 水平范围恒为 [VP_CHROME_TOP_TEXT_LEFT_X, VP_CHROME_TOP_TEXT_RIGHT_X)（与 layout 一致；文件名与路径同一组常量）
 * keep_left=true（标题）：永远保左；超出则右裁（上遮罩）
 * keep_left=false（路径）：能放下则保左且右缘不超过 RIGHT；超出则整串右缘对齐 RIGHT（非屏右）、左裁 */
static void p_draw_text_bar(const char *str, float x, float max_right, u32 color, bool keep_left)
{
    if (!str || str[0] == '\0' || !s_tbuf) return;
    float avail = max_right - x;
    if (avail <= 0.0f) return;

    C2D_TextBufClear(s_tbuf);
    C2D_Text td;
    if (s_font) C2D_TextFontParse(&td, s_font, s_tbuf, str);
    else        C2D_TextParse(&td, s_tbuf, str);
    C2D_TextOptimize(&td);
    float tw, th;
    C2D_TextGetDimensions(&td, VP_CHROME_TOP_TEXT_SCALE, VP_CHROME_TOP_TEXT_SCALE, &tw, &th);
    float ty = vp_chrome_top_text_ty(th);

    if (keep_left) {
        C2D_DrawText(&td, C2D_WithColor, x, ty, VP_Z_CHR_TEXT,
                     VP_CHROME_TOP_TEXT_SCALE, VP_CHROME_TOP_TEXT_SCALE, color);
        if (tw > avail)
            p_chrome_top_mask_right_of_field(max_right);
        return;
    }

    if (tw <= avail) {
        C2D_DrawText(&td, C2D_WithColor, x, ty, VP_Z_CHR_TEXT,
                     VP_CHROME_TOP_TEXT_SCALE, VP_CHROME_TOP_TEXT_SCALE, color);
        p_chrome_top_mask_right_of_field(max_right);
        return;
    }

    /* 右缘对齐 max_right（路径区右界）：tx + tw ≈ max_right，不是 VP_SCREEN_W */
    float tx = max_right - tw;
    C2D_DrawText(&td, C2D_WithColor, tx, ty, VP_Z_CHR_TEXT,
                 VP_CHROME_TOP_TEXT_SCALE, VP_CHROME_TOP_TEXT_SCALE, color);
    if (tx < x) {
        float mw = x;
        if (mw > 0.0f)
            C2D_DrawRectSolid(0.0f, 0.0f, VP_Z_CHR_TEXT, mw,
                              (float)VP_PATH_BAR_H, VP_COL_PATH_BG);
    }
    p_chrome_top_mask_right_of_field(max_right);
}

/* 三界面共用 chrome 按钮：标签在按钮矩形内水平垂直居中（Citro2D 水平用 AlignCenter 锚在按钮中心；过长则改左对齐并夹紧） */
static void p_draw_button(float x, float y, float w, float h, const char *label)
{
    const float pad = 2.0f;

    C2D_DrawRectSolid(x, y,     VP_Z_CHROME, w, h, VP_COL_BTN_BG);
    C2D_DrawRectSolid(x,     y,     VP_Z_CHROME, w, 1, VP_COL_BTN_BORDER);
    C2D_DrawRectSolid(x,     y+h-1, VP_Z_CHROME, w, 1, VP_COL_BTN_BORDER);
    C2D_DrawRectSolid(x,     y,     VP_Z_CHROME, 1, h, VP_COL_BTN_BORDER);
    C2D_DrawRectSolid(x+w-1, y,     VP_Z_CHROME, 1, h, VP_COL_BTN_BORDER);
    if (!label || label[0] == '\0' || !s_tbuf) return;
    C2D_Text t;
    if (s_font) C2D_TextFontParse(&t, s_font, s_tbuf, label);
    else        C2D_TextParse(&t, s_tbuf, label);
    C2D_TextOptimize(&t);
    float tw, th;
    C2D_TextGetDimensions(&t, VP_FONT_SCALE, VP_FONT_SCALE, &tw, &th);
    /* 垂直：按 C2D 行高居中（与 GetDimensions 的 th 一致）；不用 path 条带的 +1 偏移 */
    float ty = y + (h - th) * 0.5f;
    float inner = w - 2.0f * pad;
    if (tw <= inner) {
        C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, x + w * 0.5f, ty, VP_Z_CHR_TEXT,
                     VP_FONT_SCALE, VP_FONT_SCALE, VP_COL_TEXT);
    } else {
        float tx = x + pad;
        if (tx + tw > x + w - pad)
            tx = x + w - tw - pad;
        C2D_DrawText(&t, C2D_WithColor, tx, ty, VP_Z_CHR_TEXT,
                     VP_FONT_SCALE, VP_FONT_SCALE, VP_COL_TEXT);
    }
}

/* Draw shared chrome: path-bar (top strip) + separator */
static void p_draw_chrome_top(const char *title, const char *sw_label)
{
    C2D_DrawRectSolid(0, 0, VP_Z_CHROME, VP_SCREEN_W, VP_PATH_BAR_H, VP_COL_PATH_BG);
    p_draw_text_bar(title, (float)VP_CHROME_TOP_TEXT_LEFT_X, (float)VP_CHROME_TOP_TEXT_RIGHT_X,
                    VP_COL_TEXT, false);
    p_draw_button((float)VP_SW_BTN_X, (float)VP_TOP_BTN_Y,
                  (float)VP_SW_BTN_W, (float)VP_TOP_BTN_H, sw_label);
    C2D_DrawRectSolid(0, VP_PATH_BAR_H, VP_Z_CHROME, VP_SCREEN_W, 1, VP_COL_SEP);
}

/* Draw shared chrome: footer bar */
static void p_draw_chrome_footer(const char *back_label, const char *set_label)
{
    float fy = (float)(VP_SCREEN_H - VP_FOOTER_H);
    C2D_DrawRectSolid(0, fy - 1, VP_Z_CHROME, VP_SCREEN_W, 1, VP_COL_SEP);
    C2D_DrawRectSolid(0, fy,     VP_Z_CHROME, VP_SCREEN_W, VP_FOOTER_H, VP_COL_FOOTER_BG);
    p_draw_button((float)VP_BACK_BTN_X, fy + (float)VP_FOOTER_BTN_PAD_Y,
                  (float)VP_BACK_BTN_W, (float)VP_FOOTER_BTN_H, back_label);
    p_draw_button((float)VP_SET_BTN_X,  fy + (float)VP_FOOTER_BTN_PAD_Y,
                  (float)VP_SET_BTN_W,  (float)VP_FOOTER_BTN_H, set_label);
}

/* Draw the progress bar + seek line.
 * seek_x: screen X of the seek cursor (-1 = no seek line).
 *
 * 两层结构（左右对称）：
 *   [0, INVALID)              — 端帽 (PROGRESS_END，近黑)
 *   [INVALID, W-INVALID)      — 主进度轨道 (PROGRESS_BG / PROGRESS_FILL)
 */
static void p_draw_progress_bar(double pts, double duration, int seek_x)
{
    C2D_DrawRectSolid((float)VP_PROGRESS_X_MIN, (float)VP_PROGRESS_Y, VP_Z_PROGRESS,
                      (float)VP_PROGRESS_TOTAL_W, (float)VP_PROGRESS_H,
                      VP_COL_PROGRESS_BG);
    C2D_DrawRectSolid(0, (float)VP_PROGRESS_Y, VP_Z_PROGRESS,
                      (float)VP_PROGRESS_INVALID, (float)VP_PROGRESS_H,
                      VP_COL_PROGRESS_END);
    C2D_DrawRectSolid((float)(VP_SCREEN_W - VP_PROGRESS_INVALID), (float)VP_PROGRESS_Y, VP_Z_PROGRESS,
                      (float)VP_PROGRESS_INVALID, (float)VP_PROGRESS_H,
                      VP_COL_PROGRESS_END);

    if (duration > 0.0) {
        float prog = (float)(pts / duration);
        if (prog < 0.0f) prog = 0.0f;
        if (prog > 1.0f) prog = 1.0f;

        float filled_w = prog * (float)VP_PROGRESS_TOTAL_W;
        if (filled_w > 0.0f)
            C2D_DrawRectSolid((float)VP_PROGRESS_X_MIN, (float)VP_PROGRESS_Y,
                              VP_Z_PROGRESS, filled_w, (float)VP_PROGRESS_H,
                              VP_COL_PROGRESS_FILL);

        float hx = (float)VP_PROGRESS_X_MIN + filled_w - 4.0f;
        if (hx < (float)VP_PROGRESS_X_MIN)        hx = (float)VP_PROGRESS_X_MIN;
        if (hx > (float)(VP_PROGRESS_X_MAX - 7))  hx = (float)(VP_PROGRESS_X_MAX - 7);
        C2D_DrawRectSolid(hx, (float)(VP_PROGRESS_Y - 3), VP_Z_PROGRESS, 8, 9,
                          VP_COL_PROGRESS_HEAD);
    }

    if (seek_x >= 0)
        C2D_DrawRectSolid((float)(seek_x - 1), (float)(VP_PROGRESS_Y - 5), VP_Z_PROGRESS,
                          2, 14, VP_COL_SEEK_LINE);
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Internal: file browser helpers
 * ────────────────────────────────────────────────────────────────────────────*/

static void panel_reload_listing(void);

/* 设置项「媒体目录」在卡上的实际路径：优先 sdmc:/movies，兼容旧版 sdmc:/movie；均无则返回 NULL → 回退 TF 根。 */
static const char *panel_media_sdmc_path(void)
{
    if (fs_directory_exists("sdmc:/movies"))
        return "sdmc:/movies";
    if (fs_directory_exists("sdmc:/movie"))
        return "sdmc:/movie";
    return NULL;
}

/* Effective top of file browser: 媒体模式且卡上存在上述目录之一 → 该目录；否则 sdmc:/ */
static void panel_effective_files_root(char *out, size_t out_sz)
{
    if (vid_player.fs_browser_root_mode == VID_FS_BROWSER_ROOT_MOVIE) {
        const char *p = panel_media_sdmc_path();
        if (p) {
            snprintf(out, out_sz, "%s", p);
            return;
        }
    }
    snprintf(out, out_sz, "sdmc:/");
}

void Vid_panel_files_sync_after_settings_load(void)
{
    char root[FS_PATH_LEN];

    panel_effective_files_root(root, sizeof(root));
    snprintf(s_path, sizeof(s_path), "%s", root);
    /* 须始终刷新列表：否则当前若在 PLAYER，s_path 已随存档更新而 s_listing 仍是 init 时默认根目录的快照 */
    panel_reload_listing();
}

void Vid_panel_files_on_root_mode_changed(void)
{
    char root[FS_PATH_LEN];

    panel_effective_files_root(root, sizeof(root));
    fs_path_clamp_to_root(s_path, root);
    /* 与 s_path 保持一致，避免在设置里改根目录后进入文件页仍显示旧目录列表 */
    panel_reload_listing();
}

static void panel_reload_listing(void)
{
    fs_list(s_path, &s_listing);
    s_selected    = 0;
    s_scroll_off  = 0;
    s_flash_row   = -1;
    s_flash_until = 0;
}

static void panel_clamp_scroll(void)
{
    int max_off = s_listing.count - VP_VISIBLE_ROWS;
    if (max_off < 0) max_off = 0;
    if (s_scroll_off > max_off) s_scroll_off = max_off;
    if (s_scroll_off < 0)       s_scroll_off = 0;
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Internal: open a selected file for playback
 * ────────────────────────────────────────────────────────────────────────────*/
static void panel_open_file(const char *filename, const char *directory)
{
    Vid_file *file_data = (Vid_file *)malloc(sizeof(Vid_file));
    if (!file_data) return;

    /* vid_decode.c 把 directory + name 拼成传给 FFmpeg 的路径：
     *   Util_str_set(&path, file.directory);
     *   Util_str_add(&path, file.name);
     * 约定：directory 为 "/Videos/"（无 sdmc:，末尾有 /），name 为 "movie.mp4"。
     * s_path 常为 "sdmc:/Videos"：去掉 "sdmc:" 前缀并保证末尾 '/'。 */
    file_data->index = 0;
    file_data->request_player_panel_on_ok = true;
    snprintf(file_data->name, sizeof(file_data->name), "%s", filename);

    {
        const char *dir = directory;
        /* 去掉 "sdmc:" 前缀，与解码层路径约定一致 */
        if (strncmp(dir, "sdmc:", 5) == 0)
            dir += 5;

        int dlen = (int)strlen(dir);
        if (dlen > 0 && dir[dlen - 1] == '/') {
            snprintf(file_data->directory, sizeof(file_data->directory), "%s", dir);
        } else {
            snprintf(file_data->directory, sizeof(file_data->directory), "%s/", dir);
        }
    }

    Util_sync_lock(&vid_player.play_request_pending_lock, UINT64_MAX);
    if (vid_player.play_request_pending)
        free(vid_player.play_request_pending);
    vid_player.play_request_pending = file_data;
    Util_sync_unlock(&vid_player.play_request_pending_lock);

    uint32_t result = DEF_ERR_OTHER;
    DEF_LOG_RESULT_SMART(result,
        Util_queue_add(&vid_player.decode_thread_command_queue,
                       DECODE_THREAD_PLAY_REQUEST,
                       NULL, QUEUE_OP_TIMEOUT_US,
                       (Queue_option)(QUEUE_OPTION_SEND_TO_FRONT | QUEUE_OPTION_DO_NOT_ADD_IF_EXIST)),
        (result == DEF_SUCCESS), result);
}

static void p_draw_files_play_error_overlay(void)
{
    if (!s_files_play_err_visible)
        return;
    if (!s_tbuf)
        return;

    const float frame_x = (float)VP_OVERLAY_FRAME_X;
    const float frame_w = (float)VP_OVERLAY_FRAME_W;
    const float inner_x = (float)VP_OVERLAY_INNER_X;
    const float inner_w = (float)VP_OVERLAY_INNER_W;
    const float ph      = (float)VP_OVERLAY_PANEL_H;
    const float py      = ((float)VP_SCREEN_H - ph) * 0.5f;
    const float content_cx = VP_OVERLAY_CONTENT_CENTER_XF;

    const u32 col_bg     = C2D_Color32(0x18, 0x1E, 0x2C, 0xFF);
    const u32 col_bdr    = C2D_Color32(0x50, 0x60, 0x80, 0xFF);
    const u32 col_white  = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    const u32 col_btn_bg = s_files_play_err_ok.selected
                           ? C2D_Color32(0x30, 0x50, 0x80, 0xFF)
                           : C2D_Color32(0x28, 0x32, 0x48, 0xFF);
    const u32 col_btn_bd = C2D_Color32(0x70, 0x88, 0xB0, 0xFF);

    const float bd = (float)VP_OVERLAY_BORDER_PX;
    C2D_DrawRectSolid(inner_x, py + bd, 0.6f, inner_w, ph - 2.0f * bd, col_bg);
    C2D_DrawRectSolid(frame_x,          py,          0.6f, frame_w, 1,     col_bdr);
    C2D_DrawRectSolid(frame_x,          py + ph - 1, 0.6f, frame_w, 1,     col_bdr);
    C2D_DrawRectSolid(frame_x,          py,          0.6f, 1,       ph, col_bdr);
    C2D_DrawRectSolid(frame_x + frame_w - 1.0f, py,  0.6f, 1,       ph, col_bdr);

    const float btn_w = (float)VP_CHROME_BTN_W;
    const float btn_h = (float)VP_CHROME_BTN_H;
    /* 用实测字高排布，避免固定 line_h 小于真实 th 导致行叠在一起、整块在面板里偏上/偏下 */
    const float line_gap   = 6.0f;
    const float gap_above_btn = 12.0f;

    float tw1 = 0, th1 = 0, tw2 = 0, th2 = 0;
    C2D_TextBufClear(s_tbuf);
    {
        C2D_Text t;
        const char *msg = "Unable to open file.";
        if (s_font) C2D_TextFontParse(&t, s_font, s_tbuf, msg);
        else        C2D_TextParse(&t, s_tbuf, msg);
        C2D_TextOptimize(&t);
        C2D_TextGetDimensions(&t, VP_FONT_SCALE, VP_FONT_SCALE, &tw1, &th1);
    }
    C2D_TextBufClear(s_tbuf);
    {
        C2D_Text t;
        const char *msg = "Format not supported";
        if (s_font) C2D_TextFontParse(&t, s_font, s_tbuf, msg);
        else        C2D_TextParse(&t, s_tbuf, msg);
        C2D_TextOptimize(&t);
        C2D_TextGetDimensions(&t, VP_FONT_SCALE, VP_FONT_SCALE, &tw2, &th2);
    }

    const float total_h = th1 + line_gap + th2 + gap_above_btn + btn_h;
    float y_top = py + (ph - total_h) * 0.5f;
    if (y_top < py + 2.0f)
        y_top = py + 2.0f;

    C2D_TextBufClear(s_tbuf);
    {
        C2D_Text t;
        const char *msg = "Unable to open file.";
        if (s_font) C2D_TextFontParse(&t, s_font, s_tbuf, msg);
        else        C2D_TextParse(&t, s_tbuf, msg);
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, content_cx, y_top, 0.7f,
                     VP_FONT_SCALE, VP_FONT_SCALE, col_white);
    }
    C2D_TextBufClear(s_tbuf);
    {
        const float y2 = y_top + th1 + line_gap;
        C2D_Text t;
        const char *msg = "Format not supported";
        if (s_font) C2D_TextFontParse(&t, s_font, s_tbuf, msg);
        else        C2D_TextParse(&t, s_tbuf, msg);
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, content_cx, y2, 0.7f,
                     VP_FONT_SCALE, VP_FONT_SCALE, col_white);
    }

    /* 按钮矩形贴像素栅格，水平中心与内芯中心一致（避免纯 float 半像素漂移） */
    const float bx = floorf(content_cx - btn_w * 0.5f + 0.5f);
    const float by = y_top + th1 + line_gap + th2 + gap_above_btn;
    C2D_DrawRectSolid(bx,             by,             0.8f, btn_w, btn_h, col_btn_bg);
    C2D_DrawRectSolid(bx,             by,             0.8f, btn_w, 1,     col_btn_bd);
    C2D_DrawRectSolid(bx,             by + btn_h - 1, 0.8f, btn_w, 1,     col_btn_bd);
    C2D_DrawRectSolid(bx,             by,             0.8f, 1,     btn_h, col_btn_bd);
    C2D_DrawRectSolid(bx + btn_w - 1, by,             0.8f, 1,     btn_h, col_btn_bd);
    C2D_TextBufClear(s_tbuf);
    {
        C2D_Text t;
        if (s_font) C2D_TextFontParse(&t, s_font, s_tbuf, "OK");
        else        C2D_TextParse(&t, s_tbuf, "OK");
        C2D_TextOptimize(&t);
        float tw, th; C2D_TextGetDimensions(&t, VP_FONT_SCALE, VP_FONT_SCALE, &tw, &th);
        (void)tw;
        /* 水平：与按钮矩形同一中心线；垂直：与顶栏按钮一致 */
        const float btn_cx = bx + btn_w * 0.5f;
        float lty = by + (btn_h - th) * 0.5f + 1.0f;
        C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, btn_cx, lty, 0.9f,
                     VP_FONT_SCALE, VP_FONT_SCALE, col_white);
    }

    s_files_play_err_ok.x      = (double)bx;
    s_files_play_err_ok.y      = (double)by;
    s_files_play_err_ok.x_size = (double)btn_w;
    s_files_play_err_ok.y_size = (double)btn_h;
}

void Vid_panel_files_show_play_error(void)
{
    s_files_play_err_visible     = true;
    s_files_play_err_ok.selected = false;
    s_files_play_err_ok.x       = 100.0;
    s_files_play_err_ok.y       = 108.0;
    s_files_play_err_ok.x_size  = 120.0;
    s_files_play_err_ok.y_size  = 32.0;
}

bool Vid_panel_files_play_error_is_visible(void)
{
    return s_files_play_err_visible;
}

void Vid_panel_files_play_error_hid(const Hid_info *k)
{
    if (!s_files_play_err_visible || !k)
        return;

    if ((DEF_HID_PHY_PR(k->touch) && DEF_HID_INIT_IN(s_files_play_err_ok, (*k))) || DEF_HID_PHY_PR(k->a))
    {
        s_files_play_err_ok.selected = true;
        Draw_set_refresh_needed(true);
    }

    if (((DEF_HID_PR_EM(k->touch, 1) || DEF_HID_HD(k->touch)) && DEF_HID_INIT_LAST_IN(s_files_play_err_ok, (*k)))
    || (DEF_HID_PR_EM(k->a, 1) || DEF_HID_HD(k->a)))
    {
        s_files_play_err_visible     = false;
        s_files_play_err_ok.selected = false;
        Util_hid_reset_key_state(HID_KEY_BIT_ALL);
        Draw_set_refresh_needed(true);
        return;
    }

    if (DEF_HID_PHY_NP(k->touch) && DEF_HID_PHY_NP(k->a))
    {
        if (s_files_play_err_ok.selected)
            Draw_set_refresh_needed(true);
        s_files_play_err_ok.selected = false;
    }
}

/* ──────────────────────────────────────────────────────────────────────────────
 * Panel rendering
 * ────────────────────────────────────────────────────────────────────────────*/

static void draw_files_panel(void)
{
    /* Flash：按绝对时间结束，与帧率无关 */
    if (s_flash_row >= 0 && (u64)osGetTime() >= s_flash_until)
        s_flash_row = -1;

    /* Background */
    C2D_DrawRectSolid(0, 0, VP_Z_BG, VP_SCREEN_W, VP_SCREEN_H, VP_COL_BG);

    /* File list */
    if (s_listing.count == 0) {
        p_draw_text("(empty)", (float)VP_CHROME_LEFT_TEXT_X, (float)(VP_LIST_Y + 2),
                    VP_COL_FOOTER_TEXT);
    } else {
        int end = s_scroll_off + VP_VISIBLE_ROWS;
        if (end > s_listing.count) end = s_listing.count;

        for (int i = s_scroll_off; i < end; i++) {
            float row_y      = (float)(VP_LIST_Y + (i - s_scroll_off) * VP_ROW_H);
            const FsEntry *e = &s_listing.entries[i];
            int is_selected  = (i == s_selected);
            int is_flash     = (i == s_flash_row);

            if (is_flash)
                C2D_DrawRectSolid(0, row_y, VP_Z_ROW_BG, VP_SCROLLBAR_X, VP_ROW_H,
                                  VP_COL_FLASH);
            else if (is_selected)
                C2D_DrawRectSolid(0, row_y, VP_Z_ROW_BG, VP_SCROLLBAR_X, VP_ROW_H,
                                  VP_COL_HIGHLIGHT);

            char label[FS_NAME_LEN + 2];
            if (e->type == FS_DIR) {
                label[0] = '/';
                snprintf(label + 1, sizeof(label) - 1, "%s", e->name);
            } else {
                snprintf(label, sizeof(label), "%s", e->name);
            }

            u32 color = (e->type == FS_DIR) ? VP_COL_DIR : VP_COL_TEXT;
            u32 clip_bg = VP_COL_BG;
            if (is_flash)
                clip_bg = VP_COL_FLASH;
            else if (is_selected)
                clip_bg = VP_COL_HIGHLIGHT;
            p_draw_text_clipped(label, (float)VP_CHROME_LEFT_TEXT_X, row_y + 2.0f,
                                (float)(VP_SCROLLBAR_X - VP_TEXT_PAD), color, clip_bg,
                                row_y, (float)VP_ROW_H);
        }
    }

    /* Scrollbar */
    {
        float track_x = (float)VP_SCROLLBAR_X;
        float track_y = (float)VP_LIST_Y;
        float track_h = (float)(VP_LIST_CONTENT_BOT - VP_LIST_Y);
        float track_w = (float)VP_SCROLLBAR_W;

        C2D_DrawRectSolid(track_x, track_y, VP_Z_ROW_BG,  track_w, track_h, VP_COL_BTN_BG);
        C2D_DrawRectSolid(track_x, track_y, VP_Z_ROW_BDR, 1, track_h, VP_COL_SEP);

        if (s_listing.count > VP_VISIBLE_ROWS) {
            int total         = s_listing.count;
            int visible       = VP_VISIBLE_ROWS;
            int thumb_h       = (int)(track_h * visible / total);
            if (thumb_h < 12) thumb_h = 12;
            int track_travel  = (int)track_h - thumb_h;
            int thumb_top_off = (track_travel > 0)
                ? (s_scroll_off * track_travel / (total - visible)) : 0;
            float thumb_y = track_y + (float)thumb_top_off;
            C2D_DrawRectSolid(track_x + 1, thumb_y, VP_Z_ROW_TEXT,
                              track_w - 2, (float)thumb_h,
                              (s_fs_drag == 3) ? VP_COL_HIGHLIGHT : VP_COL_FOOTER_TEXT);
        }
    }

    /* Chrome z=VP_Z_CHROME(0.5f) > 行内容 z=VP_Z_ROW_TEXT(0.3f)，盖住溢出内容 */
    p_draw_chrome_footer("back(b)", "player(x)");
    p_draw_chrome_top(s_path, "setting(s)");
}

static void draw_setting_panel(void)
{
    VpSettingsView sv;
    vid_panel_settings_fill_view(&sv);
    int scroll = sv.scroll_offset;

    /* Determine chrome title based on subpage */
    const char *title = "settings";
    if (sv.subpage == VP_SUBPAGE_VIDEO)
        title = "settings > video";
    if (sv.subpage == VP_SUBPAGE_CUSTOM)   title = "settings > custom";
    if (sv.subpage == VP_SUBPAGE_ADVANCED) title = "settings > advanced";

    C2D_DrawRectSolid(0, 0, VP_Z_BG, VP_SCREEN_W, VP_SCREEN_H, VP_COL_BG);

    /* Draw rows — only those visible in the content window */
    int row;
    for (row = 0; row < sv.row_count; row++) {
        const VpSettingRowDesc *d = vid_panel_settings_row(row);
        if (!d) continue;

        /* Virtual Y → screen Y */
        int   vy  = VP_SETTINGS_ROW_Y(row);
        float sy  = (float)(vy - scroll);   /* screen y of row top */
        float sy_bot = sy + (float)VP_SET_OPT_H;

        /* Skip rows fully outside content area */
        if (sy_bot <= (float)VP_SET_CONTENT_Y) continue;
        if (sy      >= (float)VP_SET_CONTENT_BOT) break;

        int   val = sv.values[row];
        float text_y = sy + 5.0f;

        /* Selected-row cursor bar (left edge, 2px wide) */
        if (row == sv.selected_row)
            C2D_DrawRectSolid(0, sy, VP_Z_ROW_BG, 2, (float)VP_SET_OPT_H, VP_COL_ACCENT);

        /* Row label — always draw; scissor clips anything outside content area */
        p_draw_text(d->label, VP_TEXT_PAD + 4, text_y, VP_COL_TEXT);

        switch (d->kind) {
            case VP_SETTING_ROW_TOGGLE:
            case VP_SETTING_ROW_CHOICE: {
                int n  = d->num_opts;
                int cw = vp_set_chip_w(n);
                int j;
                for (j = 0; j < n; j++) {
                    float cx = (float)vp_set_chip_x(n, j);
                    float cy = sy + 2.0f;
                    float ch = (float)(VP_SET_OPT_H - 4);
                    u32 bg = (j == val) ? VP_COL_HIGHLIGHT : VP_COL_BTN_BG;
                    C2D_DrawRectSolid(cx,             cy, VP_Z_ROW_BG,  (float)cw, ch, bg);
                    C2D_DrawRectSolid(cx,             cy, VP_Z_ROW_BDR, (float)cw, 1,  VP_COL_SEP);
                    C2D_DrawRectSolid(cx,             cy + ch - 1, VP_Z_ROW_BDR, (float)cw, 1, VP_COL_SEP);
                    C2D_DrawRectSolid(cx,             cy, VP_Z_ROW_BDR, 1, ch, VP_COL_SEP);
                    C2D_DrawRectSolid(cx+(float)cw-1, cy, VP_Z_ROW_BDR, 1, ch, VP_COL_SEP);
                    p_draw_text(d->opt_labels[j], cx + 4.0f, cy + 3.0f,
                                (j == val) ? VP_COL_TEXT : VP_COL_FOOTER_TEXT);
                }
                break;
            }
            case VP_SETTING_ROW_SLIDER: {
                int   range  = d->slider_max - d->slider_min;
                float sx2    = (float)VP_SET_CTRL_X;
                float val_x  = (float)(VP_SET_SCROLLBAR_X - 26);
                float sw     = val_x - sx2 - 2.0f;
                float sty    = sy + (float)(VP_SET_OPT_H - VP_SET_SLIDER_TRACK_H) * 0.5f;
                if (sw < 8.0f) sw = 8.0f;
                C2D_DrawRectSolid(sx2, sty, VP_Z_ROW_BG,  sw, (float)VP_SET_SLIDER_TRACK_H, VP_COL_BTN_BG);
                C2D_DrawRectSolid(sx2, sty, VP_Z_ROW_BDR, sw, 1, VP_COL_SEP);
                C2D_DrawRectSolid(sx2, sty + (float)VP_SET_SLIDER_TRACK_H - 1, VP_Z_ROW_BDR, sw, 1, VP_COL_SEP);
                if (range > 0) {
                    float t  = (float)(val - d->slider_min) / (float)range;
                    float fw = sw * t;
                    if (fw > 0)
                        C2D_DrawRectSolid(sx2, sty, VP_Z_ROW_BDR, fw,
                                          (float)VP_SET_SLIDER_TRACK_H, VP_COL_PROGRESS_FILL);
                    float tx = sx2 + fw - (float)VP_SET_SLIDER_THUMB_W * 0.5f;
                    if (tx < sx2) tx = sx2;
                    if (tx > sx2 + sw - (float)VP_SET_SLIDER_THUMB_W)
                        tx = sx2 + sw - (float)VP_SET_SLIDER_THUMB_W;
                    C2D_DrawRectSolid(tx, sy + 2.0f, VP_Z_ROW_BDR,
                                      (float)VP_SET_SLIDER_THUMB_W, (float)(VP_SET_OPT_H - 4),
                                      VP_COL_PROGRESS_HEAD);
                }
                char vbuf[16];
                snprintf(vbuf, sizeof(vbuf), "%d", val);
                p_draw_text(vbuf, val_x, text_y, VP_COL_FOOTER_TEXT);
                break;
            }
            case VP_SETTING_ROW_SUBMENU:
                p_draw_text(">", (float)(VP_SET_SCROLLBAR_X - VP_TEXT_PAD - 8),
                            text_y, VP_COL_FOOTER_TEXT);
                break;
        }
    }

    if (sv.row_count == 0)
        p_draw_text("(no settings)", VP_TEXT_PAD + 4,
                    (float)(VP_SET_CONTENT_Y + 8), VP_COL_FOOTER_TEXT);

    /* ── Scrollbar（在内容区 scissor 范围内绘制，不受影响）──────────── */
    {
        float track_x = (float)VP_SET_SCROLLBAR_X;
        float track_y = (float)VP_SET_CONTENT_Y;
        float track_h = (float)VP_SET_CONTENT_H;
        float track_w = (float)VP_SET_SCROLLBAR_W;

        C2D_DrawRectSolid(track_x, track_y, VP_Z_ROW_BG,  track_w, track_h, VP_COL_BTN_BG);
        C2D_DrawRectSolid(track_x, track_y, VP_Z_ROW_BDR, 1, track_h, VP_COL_SEP);

        if (sv.total_height > sv.content_h) {
            int total   = sv.total_height;
            int visible = sv.content_h;
            int thumb_h = (int)(track_h * visible / total);
            if (thumb_h < 12) thumb_h = 12;
            int track_travel  = (int)track_h - thumb_h;
            int thumb_top_off = (track_travel > 0)
                ? (scroll * track_travel / (total - visible)) : 0;
            float thumb_y = track_y + (float)thumb_top_off;
            C2D_DrawRectSolid(track_x + 1, thumb_y, VP_Z_ROW_TEXT,
                              track_w - 2, (float)thumb_h,
                              (s_set_drag == 3) ? VP_COL_HIGHLIGHT : VP_COL_FOOTER_TEXT);
        }
    }

    /* Chrome z=VP_Z_CHROME(0.5f) > 行内容 z=VP_Z_ROW_TEXT(0.3f)，盖住溢出内容 */
    p_draw_chrome_footer("back(b)", "topos(y)");
    p_draw_chrome_top(title, "toggle(s)");
}


/* ──────────────────────────────────────────────────────────────────────────────
 * Public API
 * ────────────────────────────────────────────────────────────────────────────*/

void Vid_panel_init(VidPanelCtx ctx)
{
    s_ctx = ctx;

    /* Font: try CJK fonts first so Chinese filenames display correctly */
    s_font = C2D_FontLoadSystem(CFG_REGION_CHN);
    if (!s_font) s_font = C2D_FontLoadSystem(CFG_REGION_TWN);
    if (!s_font) {
        CFG_Region region = CFG_REGION_USA;
        cfguInit();
        CFGU_SecureInfoGetRegion(&region);
        cfguExit();
        s_font = C2D_FontLoadSystem(region);
    }
    if (!s_font) s_font = C2D_FontLoadSystem(CFG_REGION_USA);

    s_tbuf = C2D_TextBufNew(VP_TEXT_BUF_CHARS);

    vid_panel_settings_init();

    {
        char root0[FS_PATH_LEN];

        panel_effective_files_root(root0, sizeof(root0));
        snprintf(s_path, sizeof(s_path), "%s", root0);
    }
    panel_reload_listing();

    s_panel_before_setting = VID_PANEL_PLAYER;
    if (s_ctx.panel)
        *s_ctx.panel = VID_PANEL_PLAYER;
    else
        vid_player.panel = VID_PANEL_PLAYER;

    s_files_play_err_ok = Draw_get_empty_image();
    s_files_play_err_ok.selected = false;
}

void Vid_panel_exit(void)
{
    if (s_tbuf) { C2D_TextBufDelete(s_tbuf); s_tbuf = NULL; }
    if (s_font)  { C2D_FontFree(s_font);        s_font  = NULL; }
    s_touch_x0 = s_touch_y0 = -1;
    s_quick_menu_open = false;
    s_files_play_err_visible     = false;
    s_files_play_err_ok.selected = false;
}

void Vid_panel_draw_overlay(void)
{
    if (!s_quick_menu_open || !s_tbuf) return;
    if (vid_player.panel != VID_PANEL_SETTING) return;

    C2D_TextBufClear(s_tbuf);

    const float mx = (float)VP_OVERLAY_MARGIN;
    const float pw = (float)VP_SCREEN_W - mx * 2.0f;
    const float ph = (float)VP_OVERLAY_PANEL_H;
    const float py = ((float)VP_SCREEN_H - ph) * 0.5f;

    /* 全屏半透明遮罩：盖住整个设置界面，Lawvere 对话框再叠在上面 */
    C2D_DrawRectSolid(0.0f, 0.0f, VP_Z_OVERLAY_DIM,
                      (float)VP_SCREEN_W, (float)VP_SCREEN_H,
                      C2D_Color32(0x00, 0x00, 0x00, 0xA0));

    /* Panel fill */
    C2D_DrawRectSolid(mx + 1, py + 1, VP_Z_OVERLAY_LAWVERE, pw - 2, ph - 2, VP_COL_OVERLAY_BG);
    /* Border */
    C2D_DrawRectSolid(mx,          py,          VP_Z_OVERLAY_LAWVERE, pw, 1,  VP_COL_SEP);
    C2D_DrawRectSolid(mx,          py + ph - 1, VP_Z_OVERLAY_LAWVERE, pw, 1,  VP_COL_SEP);
    C2D_DrawRectSolid(mx,          py,          VP_Z_OVERLAY_LAWVERE, 1,  ph, VP_COL_SEP);
    C2D_DrawRectSolid(mx + pw - 1, py,          VP_Z_OVERLAY_LAWVERE, 1,  ph, VP_COL_SEP);

    /* Centered text (horizontal: panel center; vertical: block centered in panel) */
    const char *lines[] = {
        "software created by",
        "William Lawvere",
    };
    float line_h = VP_FONT_SCALE * 24.0f;
    int   n      = (int)(sizeof(lines) / sizeof(lines[0]));
    float total  = n * line_h;
    float start_y = py + (ph - total) * 0.5f;
    const float cx = mx + pw * 0.5f;

    for (int i = 0; i < n; i++) {
        C2D_Text t;
        if (s_font) C2D_TextFontParse(&t, s_font, s_tbuf, lines[i]);
        else        C2D_TextParse(&t, s_tbuf, lines[i]);
        C2D_TextOptimize(&t);
        u32 col = (i == 1) ? VP_COL_TEXT : VP_COL_FOOTER_TEXT;
        C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, cx, start_y + i * line_h,
                     VP_Z_OVERLAY_LAWVERE, VP_FONT_SCALE, VP_FONT_SCALE, col);
    }
}

void Vid_panel_toggle_quick_menu(void)
{
    if (vid_player.panel != VID_PANEL_SETTING)
        return;
    s_quick_menu_open = !s_quick_menu_open;
}

void Vid_panel_draw_player_chrome_bg(void)
{
    if (vid_player.state == PLAYER_STATE_IDLE)
        s_seek_x = -1;

    /* Top strip background */
    C2D_DrawRectSolid(0, 0, 0.5f, VP_SCREEN_W, VP_PATH_BAR_H, VP_COL_PATH_BG);
    C2D_DrawRectSolid(0, VP_PATH_BAR_H, 0.5f, VP_SCREEN_W, 1, VP_COL_SEP);

    /* Progress bar + seek line (shared renderer, same visual as draw_player_panel) */
    p_draw_progress_bar(vid_player.media_current_pos,
                        vid_player.media_duration, s_seek_x);

    /* Bottom strip background */
    float fy = (float)(VP_SCREEN_H - VP_FOOTER_H);
    C2D_DrawRectSolid(0, fy - 1, 0.5f, VP_SCREEN_W, 1, VP_COL_SEP);
    C2D_DrawRectSolid(0, fy, 0.5f, VP_SCREEN_W, (float)VP_FOOTER_H,
                      VP_COL_FOOTER_BG);
}

void Vid_panel_draw_player_chrome(void)
{
    if (!s_tbuf) return;
    C2D_TextBufClear(s_tbuf);

    int is_paused = (vid_player.state != PLAYER_STATE_PLAYING
                  && vid_player.state != PLAYER_STATE_BUFFERING);

    /* Top-left: basename — always shown when playing (not gated by ui mod). */
    if (vid_player.state != PLAYER_STATE_IDLE && vid_player.file.name[0] != '\0')
        p_draw_text_bar(vid_player.file.name, (float)VP_CHROME_TOP_TEXT_LEFT_X,
                        (float)VP_CHROME_TOP_TEXT_RIGHT_X, VP_COL_TEXT, true);

    /* Top-right: settings（与 Start 一致） */
    p_draw_button((float)VP_SW_BTN_X, (float)VP_TOP_BTN_Y,
                  (float)VP_SW_BTN_W, (float)VP_TOP_BTN_H, "setting(s)");

    /* FPS — only in full ui mod */
    if (vid_player.ui_mod) {
        char fps_buf[16];
        snprintf(fps_buf, sizeof(fps_buf), "%02" PRIu32 "fps",
                 (uint32_t)Draw_query_fps());
        C2D_Text ft;
        if (s_font)
            C2D_TextFontParse(&ft, s_font, s_tbuf, fps_buf);
        else
            C2D_TextParse(&ft, s_tbuf, fps_buf);
        C2D_TextOptimize(&ft);
        float ftw, fth;
        C2D_TextGetDimensions(&ft, DEF_DRAW_TEXT_C2D_SCALE, DEF_DRAW_TEXT_C2D_SCALE, &ftw, &fth);
        float ftx = (float)(VP_SW_BTN_X + VP_SW_BTN_W) - (float)VP_CHROME_BTN_MARGIN_X - ftw
                    + (float)VP_PLAYER_FPS_DX;
        /* 与 V 编解码首行同 y；字号与 Draw(DEF_DRAW_TEXT_SCALE) 在 draw.c 内 ×1.2 后一致；x 仍为 setting 侧右对齐。 */
        float fty = (float)VP_PLAYER_STATUS_ROW_V_Y;
        C2D_DrawText(&ft, C2D_WithColor, ftx, fty, VP_Z_CHR_TEXT,
                     DEF_DRAW_TEXT_C2D_SCALE, DEF_DRAW_TEXT_C2D_SCALE, VP_COL_FPS);
    }

    /* Footer buttons */
    float fy = (float)(VP_SCREEN_H - VP_FOOTER_H);
    p_draw_button((float)VP_BACK_BTN_X, fy + (float)VP_FOOTER_BTN_PAD_Y,
                  (float)VP_BACK_BTN_W, (float)VP_FOOTER_BTN_H,
                  is_paused ? "play(a)" : "pause(a)");
    p_draw_button((float)VP_SET_BTN_X, fy + (float)VP_FOOTER_BTN_PAD_Y,
                  (float)VP_SET_BTN_W, (float)VP_FOOTER_BTN_H, "file(x)");
}

void Vid_panel_draw_bottom(void)
{
    if (!s_tbuf) return;
    C2D_TextBufClear(s_tbuf);

    switch (vid_player.panel) {
        case VID_PANEL_FILES:   draw_files_panel();   break;
        case VID_PANEL_SETTING: draw_setting_panel(); break;
        default: break;
    }
}

/* ── Navigation ── */

void Vid_panel_go_files(void)
{
    s_seek_x          = -1;
    s_quick_menu_open = false;
    vid_player.panel  = VID_PANEL_FILES;
    vid_player.auto_full_screen_count = 0;
}

void Vid_panel_go_player(void)
{
    s_quick_menu_open = false;
    vid_player.panel = VID_PANEL_PLAYER;
    vid_player.auto_full_screen_count = 0;
}

void Vid_panel_toggle_player_files(void)
{
    s_seek_x = -1;
    if (vid_player.panel == VID_PANEL_FILES)
        Vid_panel_go_player();
    else if (vid_player.panel == VID_PANEL_PLAYER)
        Vid_panel_go_files();
}

void Vid_panel_go_settings(void)
{
    s_seek_x               = -1;
    s_quick_menu_open      = false;
    s_set_drag             = 0;
    s_slider_row           = -1;
    s_panel_before_setting = vid_player.panel;
    vid_player.panel       = VID_PANEL_SETTING;
    vid_player.auto_full_screen_count = 0;
}

void Vid_panel_leave_settings(void)
{
    if (vid_player.panel != VID_PANEL_SETTING)
        return;
    s_quick_menu_open = false;
    s_set_drag    = 0;
    s_slider_row  = -1;
    /* Keep settings subpage / selection / scroll across panel switches */
    vid_panel_settings_slider_drag_abort();
    if (s_panel_before_setting == VID_PANEL_FILES)
        Vid_panel_go_files();
    else
        Vid_panel_go_player();
}

void Vid_panel_back(void)
{
    switch (vid_player.panel) {
        case VID_PANEL_SETTING:
            /* B / back(b)：仅子菜单内返回，根目录不离开设置 */
            if (vid_panel_settings_can_go_back())
                vid_panel_settings_go_back();
            break;
        case VID_PANEL_FILES: {
            char root[FS_PATH_LEN];

            /* Stop at effective root: sdmc:/ 或媒体目录（movies / 旧 movie，存在时）。 */
            panel_effective_files_root(root, sizeof(root));
            if (fs_path_up_bounded(s_path, root))
                panel_reload_listing();
            break;
        }
        default:
            vid_player.panel = VID_PANEL_PLAYER;
            vid_player.auto_full_screen_count = 0;
            break;
    }
}

/* ── File list touch input ── */

static int row_from_y(int py)
{
    if (py < VP_LIST_Y || py >= VP_LIST_BOTTOM_EXCL) return -1;
    return s_scroll_off + (py - VP_LIST_Y) / VP_ROW_H;
}

static int hit_top_switch(int px, int py)
{
    return (py >= VP_TOP_BTN_Y && py <= VP_TOP_BTN_Y + VP_TOP_BTN_H
         && px >= VP_SW_BTN_X && px <= VP_SW_BTN_X + VP_SW_BTN_W);
}

static int hit_back_btn(int px, int py)
{
    int fy = VP_SCREEN_H - VP_FOOTER_H;
    return (py >= fy + VP_FOOTER_BTN_PAD_Y
         && py <= fy + VP_FOOTER_BTN_PAD_Y + VP_FOOTER_BTN_H
         && px >= VP_BACK_BTN_X
         && px <= VP_BACK_BTN_X + VP_BACK_BTN_W);
}

static int hit_set_btn(int px, int py)
{
    int fy = VP_SCREEN_H - VP_FOOTER_H;
    return (py >= fy + VP_FOOTER_BTN_PAD_Y
         && py <= fy + VP_FOOTER_BTN_PAD_Y + VP_FOOTER_BTN_H
         && px >= VP_SET_BTN_X
         && px <= VP_SET_BTN_X + VP_SET_BTN_W);
}

/* Lawvere 弹窗区域（设置面板），与 Vid_panel_draw_overlay 几何一致 */
static int vp_quick_overlay_hit(int px, int py)
{
    const int mx  = VP_OVERLAY_MARGIN;
    const int pw  = VP_SCREEN_W - mx * 2;
    const int ph  = VP_OVERLAY_PANEL_H;
    const int py0 = (VP_SCREEN_H - ph) / 2;
    return (px >= mx && px < mx + pw && py >= py0 && py < py0 + ph);
}

/* Player：顶/底条带、三个 chrome 按钮、进度条命中区（与绘制一致） */
static int vp_player_hit_chrome_controls(int px, int py)
{
    if (py < VP_LIST_Y)
        return 1;
    if (py >= VP_SCREEN_H - VP_FOOTER_H - 1)
        return 1;
    if (vp_progress_hit(px, py))
        return 1;
    if (hit_top_switch(px, py) || hit_back_btn(px, py) || hit_set_btn(px, py))
        return 1;
    return 0;
}

bool Vid_panel_player_touch_resets_auto_dim(int px, int py)
{
    if (vid_player.panel != VID_PANEL_PLAYER)
        return false;
    if (s_seek_x >= 0)
        return true;
    return vp_player_hit_chrome_controls(px, py) != 0;
}

void Vid_panel_list_press(int px, int py)
{
    s_touch_x0    = px;
    s_touch_y0    = py;
    s_touch_off0  = s_scroll_off;
    s_touch_state = 1;   /* tentative tap */
    s_press_row   = -1;

    if (vid_player.panel == VID_PANEL_FILES) {
        s_fs_touch_y0 = py;
        s_fs_scroll0  = s_scroll_off;
        if (px >= VP_SCROLLBAR_X
            && py >= VP_LIST_Y && py < VP_LIST_CONTENT_BOT)
            s_fs_drag = 3;  /* direct scrollbar drag */
        else
            s_fs_drag = 1;  /* tentative: may become content drag */
    } else if (vid_player.panel == VID_PANEL_SETTING) {
        if (s_quick_menu_open && vp_quick_overlay_hit(px, py)) {
            s_quick_menu_open = false;
            s_touch_x0    = s_touch_y0 = -1;
            s_touch_state = 0;
            return;
        }
        /* Record scroll position at press for content drag */
        VpSettingsView sv;
        vid_panel_settings_fill_view(&sv);
        s_set_touch_y0 = py;
        s_set_scroll0  = sv.scroll_offset;
        /* Distinguish scrollbar column from content column */
        if (px >= VP_SET_SCROLLBAR_X
            && py >= VP_SET_CONTENT_Y && py < VP_SET_CONTENT_BOT)
        {
            s_set_drag = 3;  /* start directly as scrollbar drag */
        }
        else
        {
            s_set_drag = 1;  /* tentative: may become content drag */

            /* ── Press-down 即时触发行 widget ──────────────────────────
             * 跳过 chrome 区域（顶部 + 底部），只处理内容区 press。
             * 滚动条列已在上面分支排除（px < VP_SET_SCROLLBAR_X）。
             * 若 press 命中一个 widget，立即执行并标记为"已在 press 消费"，
             * release 不会重复触发。 */
            if (!hit_top_switch(px, py) && !hit_back_btn(px, py) && !hit_set_btn(px, py))
            {
                int row, value;
                if (vid_panel_settings_resolve_touch(px, py, &row, &value)) {
                    const VpSettingRowDesc *d = vid_panel_settings_row(row);
                    if (d) {
                        if (d->kind == VP_SETTING_ROW_SUBMENU) {
                            vid_panel_settings_open_submenu_row(row);
                            s_set_drag = -1;
                        } else if (d->kind == VP_SETTING_ROW_SLIDER) {
                            /* 滑块：松手或横向拖动才定值；拖动过程跟手更新 */
                            s_slider_row = row;
                            s_set_drag   = 5;
                        } else {
                            vid_panel_settings_apply_change(row, value);
                            s_set_drag = -1;
                        }
                    }
                }
            }
        }
    } else if (vid_player.panel == VID_PANEL_PLAYER) {
        /* 点主内容区（非 chrome/进度条/上下条带）：与 Select 相同，触发下屏息屏 */
        if (!vp_player_hit_chrome_controls(px, py)) {
            (void)Vid_cmd_push((VidCmd){ .id = VID_CMD_ENTER_FULLSCREEN });
            /* 息屏后路由不再投递 LIST_RELEASE，避免残留 press 状态 */
            s_touch_x0 = s_touch_y0 = -1;
            s_touch_state = 0;
            return;
        }
        /* Start a seek drag if the press is within the progress bar slop zone */
        if (vp_progress_hit(px, py)) {
            s_seek_x = vp_progress_seek_x(px);
            (void)Vid_cmd_push((VidCmd){ .id   = VID_CMD_SEEK_BAR_DRAG,
                                         .iarg = (int32_t)s_seek_x });
        }
    }
}

/* Called on physical press-down: flash the row under the finger immediately */
void Vid_panel_list_press_flash(int px, int py)
{
    if (vid_player.panel != VID_PANEL_FILES) return;
    /* Ignore chrome and scrollbar column */
    if (hit_top_switch(px, py) || hit_back_btn(px, py) || hit_set_btn(px, py)
        || px >= VP_SCROLLBAR_X)
    { s_press_row = -1; return; }
    int row = row_from_y(py);
    if (row < 0 || row >= s_listing.count) { s_press_row = -1; return; }
    s_press_row   = row;
    s_flash_row   = row;
    s_flash_until = (u64)osGetTime() + VP_FLASH_MS_PRESS;
}

/* Files scrollbar drag: map touch_y → s_scroll_off (mirrors vid_panel_settings_scroll_to_bar) */
static void fs_scroll_to_bar(int touch_y)
{
    int total = s_listing.count;
    if (total <= VP_VISIBLE_ROWS) return;
    int visible      = VP_VISIBLE_ROWS;
    int track_h      = VP_LIST_CONTENT_BOT - VP_LIST_Y;
    int thumb_h      = track_h * visible / total;
    if (thumb_h < 12) thumb_h = 12;
    int track_travel = track_h - thumb_h;
    if (track_travel <= 0) return;
    int rel = touch_y - VP_LIST_Y - thumb_h / 2;
    if (rel < 0) rel = 0;
    if (rel > track_travel) rel = track_travel;
    s_scroll_off = rel * (total - visible) / track_travel;
    panel_clamp_scroll();
}

void Vid_panel_list_scroll(int px, int py)
{
    /* Player panel: update seek drag while held */
    if (vid_player.panel == VID_PANEL_PLAYER && s_seek_x >= 0) {
        s_seek_x = vp_progress_seek_x(px);
        (void)Vid_cmd_push((VidCmd){ .id   = VID_CMD_SEEK_BAR_DRAG,
                                     .iarg = (int32_t)s_seek_x });
        return;
    }

    /* Files panel: handle content drag and scrollbar drag */
    if (vid_player.panel == VID_PANEL_FILES) {
        if (s_fs_drag == 3) {
            fs_scroll_to_bar(py);
        } else if (s_fs_drag == 1) {
            if (abs(py - s_fs_touch_y0) > VP_SCROLL_THRESH)
                s_fs_drag = 2;
        }
        if (s_fs_drag == 2) {
            int delta    = (s_fs_touch_y0 - py) / VP_ROW_H;
            s_scroll_off = s_fs_scroll0 + delta;
            panel_clamp_scroll();
        }
        return;
    }

    /* Settings panel: handle content drag and scrollbar drag */
    if (vid_player.panel == VID_PANEL_SETTING) {
        if (s_set_drag == 4 && s_slider_row >= 0) {
            int v = vid_panel_settings_slider_value_at_x(px, s_slider_row);
            vid_panel_settings_apply_change(s_slider_row, v);
            return;
        }
        if (s_set_drag == 5 && s_slider_row >= 0) {
            int dx = abs(px - s_touch_x0);
            int dy = abs(py - s_set_touch_y0);
            if (dx > VP_SCROLL_THRESH || dy > VP_SCROLL_THRESH) {
                if (dx >= dy) {
                    vid_panel_settings_slider_drag_begin();
                    s_set_drag = 4;
                    vid_panel_settings_apply_change(s_slider_row,
                        vid_panel_settings_slider_value_at_x(px, s_slider_row));
                } else {
                    s_slider_row = -1;
                    s_set_drag   = 2;
                    vid_panel_settings_scroll_set(s_set_scroll0 + (s_set_touch_y0 - py));
                }
            }
            return;
        }
        if (s_set_drag == 3) {
            vid_panel_settings_scroll_to_bar(py);
        } else if (s_set_drag == 1) {
            /* 手指移动超过阈值 → 升级为内容拖拽，取消 press 消费标记 */
            if (abs(py - s_set_touch_y0) > VP_SCROLL_THRESH)
                s_set_drag = 2;
        }
        /* s_set_drag == -1（press 已消费）时不参与滚动 */
        if (s_set_drag == 2) {
            vid_panel_settings_scroll_set(s_set_scroll0 + (s_set_touch_y0 - py));
        }
        return;
    }

    if (s_touch_y0 < 0) return;
    int moved = abs(py - s_touch_y0);
    if (s_touch_state == 1 && moved > VP_SCROLL_THRESH) {
        s_touch_state = 2;
        s_press_row   = -1;
        s_flash_row   = -1;
        s_flash_until = 0;
    }
    if (s_touch_state == 2) {
        int delta    = (s_touch_y0 - py) / VP_ROW_H;
        s_scroll_off = s_touch_off0 + delta;
        panel_clamp_scroll();
    }
}

void Vid_panel_list_release(int px, int py)
{
    int tap_x = (s_touch_x0 >= 0) ? s_touch_x0 : px;
    int tap_y = (s_touch_y0 >= 0) ? s_touch_y0 : py;
    int was_tap = (s_touch_state == 1);
    s_touch_x0    = s_touch_y0 = -1;
    s_touch_state = 0;
    s_press_row   = -1;

    /* Chrome button taps per panel */
    if (vid_player.panel == VID_PANEL_PLAYER) {
        /* Commit seek on release (whether it was a tap or a drag) */
        if (s_seek_x >= 0) {
            (void)Vid_cmd_push((VidCmd){ .id = VID_CMD_SEEK_BAR_COMMIT });
            s_seek_x = -1;
            return;
        }
        if (!was_tap) return;
        if (hit_top_switch(tap_x, tap_y)) { Vid_panel_go_settings();         return; }
        if (hit_back_btn(tap_x, tap_y)) {
            (void)Vid_cmd_push((VidCmd){ .id = VID_CMD_TOGGLE_PLAY });
            return;
        }
        if (hit_set_btn(tap_x, tap_y))  { Vid_panel_toggle_player_files(); return; }
        return;
    } else if (vid_player.panel == VID_PANEL_FILES) {
        int was_fs_drag = s_fs_drag;
        s_fs_drag = 0;

        if (hit_top_switch(tap_x, tap_y)) { Vid_panel_go_settings(); return; }
        if (hit_back_btn(tap_x, tap_y))   { Vid_panel_back();        return; }
        if (hit_set_btn(tap_x, tap_y))    { Vid_panel_go_player();   return; }

        /* Scrollbar column: snap on tap, ignore on drag */
        if (tap_x >= VP_SCROLLBAR_X) {
            if (was_fs_drag < 2) fs_scroll_to_bar(tap_y);
            return;
        }

        /* Content area: only process taps (not drags) */
        if (was_fs_drag >= 2) return;
        if (!was_tap) return;

        /* List item tap: first tap selects, second tap on same selected row opens */
        int row = row_from_y(tap_y);
        if (row < 0 || row >= s_listing.count) return;

        if (row == s_selected) {
            /* Second tap on already-selected row: open it */
            s_flash_row   = row;
            s_flash_until = (u64)osGetTime() + VP_FLASH_MS_TAP;
            const FsEntry *e = &s_listing.entries[row];
            if (e->type == FS_DIR) {
                fs_path_enter(s_path, e->name);
                panel_reload_listing();
            } else {
                panel_open_file(e->name, s_path);
            }
        } else {
            /* First tap: just select and highlight */
            s_selected    = row;
            s_flash_row   = row;
            s_flash_until = (u64)osGetTime() + VP_FLASH_MS_TAP;
        }
        return;
    } else if (vid_player.panel == VID_PANEL_SETTING) {
        /* Reset settings drag state */
        int was_set_drag = s_set_drag;
        s_set_drag = 0;

        if (was_set_drag == 4) {
            vid_panel_settings_slider_drag_end();
            s_slider_row = -1;
            return;
        }
        if (was_set_drag == 5 && s_slider_row >= 0) {
            int v = vid_panel_settings_slider_value_at_x(tap_x, s_slider_row);
            vid_panel_settings_apply_change(s_slider_row, v);
            s_slider_row = -1;
            return;
        }
        s_slider_row = -1;

        /* Chrome 按钮：始终在 release 时响应（back/setting 导航）——
         * 这些不是行 widget，不受 press-down 消费逻辑影响。 */
        if (hit_top_switch(tap_x, tap_y)) {
            Vid_panel_leave_settings();
            return;
        }
        if (hit_back_btn(tap_x, tap_y)) {
            if (vid_panel_settings_can_go_back())
                vid_panel_settings_go_back();
            return;
        }
        if (hit_set_btn(tap_x, tap_y)) {
            Vid_panel_toggle_quick_menu();
            return;
        }

        /* was_set_drag == -1：行 widget 已在 press-down 时消费，release 不重触发 */
        if (was_set_drag == -1) return;

        /* 拖拽过内容区：不触发行 widget */
        if (was_set_drag >= 2) return;

        /* 滚动条列 tap（非拖拽）——snap 到位置 */
        if (tap_x >= VP_SET_SCROLLBAR_X) {
            vid_panel_settings_scroll_to_bar(tap_y);
            return;
        }

        /* 以下为 fallback：理论上 press 已提前消费，
         * 仅当 press 因某种原因未命中 widget 时才到达此处 */
        {
            int row, value;
            if (vid_panel_settings_resolve_touch(tap_x, tap_y, &row, &value)) {
                const VpSettingRowDesc *d = vid_panel_settings_row(row);
                if (d && d->kind == VP_SETTING_ROW_SUBMENU) {
                    vid_panel_settings_open_submenu_row(row);
                } else {
                    vid_panel_settings_apply_change(row, value);
                }
            }
        }
        return;
    }
}

void Vid_panel_list_go_up(void)
{
    if (vid_player.panel == VID_PANEL_FILES) {
        Vid_panel_back();
    } else if (vid_player.panel == VID_PANEL_SETTING) {
        if (vid_panel_settings_can_go_back())
            vid_panel_settings_go_back();
    }
}

void Vid_panel_confirm(void)
{
    if (vid_player.panel != VID_PANEL_FILES) return;
    if (s_selected < 0 || s_selected >= s_listing.count) return;

    s_flash_row   = s_selected;
    s_flash_until = (u64)osGetTime() + VP_FLASH_MS_TAP;

    const FsEntry *e = &s_listing.entries[s_selected];
    if (e->type == FS_DIR) {
        fs_path_enter(s_path, e->name);
        panel_reload_listing();
    } else {
        panel_open_file(e->name, s_path);
    }
}

void Vid_panel_nav_select(int delta)
{
    if (vid_player.panel != VID_PANEL_FILES) return;
    if (s_listing.count == 0) return;

    s_selected += delta;
    if (s_selected < 0)                   s_selected = 0;
    if (s_selected >= s_listing.count)    s_selected = s_listing.count - 1;

    /* 让选中行保持在可见区域内 */
    if (s_selected < s_scroll_off)
        s_scroll_off = s_selected;
    else if (s_selected >= s_scroll_off + VP_VISIBLE_ROWS)
        s_scroll_off = s_selected - VP_VISIBLE_ROWS + 1;

    panel_clamp_scroll();
}

/* ── Settings panel keyboard control ────────────────────────────────────────
 * 上下键：移动高亮选中行，自动滚动到可见区域
 * 左右键：对当前行的值做 ±1 调整（toggle/choice 循环、slider 步进）
 * A 键：仅打开子菜单行；非子菜单行按 A 不改变选项（改值须左右键）
 * ─────────────────────────────────────────────────────────────────────────*/
void Vid_panel_settings_kbd_nav(int delta)
{
    if (vid_player.panel != VID_PANEL_SETTING) return;
    vid_panel_settings_move_selection(delta);
}

void Vid_panel_settings_kbd_adjust(int delta)
{
    if (vid_player.panel != VID_PANEL_SETTING) return;
    int row = vid_panel_settings_get_selected();
    const VpSettingRowDesc *d = vid_panel_settings_row(row);
    if (!d) return;
    /* 子菜单行：无值可改 */
    if (d->kind == VP_SETTING_ROW_SUBMENU) return;
    vid_panel_settings_adjust(row, delta);
}

void Vid_panel_settings_kbd_submenu_a(void)
{
    if (vid_player.panel != VID_PANEL_SETTING) return;
    vid_panel_settings_open_submenu_row(vid_panel_settings_get_selected());
}

/* ── 统一底屏渲染入口 ──────────────────────────────────────────────────────────
 * 替代 vid_draw.c 原来的 if(config.is_bottom_lcd_on) 大块。
 * vid_draw.c 每帧调用此函数一次即可完成底屏全部绘制。
 *
 * 参数说明见 vid_panel.h。
 * ────────────────────────────────────────────────────────────────────────────*/
void Vid_panel_draw_bottom_screen(bool     is_bottom_lcd_on,
                                   uint32_t back_color,
                                   bool     is_err_shown)
{
    if (!is_bottom_lcd_on)
        return;

    if (vid_player.panel != VID_PANEL_PLAYER)
    {
        /* FILES / SETTING 面板 */
        Draw_screen_ready(DRAW_SCREEN_BOTTOM, DEF_DRAW_BLACK);
        Vid_panel_draw_bottom();
        if (is_err_shown)
            Util_err_draw();
        p_draw_files_play_error_overlay();
        /* Lawvere 最后绘制：z 高于 chrome，且盖住 Util_err / 文件错误层 */
        Vid_panel_draw_overlay();
    }
    else
    {
        /* PLAYER 面板 */
        Sem_config config = { 0, };
        Sem_get_config(&config);

        uint32_t color      = config.is_night ? DEF_DRAW_WHITE : DEF_DRAW_BLACK;
        uint64_t current_ts = osGetTime();

        /* 底屏坐标偏移（与 vid_draw.c 保持一致：x-=40, y-=240） */
        double video_x_offset_bot = vid_player.video_x_offset[EYE_LEFT] - 40.0;
        double video_y_offset_bot = vid_player.video_y_offset[EYE_LEFT] - 240.0;

        /* 当前帧缓冲索引（左眼） */
        uint8_t image_index_left = (vid_player.next_draw_index[EYE_LEFT] > 0)
            ? (vid_player.next_draw_index[EYE_LEFT] - 1)
            : (VIDEO_BUFFERS - 1);

        /* 图像尺寸（SAR 与 zoom 已由 vid_draw.c 调用前算好，此处重算以保持独立性） */
        double sar_w = vid_player.video_info[EYE_LEFT].sar_width;
        double sar_h = vid_player.video_info[EYE_LEFT].sar_height;
        double image_width_left  = vid_player.large_image[image_index_left][EYE_LEFT].image_width  * sar_w * vid_player.video_zoom[EYE_LEFT];
        double image_height_left = vid_player.large_image[image_index_left][EYE_LEFT].image_height * sar_h * vid_player.video_zoom[EYE_LEFT];

        Draw_screen_ready(DRAW_SCREEN_BOTTOM, back_color);
        Vid_panel_draw_player_chrome_bg();

        /* Codec / res / decode / CPU / SBS / preview thumbnail — only when ui mod on */
        if (vid_player.ui_mod)
            Vid_panel_player_draw_status(color, back_color, current_ts,
                video_x_offset_bot, video_y_offset_bot,
                image_width_left, image_height_left, image_index_left);

        Vid_panel_player_draw_timebar(color, current_ts);

        if (is_err_shown)
            Util_err_draw();

        Vid_panel_draw_player_chrome();
    }
}
