#if !defined(DEF_DECODER_VIDEO_SOFT_H)
#define DEF_DECODER_VIDEO_SOFT_H

#include <stdbool.h>
#include <stdint.h>

#include "system/util/decoder_types.h"
#include "system/util/media_types.h"

struct AVCodecContext;
struct AVPacket;

void DecoderVideoSoft_set_enabled_cores(const bool frame_threading_cores[4], const bool slice_threading_cores[4], int worker_start_core);
uint32_t DecoderVideoSoft_init(uint8_t low_resolution, uint8_t num_of_video_tracks, uint8_t num_of_threads, Media_thread_type thread_type, uint8_t session);
void DecoderVideoSoft_get_info(Media_v_info* video_info, uint8_t video_index, uint8_t session);
uint32_t DecoderVideoSoft_route_from_demux(uint8_t session, uint8_t track, struct AVPacket* front_pkt);
uint32_t DecoderVideoSoft_ready_packet(int8_t packet_index, int8_t session);
void DecoderVideoSoft_skip_packet(uint8_t packet_index, uint8_t session);
void DecoderVideoSoft_set_raw_image_buffer_size(uint32_t max_num_of_buffer, uint8_t packet_index, uint8_t session);
uint32_t DecoderVideoSoft_get_raw_image_buffer_size(uint8_t packet_index, uint8_t session);
uint32_t DecoderVideoSoft_decode(uint8_t packet_index, uint8_t session);
void DecoderVideoSoft_clear_raw_image(uint8_t packet_index, uint8_t session);
uint16_t DecoderVideoSoft_get_available_raw_image_num(uint8_t packet_index, uint8_t session);
uint32_t DecoderVideoSoft_get_image(uint8_t** raw_data, double* current_pos, uint32_t width, uint32_t height, uint8_t packet_index, uint8_t session);
void DecoderVideoSoft_skip_image(double* current_pos, uint8_t packet_index, uint8_t session);
void DecoderVideoSoft_exit(uint8_t session);

bool DecoderVideoSoft_is_track_initialized(uint8_t session, uint8_t track);
struct AVCodecContext* DecoderVideoSoft_codec_context(uint8_t session, uint8_t track);
struct AVPacket* DecoderVideoSoft_packet(uint8_t session, uint8_t track);
bool DecoderVideoSoft_is_packet_ready(uint8_t session, uint8_t track);
void DecoderVideoSoft_release_ready_packet(uint8_t session, uint8_t track);

#endif
