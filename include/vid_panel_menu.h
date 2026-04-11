#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * vid_panel_menu.h
 *
 * 播放器底屏 Player 面板的全部绘制 API：
 *   - 状态栏 / 编解码 / CPU+内存诊断
 *   - 时间条 / seek 进度 / 亮度提示
 *
 * 由 vid_panel.c 的 Vid_panel_draw_bottom_screen() 调用，外部不直接使用。
 */

/*
 * 状态栏 + 编解码信息 + CPU/内存诊断 + 视频缩略图
 *
 * image_index_left       : 当前帧缓冲索引（左眼）
 * video_x/y_offset_bottom: 底屏缩略图绘制偏移
 * image_width/height_left: 缩略图尺寸
 */
/** ui_mod 等导致状态区需重配时调用：下次绘制时重新判定 2/4 核 CPU 条布局（非每帧）。 */
void Vid_panel_player_invalidate_cpu_bar_layout(void);

void Vid_panel_player_draw_status(uint32_t color,
                                   uint32_t back_color,
                                   uint64_t current_ts,
                                   double   video_x_offset_bottom,
                                   double   video_y_offset_bottom,
                                   double   image_width_left,
                                   double   image_height_left,
                                   uint8_t  image_index_left);

/*
 * 时间条 / seek 进度 / 亮度提示文字
 */
void Vid_panel_player_draw_timebar(uint32_t color, uint64_t current_ts);
