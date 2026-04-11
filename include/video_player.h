#if !defined(DEF_VIDEO_PLAYER_HPP)
#define DEF_VIDEO_PLAYER_HPP
#include <stdbool.h>
#include <stdint.h>
#include "app_version.h"
#include "system/util/hid_types.h"

#define DEF_VID_ENABLE

#define DEF_VID_ENABLE_ICON
#define DEF_VID_ICON_PATH				/*(const char*)(*/"romfs:/gfx/draw/icon/vid_icon.t3x"/*)*/
#define DEF_VID_VER						"v" DEF_APP_VER_STRING
#define DEF_VID_SPEAKER_SESSION_ID		(uint8_t)(0)
#define DEF_VID_DECORDER_SESSION_ID		(uint8_t)(0)

bool Vid_query_init_flag(void);

bool Vid_query_running_flag(void);

void Vid_hid(const Hid_info* key);

void Vid_resume(void);

void Vid_suspend(void);

void Vid_init(bool draw);

void Vid_exit(bool draw);

void Vid_main(void);

//HW color conversion mode values for Vid_get/set_use_hw_color_conversion.
#define VID_HW_CONV_CPU			((uint8_t)0)
#define VID_HW_CONV_Y2R_X2		((uint8_t)1)
#define VID_HW_CONV_NEON_Y2R	((uint8_t)2)

uint8_t Vid_get_use_hw_color_conversion(void);
void Vid_set_use_hw_color_conversion(uint8_t value);

// HW decoder: true = try MVD when the stream supports it, else SW. Not forced off by O3DS or fake O3DS mode.
bool Vid_get_use_hw_decoding(void);
void Vid_set_use_hw_decoding(bool value);

#endif //!defined(DEF_VIDEO_PLAYER_HPP)
