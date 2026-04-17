#if !defined(DEF_VID_STATE_HPP)
#define DEF_VID_STATE_HPP

#include <stdbool.h>
#include <stdint.h>

#include "system/draw/draw.h"
#include "system/util/decoder.h"
#include "system/util/queue.h"
#include "system/util/str.h"
#include "system/util/sync.h"
#include "system/util/thread_types.h"
#include "system/util/util.h"
#include "vid_panel.h"

//Defines.
#define VIDEO_BUFFERS								(uint8_t)(4)
#define MAX_FILE_NAME_LENGTH						(uint16_t)(256)
#define MAX_PATH_LENGTH								(uint16_t)(8192)

#define WAIT_THRESHOLD_ALLOWED_DURATION(frametime)	(double)(frametime * 5)
#define WAIT_THRESHOLD(frametime)					(double)(Util_max_d(20, frametime) * -1.4)
#define FORCE_WAIT_THRESHOLD(frametime)				(double)(Util_max_d(20, frametime) * -2.5)
#define DELAY_SAMPLES								(uint8_t)(60)

#define SEEK_IGNORE_PACKETS							(uint8_t)(5)
#define SEEK_BACKWARD_TIMEOUT						(uint8_t)(20)
/* SEEKING 阶段：在 wait_count 已耗尽且非 backward-wait 时，每成功 parse 一包递减；到 0 仍达不到 seek_demux_target_ms 则强制结束该波 seek。须 >0。 */
#define SEEK_STALL_RESCUE_PACKETS					(uint16_t)(20000)
/* seek_start_pos_after_jump: unset until demux time is anchored (decode seek state); valid anchors are >= 0 ms */
#define VID_SEEK_JUMP_ANCHOR_UNSET					(-1.0)

#define RAM_TO_KEEP_BASE							(uint32_t)(1000 * 1000 * 6)
#define VID_FIXED_RESTART_PLAYBACK_THRESHOLD		(uint16_t)(48)

//#define NUM_OF_THREADS_MIN							(uint8_t)(1)
//#define NUM_OF_THREADS_MAX							(uint8_t)(8)
#define NUM_OF_THREADS_O3DS							(uint8_t)(2)
#define NUM_OF_THREADS_N3DS							(uint8_t)(3)
#define NUM_OF_THREADS_MIN							NUM_OF_THREADS_O3DS
#define NUM_OF_THREADS_MAX							NUM_OF_THREADS_N3DS

#define SETTINGS_ELEMENTS_V0						(uint8_t)(7)
#define SETTINGS_ELEMENTS_V1						(uint8_t)(9)
#define SETTINGS_ELEMENTS_V2						(uint8_t)(12)
#define SETTINGS_ELEMENTS_V3						(uint8_t)(13)
#define SETTINGS_ELEMENTS_V4						(uint8_t)(16)
#define SETTINGS_ELEMENTS_V5						(uint8_t)(17)
#define SETTINGS_ELEMENTS_V6						(uint8_t)(18)
#define SETTINGS_ELEMENTS_V7						(uint8_t)(16)
#define SETTINGS_ELEMENTS_V8						(uint8_t)(17)
#define SETTINGS_ELEMENTS_V9						(uint8_t)(20)
#define SETTINGS_ELEMENTS_V10						(uint8_t)(21)
#define SETTINGS_ELEMENTS_V11						(uint8_t)(22)
#define SETTINGS_ELEMENTS_V12						(uint8_t)(23)
#define SETTINGS_ELEMENTS_V13						(uint8_t)(24)
#define SETTINGS_ELEMENTS_V14						(uint8_t)(27)
#define SETTINGS_ELEMENTS_V15						(uint8_t)(28)
#define SETTINGS_ELEMENTS_NEWEST					(uint8_t)(SETTINGS_ELEMENTS_V15)

/* File browser top level: TF root vs sdmc:/movies（若不存在则尝试旧名 sdmc:/movie）。 */
#define VID_FS_BROWSER_ROOT_MOVIE					(uint8_t)(0)
#define VID_FS_BROWSER_ROOT_TF						(uint8_t)(1)

