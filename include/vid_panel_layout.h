#pragma once

/*
 * 3DS 下屏 (320×240) 三面板 UI 几何常量。
 * 与 vid_panel.c / vid_panel_hit.h 共用，坐标不散落。
 */

#define VP_SCREEN_W 320
#define VP_SCREEN_H 240

#define VP_PATH_BAR_H 24
#define VP_FOOTER_H   22

/* ── 两个内容面板（Files / Settings）共用的滚动条几何 ──────────────────────
 * 滚动条固定贴屏幕右边缘；内容区（行高亮、控件、文字）不超过 VP_SCROLLBAR_X。
 * 滚动条轨道常驻；只有内容超出可视高度时才显示拇指。
 * ────────────────────────────────────────────────────────────────────────── */
#define VP_SCROLLBAR_W   8
#define VP_SCROLLBAR_X   (VP_SCREEN_W - VP_SCROLLBAR_W)   /* = 312 */

/* ── Files 面板 ─────────────────────────────────────────────────────────── */
#define VP_LIST_Y       (VP_PATH_BAR_H + 1)
#define VP_ROW_H        16
#define VP_VISIBLE_ROWS 12

/* 内容区底部（与 Settings 的 VP_SET_CONTENT_BOT 相同） */
#define VP_LIST_CONTENT_BOT (VP_SCREEN_H - VP_FOOTER_H - 1)

/* 文件行可见数上限（由内容区高度推算，和 VP_VISIBLE_ROWS 保持一致） */
#define VP_LIST_BOTTOM_EXCL (VP_LIST_Y + (VP_VISIBLE_ROWS) * (VP_ROW_H))

#define VP_TEXT_PAD 4

/* 顶栏路径 / 文件列表 / Player 标题：文字左缘同列（与列表行 p_draw_text_clipped 的 x 一致） */
#define VP_CHROME_LEFT_TEXT_X ((VP_TEXT_PAD) + 2)

#define VP_SCROLL_THRESH 4

/* 文件列表触控闪反馈（毫秒）。原约 4 / 8 帧 @60Hz，按时间换算后再 ×1.5。 */
#define VP_FLASH_MS_PRESS 100u
#define VP_FLASH_MS_TAP   200u

/* chrome 按钮尺寸（顶部右侧 / 底部左右共用）
 * BTN_H = 18 → 与 FOOTER_H=22 配合，上下各留 2px；与 PATH_BAR_H=24 配合，上下各留 3px。
 * BTN_MARGIN_X = 2 → 按钮距屏幕左/右边缘留 2px 内缩，避免紧贴屏幕边缘。 */
#define VP_CHROME_BTN_W     66
#define VP_CHROME_BTN_H     18
#define VP_CHROME_BTN_MARGIN_X 2

#define VP_FOOTER_BTN_PAD_Y ((VP_FOOTER_H - VP_CHROME_BTN_H) / 2)
#define VP_FOOTER_BTN_H     VP_CHROME_BTN_H

/* ── Chrome 按钮坐标对齐规则 ──────────────────────────────────────────────
 * 右上 (SW) 与右下 (SET) 共用相同的 X 坐标（右对齐，内缩 MARGIN_X）。
 * 左下 (BACK) 与右下 (SET) 共用相同的 Y 坐标（由 VP_FOOTER_BTN_PAD_Y 保证）。
 * 三个按钮宽高均为 VP_CHROME_BTN_W × VP_CHROME_BTN_H；标签在各自矩形内水平居中。
 * ────────────────────────────────────────────────────────────────────────── */

/* 底部左按钮（left-bottom） */
#define VP_BACK_BTN_X VP_CHROME_BTN_MARGIN_X
#define VP_BACK_BTN_W VP_CHROME_BTN_W

/* 底部右按钮（right-bottom）X 与顶部右按钮（right-top）X 相同 */
#define VP_SET_BTN_W  VP_CHROME_BTN_W
#define VP_SET_BTN_X  (VP_SCREEN_W - VP_CHROME_BTN_W - VP_CHROME_BTN_MARGIN_X)

/* 顶部右按钮（right-top）X 与底部右按钮（right-bottom）X 相同 */
#define VP_SW_BTN_W VP_CHROME_BTN_W
#define VP_SW_BTN_X VP_SET_BTN_X
#define VP_TOP_BTN_Y ((VP_PATH_BAR_H - VP_CHROME_BTN_H) / 2)
#define VP_TOP_BTN_H VP_CHROME_BTN_H

/* path bar 文字右边界 */
#define VP_PATH_TEXT_MAX_X (VP_SW_BTN_X - VP_TEXT_PAD)

/* Player 底屏：folder 按钮下方、主内容区右上角 FPS（与 Sem state.msg 同源 Draw_query_fps） */
#define VP_PLAYER_FPS_Y (VP_PATH_BAR_H + 2)
#define VP_PLAYER_FPS_DX (-4) /* 相对右对齐基线再左移 */
#define VP_PLAYER_FPS_DY (4)  /* 相对 VP_PLAYER_FPS_Y 再下移 */

