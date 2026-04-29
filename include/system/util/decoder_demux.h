#if !defined(DEF_DECODER_DEMUX_H)
#define DEF_DECODER_DEMUX_H

#include <stdbool.h>
#include <stdint.h>

#include "system/util/decoder_types.h"
#include "system/util/media_types.h"

struct AVFormatContext;
struct AVPacket;
struct AVCodecParameters;

bool DecoderDemux_is_opened(uint8_t session);
void DecoderDemux_set_opened(uint8_t session, bool opened);

uint32_t DecoderDemux_open(const char* path, uint8_t* num_of_audio_tracks, uint8_t* num_of_video_tracks, uint8_t session);

struct AVFormatContext* DecoderDemux_format_context(uint8_t session);

uint8_t DecoderDemux_audio_stream_num(uint8_t session, uint8_t audio_index);
uint8_t DecoderDemux_video_stream_num(uint8_t session, uint8_t video_index);

void DecoderDemux_clear_cache_packet(uint8_t session);
uint16_t DecoderDemux_get_available_packet_num(uint8_t session);
uint32_t DecoderDemux_read_packet(uint8_t session);

typedef uint32_t (*DecoderDemuxRouteAudioFn)(uint8_t session, uint8_t track, struct AVPacket* front_pkt);
typedef uint32_t (*DecoderDemuxRouteVideoFn)(uint8_t session, uint8_t track, struct AVPacket* front_pkt);

uint32_t DecoderDemux_parse_packet(Media_packet_type* type, uint8_t* packet_index, bool* key_frame, uint8_t session,
	DecoderDemuxRouteAudioFn route_audio,
	DecoderDemuxRouteVideoFn route_video);

uint32_t DecoderDemux_seek(uint64_t seek_pos, Media_seek_flag flag, uint8_t session);

/** Request FFmpeg demux I/O (seek/read) to return AVERROR_EXIT ASAP (playback abort / watchdog). */
void DecoderDemux_request_io_abort(uint8_t session);
void DecoderDemux_clear_io_abort(uint8_t session);
bool DecoderDemux_io_abort_requested(uint8_t session);

void DecoderDemux_free_cache_and_close_format(uint8_t session);

#endif
