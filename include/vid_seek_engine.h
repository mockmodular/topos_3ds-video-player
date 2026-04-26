#if !defined(DEF_VID_SEEK_ENGINE_H)
#define DEF_VID_SEEK_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Seek 分层（与 vid_decode.c 解码线程配合）：
 *
 * 1) 用户意图（本模块入口）
 *    - on_bar_drag：只写 seek_pos_cache，不入队、不碰解码。
 *    - on_bar_commit / on_step_*：仅在「非 IDLE 且非 PREPARE_PLAYING」时写 seek_pos 并 submit（与 can_submit 一致）。
 *
 * 2) 提交策略 seek_engine_submit
 *    - seek_queued_pos_ms：仅在本波 demux 发起前由引擎写入，供解码线程冻结为 seek_demux_target_ms。
 *    - seek_request_deferred：管道忙时置 true；解码在 BUFFERING 结束等时机再入队，且始终用「当前 seek_pos」
 *      只保留最后一次目标（不会排队多个 demux）。
 *    - 管道忙：PREPARE_SEEKING / SEEKING / BUFFERING（与解码线程对 BUFFERING 上 seek 的延后一致）。
 *
 * 3) 解码线程（不在此文件）
 *    - PREPARE_SEEKING → issue demux；SEEKING 消耗包；收尾后视频进 BUFFERING；BUFFERING 期间拒绝叠 seek。
 */

//Read-only snapshot consumed by UI layers (draw, HUD) to display the seek bar.
//All positions are in milliseconds.
typedef struct
{
	double	display_pos_ms;	//Indicator position (drag cache, pending seek_pos, or media_current_pos).
	double	duration_ms;
	bool	is_seeking;		//True while demux/seeking phase or BUFFERING with a deferred seek (UI may show pending target).
	bool	is_drag_active;	//User is currently dragging the seek bar.
} VidSeekEngineView;

//──UI → Engine────────────────────────────────────────────────────────────────
//User is dragging; touch_x is the raw touch pixel X coordinate.
void VidSeekEngine_on_bar_drag(int16_t touch_x);

//User released the bar; commit the previewed position as the seek target.
void VidSeekEngine_on_bar_commit(void);

//Step forward / backward by seek_duration seconds (d-pad / button).
void VidSeekEngine_on_step_fwd(void);
void VidSeekEngine_on_step_back(void);

//──Engine → UI────────────────────────────────────────────────────────────────
//Returns the current display state; safe to call every frame from the draw path.
VidSeekEngineView VidSeekEngine_get_view(void);

#endif //!defined(DEF_VID_SEEK_ENGINE_H)