/* 进度条（仅在 Player 面板额外绘制时使用） */
#define VP_PROGRESS_INVALID 10   /* 两端死区宽度（各10px，不到达屏幕边缘） */
#define VP_PROGRESS_H       3
/* 进度条 + 上方「当前/总时长」与右侧 seeking 文案整组上移（像素） */
#define VP_PROGRESS_GROUP_UP_PX 8
#define VP_PROGRESS_Y       (VP_SCREEN_H - VP_FOOTER_H - 10 - VP_PROGRESS_GROUP_UP_PX)
#define VP_PROGRESS_SLOP    12   /* vertical hit-test slop above/below bar */

/* Active region within the bar */
#define VP_PROGRESS_X_MIN   (VP_PROGRESS_INVALID)
#define VP_PROGRESS_X_MAX   (VP_SCREEN_W - 1 - VP_PROGRESS_INVALID)
#define VP_PROGRESS_TOTAL_W (VP_SCREEN_W - VP_PROGRESS_INVALID * 2)

/* Seek-bar hit-test: true when (px,py) is within the progress bar + slop zone */
static inline int vp_progress_hit(int px, int py)
{
    return (px >= VP_PROGRESS_X_MIN && px <= VP_PROGRESS_X_MAX
         && py >= VP_PROGRESS_Y - VP_PROGRESS_SLOP
         && py <= VP_PROGRESS_Y + VP_PROGRESS_H + VP_PROGRESS_SLOP);
}

/* Convert a touch X pixel to a [0,1] progress ratio, clamped */
static inline float vp_progress_x_to_ratio(int px)
{
    float r = (float)(px - VP_PROGRESS_X_MIN) / (float)VP_PROGRESS_TOTAL_W;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    return r;
}

/* Convert a touch X pixel to the seek-line screen X (clamped inside active range) */
static inline int vp_progress_seek_x(int px)
{
    if (px < VP_PROGRESS_X_MIN) return VP_PROGRESS_X_MIN;
    if (px > VP_PROGRESS_X_MAX) return VP_PROGRESS_X_MAX;
    return px;
}

/* 快速菜单覆盖层（中央浮动弹窗） */
#define VP_OVERLAY_PANEL_H 108
#define VP_OVERLAY_MARGIN  20

/* ── Settings 面板 ───────────────────────────────────────────────────────
 * 内容区：顶部条带分隔线下方 ↔ 底部条带分隔线上方。
 * 滚动条：共用 VP_SCROLLBAR_X / VP_SCROLLBAR_W，贴屏幕右边缘。
 * 控件区右边界不超过 VP_SCROLLBAR_X，与 Files 面板对齐。
 * ────────────────────────────────────────────────────────────────────── */

/* 垂直内容区（上下均 exclusive of chrome strips） */
#define VP_SET_CONTENT_Y     (VP_PATH_BAR_H + 1)
#define VP_SET_CONTENT_BOT   (VP_SCREEN_H - VP_FOOTER_H - 1)
#define VP_SET_CONTENT_H     (VP_SET_CONTENT_BOT - VP_SET_CONTENT_Y)

/* 滚动条别名 → 指向共用常量 */
#define VP_SET_SCROLLBAR_W   VP_SCROLLBAR_W
#define VP_SET_SCROLLBAR_X   VP_SCROLLBAR_X

/* 控件可用宽度（滚动条左侧） */
#define VP_SET_CONTENT_W     VP_SCROLLBAR_X

/* 行几何 */
#define VP_SET_OPT_H       22
#define VP_SET_ROW_GAP     6
#define VP_SET_FIRST_ROW_Y (VP_LIST_Y + 4)
#define VP_SET_ROW_STRIDE  (VP_SET_OPT_H + VP_SET_ROW_GAP)

/* 行在虚拟（未滚动）坐标系中的 Y */
#define VP_SETTINGS_ROW_Y(row)  ((VP_SET_FIRST_ROW_Y) + (row) * (VP_SET_ROW_STRIDE))

/* n 行的虚拟总高度 */
#define VP_SET_TOTAL_H(n)  ((VP_SET_FIRST_ROW_Y) + (n) * (VP_SET_ROW_STRIDE) - VP_SET_CONTENT_Y + 4)

/* 控件区：标签右侧开始，右边界不超过滚动条；右侧留给滚动条 */
#define VP_SET_CTRL_X    156
#define VP_SET_CTRL_W    (VP_SCROLLBAR_X - VP_SET_CTRL_X - VP_TEXT_PAD)
#define VP_SET_CHIP_GAP  2

/* Slider 几何 */
#define VP_SET_SLIDER_TRACK_H 6
#define VP_SET_SLIDER_THUMB_W 8

/* Chip 自动布局：n 个 chip 均分控件区 */
static inline int vp_set_chip_w(int n) {
    return (VP_SET_CTRL_W - (n - 1) * VP_SET_CHIP_GAP) / n;
}
static inline int vp_set_chip_x(int n, int i) {
    return VP_SET_CTRL_X + i * (vp_set_chip_w(n) + VP_SET_CHIP_GAP);
}
