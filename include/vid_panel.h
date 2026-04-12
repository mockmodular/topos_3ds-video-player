#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "system/util/hid_types.h"

/* ── 面板枚举（也写入 vid_state.h / Vid_player.panel） ── */
typedef enum
{
    VID_PANEL_PLAYER  = 0,
    VID_PANEL_SETTING = 1,
    VID_PANEL_FILES   = 2,
    VID_PANEL_MAX,
} Vid_panel;

/*
 * ── 依赖注入上下文 ──────────────────────────────────────────────────────────
 * vid_panel.c / vid_panel_menu.c 通过此结构体访问播放器状态，
 * 不直接包含 vid_state.h，使 UI 层可整体替换。
 *
 * 由 vid_lifecycle.c 在 Vid_panel_init() 调用前组装并传入。
 */
typedef struct
{
    void      *player;  /* Vid_player* 整体指针，调用方 cast 回 Vid_player*；
                           用 void* 避免在此头文件中引入 vid_state.h */
    Vid_panel *panel;   /* &vid_player.panel */
    void (*open_file_cb)(const char *filename, const char *directory);
} VidPanelCtx;

/* ── 生命周期 ── */
void Vid_panel_init(VidPanelCtx ctx);
void Vid_panel_exit(void);

/* ── 每帧渲染 ── */
void Vid_panel_draw_bottom(void);             /* FILES / SETTING 面板完整渲染 */
void Vid_panel_draw_player_chrome_bg(void);   /* PLAYER 面板：先画条带背景（底层） */
void Vid_panel_draw_player_chrome(void);      /* PLAYER 面板：后画 chrome 按钮（顶层） */

/* ── 面板导航（由 vid_input.c 的 VID_CMD_PANEL_* 处理器调用） ── */
void Vid_panel_go_files(void);
void Vid_panel_go_player(void);
void Vid_panel_go_settings(void);
void Vid_panel_leave_settings(void);
void Vid_panel_toggle_player_files(void);
void Vid_panel_back(void);

/* After Vid_load_settings(): set file browser path to effective root (媒体目录或 sdmc:/)。 */
void Vid_panel_files_sync_after_settings_load(void);
/* Call when Custom → 根目录 option changes (clamp path + reload if on FILES). */
void Vid_panel_files_on_root_mode_changed(void);

/* ── 文件列表交互（由 vid_input.c 的触摸命令处理器调用） ── */
void Vid_panel_list_press(int px, int py);
void Vid_panel_list_scroll(int px, int py);
void Vid_panel_list_release(int px, int py);
void Vid_panel_list_go_up(void);
void Vid_panel_nav_select(int delta);  /* 正数=向下，负数=向上，自动同步滚动偏移 */
void Vid_panel_confirm(void);          /* A键：打开当前选中项 */
void Vid_panel_list_press_flash(int px, int py); /* 按下瞬间flash高亮 */

/* Player 面板：用于自动 5 秒息屏计时器 —— 仅 chrome/进度条/拖进度中触摸应清零计数 */
bool Vid_panel_player_touch_resets_auto_dim(int px, int py);

/* ── Settings panel keyboard control (called from vid_input.c SET_NAV/VALUE_LEFT/RIGHT) ── */
void Vid_panel_settings_kbd_nav(int delta);     /* 上(-1)/下(+1) 移动选中行 */
void Vid_panel_settings_kbd_adjust(int delta);  /* 左(-1)/右(+1) 修改当前行的值 */
void Vid_panel_settings_kbd_submenu_a(void);    /* A：仅打开子菜单，不改选项值 */

/* ── Lawvere 信息覆盖层（仅设置面板） ── */
void Vid_panel_toggle_quick_menu(void);
void Vid_panel_draw_overlay(void);

/* FILES：从列表打开失败时（路径非空但无法解码）的英文提示 + OK，与 Util_err 独立 */
void Vid_panel_files_show_play_error(void);
bool Vid_panel_files_play_error_is_visible(void);
void Vid_panel_files_play_error_hid(const Hid_info *key);

/*
 * ── 统一底屏渲染入口 ────────────────────────────────────────────────────────
 * 替代 vid_draw.c 中原来的 if(config.is_bottom_lcd_on) 大块。
 * vid_draw.c 每帧调用一次此函数即可完成底屏的全部绘制。
 *
 * 参数：
 *   is_bottom_lcd_on  : config.is_bottom_lcd_on
 *   back_color        : 当前主题背景色
 *   is_err_shown      : Util_err_query_show_flag()
 */
void Vid_panel_draw_bottom_screen(bool     is_bottom_lcd_on,
                                   uint32_t back_color,
                                   bool     is_err_shown);

/* ── 触摸命令坐标打包/解包辅助（在 vid_hid.c / vid_input.c 共用） ── */
static inline uint32_t vid_panel_pack_xy(int px, int py)
{
    return (uint32_t)(((uint32_t)(uint16_t)py << 16) | (uint16_t)(uint32_t)px);
}
static inline void vid_panel_unpack_xy(uint32_t packed, int *px, int *py)
{
    *px = (int)(int16_t)(packed & 0xFFFFu);
    *py = (int)(int16_t)((packed >> 16) & 0xFFFFu);
}
