#if !defined(DEF_DECODER_MVD_H)
#define DEF_DECODER_MVD_H

#include <stdint.h>

uint32_t DecoderMvd_init(uint8_t session);
void DecoderMvd_set_raw_image_buffer_size(uint32_t max_num_of_buffer, uint8_t session);
uint32_t DecoderMvd_get_raw_image_buffer_size(uint8_t session);
uint32_t DecoderMvd_decode(uint8_t session);
void DecoderMvd_clear_raw_image(uint8_t session);
uint16_t DecoderMvd_get_available_raw_image_num(uint8_t session);
uint32_t DecoderMvd_get_image(uint8_t** raw_data, double* current_pos, uint32_t width, uint32_t height, uint8_t session);
void DecoderMvd_skip_image(double* current_pos, uint8_t session);
void DecoderMvd_exit(uint8_t session);

#endif
