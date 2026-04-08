//Includes.
#include "system/util/decoder.h"
#include "system/util/decoder_audio.h"
#include "system/util/decoder_demux.h"
#include "system/util/decoder_video_soft.h"
#include "system/util/decoder_mvd.h"

#include <stdbool.h>
#include <stdint.h>

#include "3ds.h"

#include "system/util/converter_types.h"
#include "system/util/err_types.h"
#include "system/util/log.h"
#include "system/util/media_types.h"
#include "system/util/util.h"

#if DEF_DECODER_IMAGE_API_ENABLE
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif //DEF_DECODER_IMAGE_API_ENABLE

//Defines.
//N/A.

//Typedefs.
//N/A.

//Variables.
//Code.
#if DEF_DECODER_VIDEO_AUDIO_API_ENABLE
//We can't get rid of this "int" because library uses "int" type as args.
// static void Util_decoder_video_log_callback(void *avcl, int level, const char *fmt, va_list list)
// {
// 	if(level > AV_LOG_TRACE)
// 		return;

// 	DEF_LOG_VFORMAT(fmt, list);
// }

uint32_t Util_decoder_open_file(const char* path, uint8_t* num_of_audio_tracks, uint8_t* num_of_video_tracks, uint8_t session)
{
	return DecoderDemux_open(path, num_of_audio_tracks, num_of_video_tracks, session);
}

uint32_t Util_decoder_audio_init(uint8_t num_of_audio_tracks, uint8_t session)
{
	return DecoderAudio_init(num_of_audio_tracks, session);
}

void Util_decoder_video_set_enabled_cores(const bool frame_threading_cores[4], const bool slice_threading_cores[4], int worker_start_core)
{
	DecoderVideoSoft_set_enabled_cores(frame_threading_cores, slice_threading_cores, worker_start_core);
}

uint32_t Util_decoder_video_init(uint8_t low_resolution, uint8_t num_of_video_tracks, uint8_t num_of_threads, Media_thread_type thread_type, uint8_t session)
{
	return DecoderVideoSoft_init(low_resolution, num_of_video_tracks, num_of_threads, thread_type, session);
}

uint32_t Util_decoder_mvd_init(uint8_t session)
{
	return DecoderMvd_init(session);
}

void Util_decoder_audio_get_info(Media_a_info* audio_info, uint8_t audio_index, uint8_t session)
{
	DecoderAudio_get_info(audio_info, audio_index, session);
}

void Util_decoder_video_get_info(Media_v_info* video_info, uint8_t video_index, uint8_t session)
{
	DecoderVideoSoft_get_info(video_info, video_index, session);
}

void Util_decoder_clear_cache_packet(uint8_t session)
{
	DecoderDemux_clear_cache_packet(session);
}

uint16_t Util_decoder_get_available_packet_num(uint8_t session)
{
	return DecoderDemux_get_available_packet_num(session);
}

uint32_t Util_decoder_read_packet(uint8_t session)
{
	return DecoderDemux_read_packet(session);
}

uint32_t Util_decoder_parse_packet(Media_packet_type* type, uint8_t* packet_index, bool* key_frame, uint8_t session)
{
	return DecoderDemux_parse_packet(type, packet_index, key_frame, session, DecoderAudio_route_from_demux, DecoderVideoSoft_route_from_demux);
}

uint32_t Util_decoder_ready_audio_packet(uint8_t packet_index, uint8_t session)
{
	return DecoderAudio_ready_packet(packet_index, session);
}

uint32_t Util_decoder_ready_video_packet(int8_t packet_index, int8_t session)
{
	return DecoderVideoSoft_ready_packet(packet_index, session);
}

void Util_decoder_skip_audio_packet(uint8_t packet_index, uint8_t session)
{
	DecoderAudio_skip_packet(packet_index, session);
}

void Util_decoder_skip_video_packet(uint8_t packet_index, uint8_t session)
{
	DecoderVideoSoft_skip_packet(packet_index, session);
}

void Util_decoder_video_set_raw_image_buffer_size(uint32_t max_num_of_buffer, uint8_t packet_index, uint8_t session)
{
	DecoderVideoSoft_set_raw_image_buffer_size(max_num_of_buffer, packet_index, session);
}

void Util_decoder_mvd_set_raw_image_buffer_size(uint32_t max_num_of_buffer, uint8_t session)
{
	DecoderMvd_set_raw_image_buffer_size(max_num_of_buffer, session);
}

uint32_t Util_decoder_video_get_raw_image_buffer_size(uint8_t packet_index, uint8_t session)
{
	return DecoderVideoSoft_get_raw_image_buffer_size(packet_index, session);
}

uint32_t Util_decoder_mvd_get_raw_image_buffer_size(uint8_t session)
{
	return DecoderMvd_get_raw_image_buffer_size(session);
}

uint32_t Util_decoder_audio_decode(uint32_t* samples, uint8_t** raw_data, double* current_pos, uint8_t packet_index, uint8_t session)
{
	return DecoderAudio_decode(samples, raw_data, current_pos, packet_index, session);
}

