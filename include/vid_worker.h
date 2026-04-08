#if !defined(DEF_VID_WORKER_HPP)
#define DEF_VID_WORKER_HPP

#include <stdbool.h>
#include <stdint.h>

#include "vid_state.h"

// Statistics / media-info helpers (also called from Vid_decode_thread in video_player.c).
bool   Vid_effective_use_linear_texture_filter(uint32_t eye_k);
void   Vid_update_decoding_statistics_every_100ms(void);
double Vid_get_media_duration(double video_track_0_duration, double video_track_1_duration, double audio_track_duration);
double Vid_get_current_media_pos(double video_track_0_pos, double video_track_1_pos, double audio_track_pos);
bool   Vid_has_video(uint8_t num_of_video_tracks, double video_frametimes[EYE_MAX]);
void   Vid_log_media_info(void);

// Weak-function implementations for frame-decode timing callbacks.
void frame_worker_thread_start(const void* ptr);
void frame_worker_thread_end(const void* ptr);
void dav1d_worker_task_start(const void* ptr);
void dav1d_worker_task_end(const void* ptr);

// Worker threads.
void Vid_decode_video_thread(void* arg);
void Vid_convert_thread(void* arg);

#endif //!defined(DEF_VID_WORKER_HPP)
