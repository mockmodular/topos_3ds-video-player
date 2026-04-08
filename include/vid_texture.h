#if !defined(DEF_VID_TEXTURE_HPP)
#define DEF_VID_TEXTURE_HPP

#include <stdbool.h>
#include <stdint.h>

#include "vid_state.h"
#include "system/util/converter_types.h"

uint32_t Vid_get_min_texture_size(uint32_t width_or_height);
bool     Vid_texture_dimensions_prefer_nearest(uint32_t codec_width, uint32_t codec_height);
void     Vid_large_texture_free(Large_image* large_image_data);
void     Vid_large_texture_set_filter(Large_image* large_image_data, bool filter);
void     Vid_large_texture_crop(Large_image* large_image_data, uint32_t width, uint32_t height);
uint32_t Vid_large_texture_init(Large_image* large_image_data, uint32_t width, uint32_t height, Raw_pixel color_format, bool zero_initialize);
uint32_t Vid_large_texture_set_data(Large_image* large_image_data, uint8_t* raw_image, uint32_t width, uint32_t height, bool use_direct);
void     Vid_large_texture_draw(Large_image* large_image_data, double x_offset, double y_offset, double pic_width, double pic_height);

#endif //!defined(DEF_VID_TEXTURE_HPP)
