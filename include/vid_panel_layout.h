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

/* ── 顶栏 path bar 内「路径」与「Player 标题」共用一套水平范围与竖直基准 ──
 * 左、右为文字可显示区 [LEFT, RIGHT)（RIGHT 为右缘 x，该列像素不归文字）。
 * 字体缩放见 vid_panel_theme.h：VP_CHROME_TOP_TEXT_SCALE（与 VP_FONT_SCALE 同一值）。
 * 行为差异在 vid_panel.c p_draw_text_bar(keep_left)：标题永远保左右裁；路径短则保左、长则右缘对齐本 RIGHT（非屏右）左裁。
 * 文件名与路径绘制须同传 VP_CHROME_TOP_TEXT_LEFT_X / VP_CHROME_TOP_TEXT_RIGHT_X（见 p_draw_chrome_top、Vid_panel_draw_player_chrome）。
 * 文件列表行左缘与顶栏左缘对齐 → VP_CHROME_LEFT_TEXT_X 别名指向顶栏左。 */
#define VP_CHROME_TOP_TEXT_LEFT_X   ((VP_TEXT_PAD) + 2)
#define VP_CHROME_TOP_TEXT_RIGHT_X  246 /* 手写像素；路径与标题仅此一处定义右界 */

#define VP_CHROME_LEFT_TEXT_X       (VP_CHROME_TOP_TEXT_LEFT_X)

/* ── Player 底屏 ui_mod 信息区（vid_panel_menu.c）──────────────────────────
 * 经 `Draw_*` 的字号：`DEF_DRAW_TEXT_SCALE`（见 draw_types.h）。
 * 与顶栏 VP_CHROME_TOP_TEXT_LEFT_X 默认同为 6，但是两个独立宏，禁止互相 #define 引用。
 *
 * VP_PLAYER_UI_STATUS_LEFT_X — V/A/CPU 等「整行左对齐」的 x；与 HCOL 第 0 列相同。
 * VP_PLAYER_UI_HCOL_STEP — 同一行内多列时相邻列「左缘–左缘」间距（像素）。
 *
 * 行结构：V 单行；A 单行；分辨率+帧率+SBS+3D 一行（列 0–3）；Dec/Tex/SAR/ASM 一行；Lin+Heap 一行；CPU 一行。
 */
#define VP_PLAYER_UI_STATUS_LEFT_X        6
#define VP_PLAYER_UI_HCOL_STEP            80
#define VP_PLAYER_UI_HCOL_X(n)            ((float)(VP_PLAYER_UI_STATUS_LEFT_X + (n) * (VP_PLAYER_UI_HCOL_STEP)))
#define VP_PLAYER_UI_STATUS_BAR_X         22
#define VP_PLAYER_UI_STATUS_PCT_X         290
/* 叠在 video_x_offset_bottom（解码层给的底屏坐标）上再向右加的像素；与左列、顶栏无公式关系 */
#define VP_PLAYER_UI_STATUS_THUMB_SHIFT_X 4

/* Player 状态区：path 条带下首行 y + 统一行距（小字号 0.30 下更易扫读）。 */
#define VP_PLAYER_STATUS_FIRST_Y        27
#define VP_PLAYER_STATUS_LINE_STEP      11
#define VP_PLAYER_STATUS_BAR_GAP_PRE    2 /* CPU 标题与第一条形图之间的空隙 */

#define VP_PLAYER_STATUS_ROW_V_Y        (VP_PLAYER_STATUS_FIRST_Y + 0 * VP_PLAYER_STATUS_LINE_STEP)
#define VP_PLAYER_STATUS_ROW_A_Y        (VP_PLAYER_STATUS_FIRST_Y + 1 * VP_PLAYER_STATUS_LINE_STEP)
#define VP_PLAYER_STATUS_ROW_RES_Y      (VP_PLAYER_STATUS_FIRST_Y + 2 * VP_PLAYER_STATUS_LINE_STEP)
#define VP_PLAYER_STATUS_ROW_DEC_Y      (VP_PLAYER_STATUS_FIRST_Y + 3 * VP_PLAYER_STATUS_LINE_STEP)
#define VP_PLAYER_STATUS_ROW_DIAG_LIN_Y (VP_PLAYER_STATUS_FIRST_Y + 4 * VP_PLAYER_STATUS_LINE_STEP)
#define VP_PLAYER_STATUS_ROW_CPUHDR_Y   (VP_PLAYER_STATUS_FIRST_Y + 5 * VP_PLAYER_STATUS_LINE_STEP)
#define VP_PLAYER_STATUS_BAR0_Y         (VP_PLAYER_STATUS_FIRST_Y + 6 * VP_PLAYER_STATUS_LINE_STEP + (VP_PLAYER_STATUS_BAR_GAP_PRE))
#define VP_PLAYER_STATUS_BAR_STRIDE     12

/* 分辨率行：列 0=WxH，列 1=fps，列 2=SBS，列 3=3D */
#define VP_PLAYER_UI_STATUS_RES_X   (VP_PLAYER_UI_HCOL_X(0))
#define VP_PLAYER_UI_STATUS_FPS_X   (VP_PLAYER_UI_HCOL_X(1))
#define VP_PLAYER_UI_STATUS_SBS_X   (VP_PLAYER_UI_HCOL_X(2))
#define VP_PLAYER_UI_STATUS_3D_X    (VP_PLAYER_UI_HCOL_X(3))

