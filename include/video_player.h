#if !defined(DEF_VIDEO_PLAYER_HPP)
#define DEF_VIDEO_PLAYER_HPP
#include <stdbool.h>
#include <stdint.h>
#include "system/util/hid_types.h"

#define DEF_VID_ENABLE

#define DEF_VID_ENABLE_ICON
//#define DEF_VID_ENABLE_NAME
#define DEF_VID_ICON_PATH				/*(const char*)(*/"romfs:/gfx/draw/icon/vid_icon.t3x"/*)*/
#define DEF_VID_NAME					/*(const char*)(*/"Video\nplayer"/*)*/
#define DEF_VID_VER						/*(const char*)(*/"v1.62.9"/*)*/
#define DEF_VID_SPEAKER_SESSION_ID		(uint8_t)(0)
#define DEF_VID_DECORDER_SESSION_ID		(uint8_t)(0)

bool Vid_query_init_flag(void);

bool Vid_query_running_flag(void);

void Vid_hid(const Hid_info* key);

void Vid_resume(void);

void Vid_suspend(void);

uint32_t Vid_load_msg(void);

void Vid_init(bool draw);

void Vid_exit(bool draw);

void Vid_main(void);

//HW color conversion mode values for Vid_get/set_use_hw_color_conversion.
#define VID_HW_CONV_CPU			((uint8_t)0)
#define VID_HW_CONV_Y2R_X2		((uint8_t)1)
#define VID_HW_CONV_NEON_Y2R	((uint8_t)2)

uint8_t Vid_get_use_hw_color_conversion(void);
void Vid_set_use_hw_color_conversion(uint8_t value);

// HW decoder preference: true = try MVD (H.264), false = always SW.
bool Vid_get_use_hw_decoding(void);
void Vid_set_use_hw_decoding(bool value);

//MVD texture upload path (user setting removed; runtime is fixed to Unroll4).
#define VID_MVD_UPLOAD_CLASSIC	((uint8_t)0)
#define VID_MVD_UPLOAD_UNROLL4	((uint8_t)1)
#define VID_MVD_UPLOAD_DMA		((uint8_t)2)

uint8_t Vid_get_mvd_upload_mode(void);
void    Vid_set_mvd_upload_mode(uint8_t value); /* Ignores value; always Unroll4 */

#endif //!defined(DEF_VIDEO_PLAYER_HPP)
