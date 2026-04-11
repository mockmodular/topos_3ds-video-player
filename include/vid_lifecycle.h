#if !defined(DEF_VID_LIFECYCLE_HPP)
#define DEF_VID_LIFECYCLE_HPP

// Internal init helpers shared between video_player.c (Vid_init_variable) and
// vid_decode.c (Vid_decode_thread).  Will move to vid_lifecycle.c in the final split.
void Vid_init_debug_view_data(void);
void Vid_init_media_data(void);
void Vid_init_video_data(void);
void Vid_init_audio_data(void);

#endif //!defined(DEF_VID_LIFECYCLE_HPP)
