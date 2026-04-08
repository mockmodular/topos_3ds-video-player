#pragma once

#include <citro2d.h>

#define VP_FONT_SCALE       0.45f
#define VP_TEXT_BUF_CHARS   4096

/* ── 底色 ── */
#define VP_COL_BG          C2D_Color32(0x00, 0x00, 0x00, 0xFF)
#define VP_COL_PATH_BG     C2D_Color32(0x08, 0x0A, 0x0E, 0xFF)
#define VP_COL_FOOTER_BG   C2D_Color32(0x08, 0x0A, 0x0E, 0xFF)
#define VP_COL_SEP         C2D_Color32(0x1C, 0x20, 0x30, 0xFF)

/* ── 交互状态 ── */
#define VP_COL_HIGHLIGHT   C2D_Color32(0x10, 0x18, 0x28, 0xFF)
#define VP_COL_FLASH       C2D_Color32(0x1A, 0x28, 0x40, 0xFF)

/* ── 文字 ── */
#define VP_COL_TEXT        C2D_Color32(0xD4, 0xD8, 0xDE, 0xFF)
#define VP_COL_DIR         C2D_Color32(0x5C, 0x8C, 0xB8, 0xFF)
#define VP_COL_FOOTER_TEXT C2D_Color32(0x78, 0x84, 0x94, 0xFF)

/* ── 按钮 ── */
#define VP_COL_BTN_BG      C2D_Color32(0x0C, 0x0E, 0x14, 0xFF)
#define VP_COL_BTN_BORDER  C2D_Color32(0x26, 0x30, 0x46, 0xFF)
/* Alias matching topos.ui0.04 naming (same value) */
#define VP_COL_BTN_BORDER2 VP_COL_BTN_BORDER

/* ── 进度条 ── */
#define VP_COL_PROGRESS_BG   C2D_Color32(0x0E, 0x10, 0x18, 0xFF)
#define VP_COL_PROGRESS_FILL C2D_Color32(0x2C, 0x50, 0x74, 0xFF)
#define VP_COL_PROGRESS_HEAD C2D_Color32(0xC8, 0xD4, 0xE0, 0xFF)
#define VP_COL_SEEK_LINE     C2D_Color32(0x40, 0x58, 0x70, 0xFF)
/* 端帽：近黑，与主轨道未播放背景(0x0E,0x10,0x18)形成明显对比 */
#define VP_COL_PROGRESS_END  C2D_Color32(0x04, 0x04, 0x06, 0xFF)

/* ── 覆盖层 ── */
#define VP_COL_OVERLAY_BG  C2D_Color32(0x08, 0x0A, 0x10, 0xFF)

/* ── 强调色 ── */
#define VP_COL_ACCENT      C2D_Color32(0x24, 0x44, 0x70, 0xFF)

/* 与 DEF_DRAW_GREEN 一致（原 state.msg 行） */
#define VP_COL_FPS         C2D_Color32(0x00, 0xFF, 0x00, 0xFF)