/* Dav1d / frame-worker: registered decode job handles (see frame_worker_thread_* in vid_worker.c). */
#define DEBUG_GRAPH_TEMP_ELEMENTS					(uint16_t)(32)

#define QUEUE_OP_TIMEOUT_US							(uint64_t)(DEF_UTIL_MS_TO_US(100))

/** Texture filter mode (vid_settings.txt <20>; <0> remains legacy “not nearest” for old parsers). */
#define VID_TEX_FILTER_BILINEAR						((uint8_t)0)
#define VID_TEX_FILTER_NEAREST						((uint8_t)1)
#define VID_TEX_FILTER_AUTO							((uint8_t)2)

/** Top-screen scale: pixel-perfect 1:1 (2D + size rules) vs aspect fit (default). <21> in vid_settings.txt */
#define VID_SCALE_PIXEL_PERFECT						((uint8_t)0)
#define VID_SCALE_FIT								((uint8_t)1)

#define TOP_SCREEN_WIDTH							(uint16_t)(400)
#define TOP_SCREEN_HEIGHT							(uint16_t)(240)
#define FULL_SCREEN_WIDTH							(uint16_t)(400)
#define FULL_SCREEN_HEIGHT							(uint16_t)(240)
#define NON_FULL_SCREEN_WIDTH						(uint16_t)(400)
#define ENTER_FULL_SCREEN_TRANSITION_PERIOD			(uint16_t)(180)

//Typedefs.
typedef enum
{
	NONE_REQUEST,

	DECODE_THREAD_PLAY_REQUEST,
	DECODE_THREAD_PAUSE_REQUEST,
	DECODE_THREAD_RESUME_REQUEST,
	DECODE_THREAD_SEEK_REQUEST,
	DECODE_THREAD_PLAY_NEXT_REQUEST,
	DECODE_THREAD_ABORT_REQUEST,
	DECODE_THREAD_SHUTDOWN_REQUEST,
	DECODE_THREAD_INCREASE_KEEP_RAM_REQUEST,

	READ_PACKET_THREAD_READ_PACKET_REQUEST,
	READ_PACKET_THREAD_SEEK_REQUEST,
	READ_PACKET_THREAD_ABORT_REQUEST,

	DECODE_VIDEO_THREAD_DECODE_REQUEST,
	DECODE_VIDEO_THREAD_CLEAR_CACHE_REQUEST,
	DECODE_VIDEO_THREAD_ABORT_REQUEST,

	CONVERT_THREAD_CONVERT_REQUEST,
	CONVERT_THREAD_CLEAR_CACHE_REQUEST,
	CONVERT_THREAD_ABORT_REQUEST,

	AUDIO_DECODE_THREAD_DECODE_REQUEST,
	AUDIO_DECODE_THREAD_SEEK_REQUEST,
	AUDIO_DECODE_THREAD_ABORT_REQUEST,
	AUDIO_DECODE_THREAD_SHUTDOWN_REQUEST,

	MAX_REQUEST = UINT32_MAX,
} Vid_command;

typedef enum
{
	NONE_NOTIFICATION,

	DECODE_THREAD_FINISHED_BUFFERING_NOTIFICATION,

	READ_PACKET_THREAD_FINISHED_READING_NOTIFICATION,
	READ_PACKET_THREAD_FINISHED_READING_EOF_NOTIFICATION,
	READ_PACKET_THREAD_FINISHED_SEEKING_NOTIFICATION,
	READ_PACKET_THREAD_FINISHED_ABORTING_NOTIFICATION,

	DECODE_VIDEO_THREAD_FINISHED_COPYING_PACKET_NOTIFICATION,
	DECODE_VIDEO_THREAD_FINISHED_ABORTING_NOTIFICATION,

	CONVERT_THREAD_OUT_OF_BUFFER_NOTIFICATION,
	CONVERT_THREAD_FINISHED_BUFFERING_NOTIFICATION,
	CONVERT_THREAD_FINISHED_CLEARING_CACHE,
	CONVERT_THREAD_FINISHED_ABORTING_NOTIFICATION,

	AUDIO_DECODE_THREAD_FINISHED_SEEKING_NOTIFICATION,
	AUDIO_DECODE_THREAD_FINISHED_ABORTING_NOTIFICATION,

	MAX_NOTIFICATION = UINT32_MAX,
} Vid_notification;

