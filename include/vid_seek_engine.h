#if !defined(DEF_VID_SEEK_ENGINE_H)
#define DEF_VID_SEEK_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Seek 分层（与 vid_decode.c 解码线程配合）：
 *
 * 1) 用户意图（本模块入口）
 *    - on_bar_drag：只写 seek_pos_cache，不入队、不碰解码。
 *    - on_bar_commit：松手提交触摸预览 → seek_pos + submit。
 *    - 左右键/摇杆：on_kbd_lr_preview_* 只累加 seek_pos_cache；on_kbd_lr_release 松手才 seek_pos=cache + submit。
 *    - on_step_fwd/back：仍供其它调用方「立即步进并 submit」（若仍使用）。
 *
 * 2) 提交策略 seek_engine_submit
 *    - seek_queued_pos_ms：仅在本波 demux 发起前由引擎写入，供解码线程冻结为 seek_demux_target_ms。
 *    - seek_request_deferred：管道忙或入队失败时置 true；解码线程自本波 seek 在解码侧开始（PREPARE_SEEKING）起
 *      满 100ms 才把「当前 seek_pos」（始终为最新，中间旧请求丢弃）再入队一次 DECODE_THREAD_SEEK_REQUEST。
 *    - 管道忙：仅 PREPARE_SEEKING / SEEKING；BUFFERING 不视为忙（可立即 submit seek）。
 *
 * 3) 解码线程（不在此文件）
 *    - PREPARE_SEEKING → issue demux；SEEKING 消耗包；收尾后视频进 BUFFERING（可带 POST_SEEK_BUFFERING）；
 *      POST_SEEK_BUFFERING 上叠 seek 时解码线程截断缓冲并立即下一波 demux。
 */

/* Read-only snapshot for进度条/时间字（毫秒）。VidSeekEngine_get_view() 中 display_pos_ms 优先级：
 *  1) 触摸拖条或左右键/摇杆预览中 → seek_pos_cache（「将要 seek 到」的预览）；
 *  2) 已提交 seek、管道在 PREPARE_SEEKING/SEEKING 或 BUFFERING+deferred → seek_pos（已选目标）；
 *  3) 正常播放/暂停等 → media_current_pos（实际播放头，由解码/转码线程更新）。 */
typedef struct
{
	double	display_pos_ms;
	double	duration_ms;
	bool	is_seeking;
	bool	is_drag_active; /* seek_bar.selected 或键盘左右预览中 */
} VidSeekEngineView;

//──UI → Engine────────────────────────────────────────────────────────────────
//User is dragging; touch_x is the raw touch pixel X coordinate.
void VidSeekEngine_on_bar_drag(int16_t touch_x);

//User released the bar; commit the previewed position as the seek target.
void VidSeekEngine_on_bar_commit(void);

//Step forward / backward by seek_duration seconds (immediate submit; legacy callers).
void VidSeekEngine_on_step_fwd(void);
void VidSeekEngine_on_step_back(void);

void VidSeekEngine_on_kbd_lr_preview_fwd(void);
void VidSeekEngine_on_kbd_lr_preview_back(void);
/** 松手提交键盘/摇杆预览；返回 true 表示已写入 seek_pos 并入队 seek（用于全屏仅在真实 seek 后退出）。 */
bool VidSeekEngine_on_kbd_lr_release(void);
void VidSeekEngine_cancel_kbd_preview(void);

//──Engine → UI────────────────────────────────────────────────────────────────
//Returns the current display state; safe to call every frame from the draw path.
VidSeekEngineView VidSeekEngine_get_view(void);

/* 首波缓冲结束、进入可播放态后由解码线程调用，允许「合法 seek」入队。 */
void VidSeekEngine_mark_playback_started(void);

/* 入队 DECODE_THREAD_PLAY_REQUEST 之前调用：运行时唯一把 playback_not_started 置回 true。 */
void VidSeekEngine_mark_playback_not_started(void);

#endif //!defined(DEF_VID_SEEK_ENGINE_H)
