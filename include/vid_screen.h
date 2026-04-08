#if !defined(DEF_VID_SCREEN_HPP)
#define DEF_VID_SCREEN_HPP

#include <stdbool.h>
#include <stdint.h>

#include "vid_state.h"

/** Presentation Video width & height (Media_v_info.width / .height). If both are zero, uses
 *  codec_width / codec_height. Use for 3d/2d auto, tex-filter auto, pixel-perfect checks so
 *  AV1 (and similar) logic follows display size, not 128px-padded coded dimensions. */
void Vid_video_presentation_wh(Vid_eye eye, uint32_t *out_w, uint32_t *out_h);

void Vid_fit_to_screen(uint16_t screen_width, uint16_t screen_height, Vid_eye eye_index);
/** True when Video > Scale is pixel-perfect, 2D, and dimensions match rules — forces NEAREST over tex filter. */
bool Vid_pixel_perfect_forces_nearest(uint32_t eye_k);
void Vid_change_video_size(double change_px, Vid_eye eye_index);
void Vid_enter_full_screen(uint32_t bottom_screen_timeout);
void Vid_exit_full_screen(void);
void Vid_control_full_screen(void);

#endif //!defined(DEF_VID_SCREEN_HPP)