typedef enum
{
	PLAYER_STATE_IDLE,
	PLAYER_STATE_PREPARE_PLAYING,
	PLAYER_STATE_PLAYING,
	PLAYER_STATE_PAUSE,
	PLAYER_STATE_BUFFERING,
	PLAYER_STATE_PREPARE_SEEKING,
	PLAYER_STATE_SEEKING,
} Vid_player_main_state;

typedef enum
{
	PLAYER_SUB_STATE_NONE					= 0,
	PLAYER_SUB_STATE_HW_DECODING			= (1 << 0),
	PLAYER_SUB_STATE_HW_CONVERSION			= (1 << 1),
	PLAYER_SUB_STATE_TOO_BIG				= (1 << 2),
	PLAYER_SUB_STATE_RESUME_LATER			= (1 << 3),
	PLAYER_SUB_STATE_SEEK_BACKWARD_WAIT		= (1 << 4),
} Vid_player_sub_state;

/* 自然播放到末尾（PLAY_NEXT）时保留：seek 重开会话依赖这些位决定是否 MVD/Y2R。
 * 仅清除 RESUME_LATER / SEEK_BACKWARD_WAIT 等临时状态。 */
#define PLAYER_SUB_STATE_PERSIST_AFTER_NATURAL_EOF_MASK \
	(PLAYER_SUB_STATE_HW_DECODING | PLAYER_SUB_STATE_HW_CONVERSION | PLAYER_SUB_STATE_TOO_BIG)

typedef enum
{
	SCREEN_POS_TOP_LEFT,
	SCREEN_POS_TOP_RIGHT,
	SCREEN_POS_BOTTOM,

	SCREEN_POS_MAX,
} Vid_screen_pos;

typedef enum
{
	EYE_LEFT,
	EYE_RIGHT,

	EYE_MAX,
} Vid_eye;

typedef struct
{
	uint32_t image_width;
	uint32_t image_height;
	uint16_t num_of_images;
	Draw_image_data* images;
} Large_image;

typedef struct
{
	char name[MAX_FILE_NAME_LENGTH];
	char directory[MAX_PATH_LENGTH];
	uint32_t index;
	bool request_player_panel_on_ok;
} Vid_file;

typedef struct
{
	bool is_key_frame;
	uint8_t packet_index;
} Vid_video_packet_data;

typedef struct
{
	uint8_t* audio;
	uint32_t audio_samples;
	double pos;
	uint8_t packet_index;
} Vid_audio_decoded_data;