uint32_t Util_decoder_video_decode(uint8_t packet_index, uint8_t session)
{
	return DecoderVideoSoft_decode(packet_index, session);
}

uint32_t Util_decoder_mvd_decode(uint8_t session)
{
	return DecoderMvd_decode(session);
}

void Util_decoder_video_clear_raw_image(uint8_t packet_index, uint8_t session)
{
	DecoderVideoSoft_clear_raw_image(packet_index, session);
}

void Util_decoder_mvd_clear_raw_image(uint8_t session)
{
	DecoderMvd_clear_raw_image(session);
}

uint16_t Util_decoder_video_get_available_raw_image_num(uint8_t packet_index, uint8_t session)
{
	return DecoderVideoSoft_get_available_raw_image_num(packet_index, session);
}

uint16_t Util_decoder_mvd_get_available_raw_image_num(uint8_t session)
{
	return DecoderMvd_get_available_raw_image_num(session);
}

uint32_t Util_decoder_video_get_image(uint8_t** raw_data, double* current_pos, uint32_t width, uint32_t height, uint8_t packet_index, uint8_t session)
{
	return DecoderVideoSoft_get_image(raw_data, current_pos, width, height, packet_index, session);
}

uint32_t Util_decoder_mvd_get_image(uint8_t** raw_data, double* current_pos, uint32_t width, uint32_t height, uint8_t session)
{
	return DecoderMvd_get_image(raw_data, current_pos, width, height, session);
}

void Util_decoder_video_skip_image(double* current_pos, uint8_t packet_index, uint8_t session)
{
	DecoderVideoSoft_skip_image(current_pos, packet_index, session);
}

void Util_decoder_mvd_skip_image(double* current_pos, uint8_t session)
{
	DecoderMvd_skip_image(current_pos, session);
}

uint32_t Util_decoder_seek(uint64_t seek_pos, Media_seek_flag flag, uint8_t session)
{
	return DecoderDemux_seek(seek_pos, flag, session);
}

void Util_decoder_close_file(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return;

	if(!DecoderDemux_is_opened(session))
		return;

	DecoderDemux_set_opened(session, false);
	DecoderAudio_exit(session);
	DecoderVideoSoft_exit(session);
	DecoderMvd_exit(session);
	DecoderDemux_free_cache_and_close_format(session);
}

#endif //DEF_DECODER_VIDEO_AUDIO_API_ENABLE

#if DEF_DECODER_IMAGE_API_ENABLE
uint32_t Util_decoder_image_decode(const char* path, uint8_t** raw_data, uint32_t* width, uint32_t* height, Raw_pixel* format)
{
	//We can't get rid of this "int" because library uses "int" type as args.
	int ch = 0, w = 0, h = 0;

	if(!path || !raw_data || !width || !height || !format)
		goto invalid_arg;

	*raw_data = stbi_load(path, &w, &h, &ch, STBI_default);
	if(!*raw_data)
	{
		DEF_LOG_RESULT(stbi_load, false, DEF_ERR_STB_IMG_RETURNED_NOT_SUCCESS);
		DEF_LOG_STRING(stbi_failure_reason());
		goto stbi_api_failed;
	}
	*width = w;
	*height = h;

	if(ch == 4)
		*format = RAW_PIXEL_RGBA8888;
	else if(ch == 3)
		*format = RAW_PIXEL_RGB888;
	else if(ch == 2)
		*format = RAW_PIXEL_GRAYALPHA88;
	else
		*format = RAW_PIXEL_GRAY8;

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	stbi_api_failed:
	return DEF_ERR_STB_IMG_RETURNED_NOT_SUCCESS;
}

uint32_t Util_decoder_image_decode_data(const uint8_t* compressed_data, uint32_t compressed_buffer_size, uint8_t** raw_data, uint32_t* width, uint32_t* height, Raw_pixel* format)
{
	//We can't get rid of this "int" because library uses "int" type as args.
	int ch = 0, w = 0, h = 0;

	if(!compressed_data || compressed_buffer_size == 0 || !raw_data || !width || !height || !format)
		goto invalid_arg;

	*raw_data = stbi_load_from_memory(compressed_data, compressed_buffer_size, &w, &h, &ch, STBI_default);
	if(!*raw_data)
	{
		DEF_LOG_RESULT(stbi_load_from_memory, false, DEF_ERR_STB_IMG_RETURNED_NOT_SUCCESS);
		DEF_LOG_STRING(stbi_failure_reason());
		goto stbi_api_failed;
	}
	*width = w;
	*height = h;

	if(ch == 4)
		*format = RAW_PIXEL_RGBA8888;
	else if(ch == 3)
		*format = RAW_PIXEL_RGB888;
	else if(ch == 2)
		*format = RAW_PIXEL_GRAYALPHA88;
	else
		*format = RAW_PIXEL_GRAY8;

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	stbi_api_failed:
	return DEF_ERR_STB_IMG_RETURNED_NOT_SUCCESS;
}
#endif //DEF_DECODER_IMAGE_API_ENABLE
