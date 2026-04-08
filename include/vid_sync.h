#if !defined(DEF_VID_SYNC_HPP)
#define DEF_VID_SYNC_HPP

#include "vid_state.h"

void Vid_init_desync_data(void);
void Vid_update_video_delay(Vid_eye eye_index);
void Vid_update_decoding_delay(double decoding_time, double* total_delay, Vid_eye eye_index);

#endif //!defined(DEF_VID_SYNC_HPP)
