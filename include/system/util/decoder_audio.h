#if !defined(DEF_DECODER_AUDIO_H)
#define DEF_DECODER_AUDIO_H

#include <stdint.h>

#include "system/util/decoder_types.h"
#include "system/util/media_types.h"

struct AVPacket;

uint32_t DecoderAudio_init(uint8_t num_of_audio_tracks, uint8_t session);
void DecoderAudio_exit(uint8_t session);
void DecoderAudio_get_info(Media_a_info* audio_info, uint8_t audio_index, uint8_t session);
uint32_t DecoderAudio_ready_packet(uint8_t packet_index, uint8_t session);
void DecoderAudio_skip_packet(uint8_t packet_index, uint8_t session);
uint32_t DecoderAudio_decode(uint32_t* samples, uint8_t** raw_data, double* current_pos, uint8_t packet_index, uint8_t session);

uint32_t DecoderAudio_route_from_demux(uint8_t session, uint8_t track, struct AVPacket* front_pkt);

#endif