#define VP_PLAYER_UI_DIAG_LIN_X     (VP_PLAYER_UI_HCOL_X(0))
#define VP_PLAYER_UI_DIAG_HEAP_X    (VP_PLAYER_UI_HCOL_X(1))

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

/* 兼容旧名：均等于顶栏文字右界 VP_CHROME_TOP_TEXT_RIGHT_X */
#define VP_PLAYER_TITLE_TEXT_MAX_X  (VP_CHROME_TOP_TEXT_RIGHT_X)
#define VP_PATH_TEXT_MAX_X          (VP_CHROME_TOP_TEXT_RIGHT_X)

/* 顶栏路径/标题行：与右上按钮同高的条带内竖直居中 +1（Citro2D 顶对齐 y） */
static inline float vp_chrome_top_text_ty(float text_h)
{
	float btn_y = (float)VP_TOP_BTN_Y;
	float btn_h = (float)VP_CHROME_BTN_H;
	return btn_y + (btn_h - text_h) * 0.5f + 1.0f;
}

/* Player 底屏 FPS（vid_panel.c）：与 V 编解码首行同 y（VP_PLAYER_STATUS_ROW_V_Y）；字号用 DEF_DRAW_TEXT_C2D_SCALE。 */
#define VP_PLAYER_FPS_DX (-4) /* 相对右对齐基线再左移 */

/* 进度条（仅在 Player 面板额外绘制时使用） */
#define VP_PROGRESS_INVALID 10   /* 两端死区宽度（各10px，不到达屏幕边缘） */
#define VP_PROGRESS_H       3    /* 实际绘制的条高度（像素） */
#define VP_PROGRESS_HIT_HALF 16  /* 以条 Y 中心行为轴，上/下各含若干行；+ 中心行共 33 行 */
#define VP_PROGRESS_HIT_H   (VP_PROGRESS_HIT_HALF * 2 + 1)
/* 进度条 + 上方「当前/总时长」与右侧 seeking 文案整组上移（像素） */
#define VP_PROGRESS_GROUP_UP_PX 8
#define VP_PROGRESS_Y       (VP_SCREEN_H - VP_FOOTER_H - 10 - VP_PROGRESS_GROUP_UP_PX)
#define VP_PROGRESS_CENTER_Y (VP_PROGRESS_Y + ((VP_PROGRESS_H - 1) / 2))
#define VP_PROGRESS_HIT_TOP  (VP_PROGRESS_CENTER_Y - (VP_PROGRESS_HIT_HALF))

/* 进度条上方时间 / Seek 文案：`Draw` 字号 DEF_DRAW_TEXT_SCALE(0.30)；y 在条带上方固定间距。 */
#define VP_TIMEBAR_TEXT_ABOVE_PROGRESS  15.0f
#define VP_TIMEBAR_TIME_X               ((float)VP_PLAYER_UI_STATUS_LEFT_X)
#define VP_TIMEBAR_SEEK_X               (210.0f) /* 原 170 + 右移 40 */

/* Active region within the bar */
#define VP_PROGRESS_X_MIN   (VP_PROGRESS_INVALID)
#define VP_PROGRESS_X_MAX   (VP_SCREEN_W - 1 - VP_PROGRESS_INVALID)
#define VP_PROGRESS_TOTAL_W (VP_SCREEN_W - VP_PROGRESS_INVALID * 2)

/* Seek-bar hit-test: 宽同条；竖直以 VP_PROGRESS_CENTER_Y 为中心上下各 VP_PROGRESS_HIT_HALF */
static inline int vp_progress_hit(int px, int py)
{
    return (px >= VP_PROGRESS_X_MIN && px <= VP_PROGRESS_X_MAX
         && py >= VP_PROGRESS_HIT_TOP
         && py < VP_PROGRESS_HIT_TOP + VP_PROGRESS_HIT_H);
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

/* 快速菜单覆盖层（中央浮动弹窗）
 * 几何分两层，避免“描边外框”和“深色内芯”混用导致视觉中心与计算中心不一致：
 *   FRAME — 1px 描边矩形（左右各距屏幕 VP_OVERLAY_MARGIN）
 *   INNER — 内缩 BORDER_PX 后的实心区；文案与控件的 X 向一律相对 INNER 居中。
 */
#define VP_OVERLAY_PANEL_H    108
#define VP_OVERLAY_MARGIN     20
#define VP_OVERLAY_BORDER_PX  1

#define VP_OVERLAY_FRAME_X    (VP_OVERLAY_MARGIN)
#define VP_OVERLAY_FRAME_W    (VP_SCREEN_W - 2 * (VP_OVERLAY_MARGIN))

#define VP_OVERLAY_INNER_X    ((VP_OVERLAY_FRAME_X) + (VP_OVERLAY_BORDER_PX))
#define VP_OVERLAY_INNER_W    ((VP_OVERLAY_FRAME_W) - 2 * (VP_OVERLAY_BORDER_PX))

/** 内芯水平几何中心（浮点）。与 C2D_DrawText(..., C2D_AlignCenter, x, ...) 的 x 一致。 */
#define VP_OVERLAY_CONTENT_CENTER_XF \
	((float)(VP_OVERLAY_INNER_X) + (float)(VP_OVERLAY_INNER_W) * 0.5f)

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