typedef struct
{
	//Global.
	bool inited;
	bool main_run;
	bool thread_run;
	bool thread_suspend;
	Str_data status;

	//Settings (can be changed while playing videos).
	bool auto_dim_5s;
	uint8_t fs_browser_root_mode;	/* VID_FS_BROWSER_ROOT_MOVIE or VID_FS_BROWSER_ROOT_TF */
	bool    ui_mod;			/* true = full bottom-screen info (codec/cpu/fps/thumb…); false = progress+time+seek+chrome buttons only */
	uint8_t texture_filter_mode;	/* VID_TEX_FILTER_BILINEAR / NEAREST / AUTO */
	uint8_t video_scale_mode;		/* VID_SCALE_PIXEL_PERFECT / VID_SCALE_FIT */
	uint8_t seek_duration;
	uint16_t volume;

	//Settings (can NOT be changed while playing videos).
	bool disable_audio;
	bool disable_video;
	bool use_hw_decoding;
	uint8_t use_hw_color_conversion;	//VID_HW_CONV_CPU/Y2R_X2/NEON_Y2R
	bool use_multi_threaded_decoding;
	bool is_sbs_3d;
	bool sbs_swap_eyes;

	//Other settings (user doesn't have permission to change).
	uint8_t num_of_threads;
	uint32_t ram_to_keep_base;
	Media_thread_type thread_mode;

	/* 100 ms housekeeping (buffer %, seek bar); not decode-time graphs. */
	uint64_t previous_ts;
	const void* frame_list[DEBUG_GRAPH_TEMP_ELEMENTS];

	//A/V desync management.
	uint64_t wait_threshold_exceeded_ts[EYE_MAX];
	uint64_t last_video_frame_updated_ts[EYE_MAX];
	double video_delay_ms[EYE_MAX][DELAY_SAMPLES];
	double video_delay_avg_ms[EYE_MAX];

	//Player.
	Vid_player_main_state state;
	Vid_player_sub_state sub_state;
	Vid_file file;

	//Media.
	double media_duration;
	double media_current_pos;
	double seek_pos_cache;
	double seek_pos;
	/* 一次 seek 波：入队时冻结的目标(ms)；demux/完成判定用 seek_demux_target_ms，避免 seek 进行中 UI 改写 seek_pos。 */
	double seek_queued_pos_ms;
	double seek_demux_target_ms;
	double seek_start_pos_after_jump;
	bool seek_request_deferred;
	uint16_t seek_stall_rescue_packets;

	/* 文件列表连续打开：合并为一条 PLAY，仅保留最后一次 malloc 的 Vid_file（解码线程取走）。 */
	Vid_file *play_request_pending;
	Sync_data play_request_pending_lock;

	//Video.
	uint8_t num_of_video_tracks;
	double next_vfps_update;
	double buffer_progress;
	uint8_t next_store_index[EYE_MAX];
	uint8_t next_draw_index[EYE_MAX];
	uint16_t vps[EYE_MAX];
	uint16_t vps_cache[EYE_MAX];
	double next_frame_update_time[EYE_MAX];
	double video_frametime[EYE_MAX];
	double video_x_offset[EYE_MAX];
	double video_y_offset[EYE_MAX];
	double video_zoom[EYE_MAX];
	double video_current_pos[EYE_MAX];
	/* Decoder PTS (ms) for each ring slot / eye; -1 = unknown (use legacy A/V estimate). */
	double video_buffer_pts[VIDEO_BUFFERS][EYE_MAX];
	Media_v_info video_info[EYE_MAX];
	Large_image large_image[VIDEO_BUFFERS][EYE_MAX];
	uint8_t* sbs_right_buf;

	//Audio.
	uint8_t num_of_audio_tracks;
	double audio_current_pos;
	double last_decoded_audio_pos;
	Media_a_info audio_info[DEF_DECODER_MAX_AUDIO_TRACKS];

	//UI.
	bool is_full_screen;
	bool is_waiting_home_menu;
	bool is_setting_volume;
	bool is_setting_seek_duration;
	bool must_resume_after_home_menu;
	uint32_t turn_off_bottom_screen_count;
	uint32_t auto_full_screen_count;
	/* Select 手动关底屏后为 true；再按 Select 或触摸底屏唤醒后清 false（与全屏自动关底区分） */
	bool bottom_lcd_select_sleep;

	//Buttons (底屏进度条命中区；R/L 肩键菜单已移除)。
	Draw_image_data seek_bar;

	//Panel UI (new three-panel system).
	Vid_panel panel;

	//Threads and queues.
	Thread decode_thread;
	Queue_data decode_thread_command_queue;
	Queue_data decode_thread_notification_queue;

	Thread decode_video_thread;
	Queue_data decode_video_thread_command_queue;

	Thread convert_thread;
	Queue_data convert_thread_command_queue;

	Thread read_packet_thread;
	Queue_data read_packet_thread_command_queue;

	Thread audio_decode_thread;
	Queue_data audio_decode_thread_command_queue;

	Thread init_thread;
	Thread exit_thread;

	//Mutexs.
	Sync_data texture_init_free_lock;
	Sync_data delay_update_lock;
} Vid_player;

//Globals (defined in video_player.c).
extern Vid_player vid_player;

#endif //!defined(DEF_VID_STATE_HPP)
