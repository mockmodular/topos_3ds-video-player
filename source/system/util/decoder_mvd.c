#include "system/util/decoder_mvd.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "3ds.h"

#include "system/util/decoder_demux.h"
#include "system/util/decoder_video_soft.h"
#include "system/util/err_types.h"
#include "system/util/log.h"
#include "system/util/util.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

extern void memcpy_asm(uint8_t*, uint8_t*, uint32_t);


//MVD_OUTPUT_ABGR32 (0x00041002): supported by N3DS firmware (MVD inits OK, audio works), but A byte = 0 (H264 has no alpha).
//Fix: after MVD decode, set A=0xFF per pixel. No BGR565 intermediate step needed.
#define MVD_OUTPUT_ABGR32 ((MVDSTD_OutputFormat)0x00041002)

static bool util_mvd_video_decoder_init = false;
static bool util_mvd_video_decoder_changeable_buffer_size = false;
static bool util_mvd_video_decoder_first = false;
static bool util_mvd_video_decoder_should_skip_process_nal_unit = false;
static uint8_t util_mvd_video_decoder_current_cached_pts_index = 0;
static uint8_t util_mvd_video_decoder_next_cached_pts_index = 0;
static uint8_t* util_mvd_video_decoder_packet = NULL;
static uint16_t util_mvd_video_decoder_available_raw_image[DEF_DECODER_MAX_SESSIONS] = { 0, };
static uint16_t util_mvd_video_decoder_raw_image_ready_index[DEF_DECODER_MAX_SESSIONS] = { 0, };
static uint16_t util_mvd_video_decoder_raw_image_current_index[DEF_DECODER_MAX_SESSIONS] = { 0, };
static uint16_t util_mvd_video_decoder_max_raw_image[DEF_DECODER_MAX_SESSIONS] = { 0, };
static uint32_t util_mvd_video_decoder_packet_size = 0;
static int64_t util_mvd_video_decoder_cached_pts[32] = { 0, };
static LightLock util_mvd_video_decoder_raw_image_mutex[DEF_DECODER_MAX_SESSIONS] = { 0, };
static AVFrame* util_mvd_video_decoder_raw_image[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_RAW_IMAGE] = { 0, };

static MVDSTD_Config util_decoder_mvd_config = { .input_type = MVD_INPUT_H264, .output_type = MVD_OUTPUT_ABGR32, 0, };

uint32_t DecoderMvd_init(uint8_t session)
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t size = 0;
	uint32_t result = DEF_ERR_OTHER;
	MVDSTD_CalculateWorkBufSizeConfig config = { 0, };

	if(session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0))
		goto not_inited;

	if(util_mvd_video_decoder_init)
		goto already_inited;

	width = DecoderVideoSoft_codec_context(session, 0)->width;
	height = DecoderVideoSoft_codec_context(session, 0)->height;

	for(uint8_t i = 0; i < 32; i++)
		util_mvd_video_decoder_cached_pts[i] = 0;

	util_mvd_video_decoder_current_cached_pts_index = 0;
	util_mvd_video_decoder_next_cached_pts_index = 0;

	util_mvd_video_decoder_raw_image_current_index[session] = 0;
	util_mvd_video_decoder_raw_image_ready_index[session] = 0;
	util_mvd_video_decoder_available_raw_image[session] = 0;
	util_mvd_video_decoder_should_skip_process_nal_unit = false;

	util_mvd_video_decoder_packet_size = (1000 * 256);
	util_mvd_video_decoder_packet = (uint8_t*)linearAlloc(util_mvd_video_decoder_packet_size);
	if(!util_mvd_video_decoder_packet)
		goto out_of_linear_memory;

	config.level.enable = true;
	config.level.flag = (MVD_CALC_WITH_LEVEL_FLAG_ENABLE_CALC | MVD_CALC_WITH_LEVEL_FLAG_ENABLE_EXTRA_OP | MVD_CALC_WITH_LEVEL_FLAG_UNK);
	config.level.level = 0xFF;
	config.width = width;
	config.height = height;

	switch(DecoderVideoSoft_codec_context(session, 0)->level)
	{
		case 10: config.level.level = MVD_H264_LEVEL_1_0; break; //Level 1.0.
		case 9: config.level.level = MVD_H264_LEVEL_1_0B; break; //Level 1.0b.
		case 11: config.level.level = MVD_H264_LEVEL_1_1; break; //Level 1.1.
		case 12: config.level.level = MVD_H264_LEVEL_1_2; break; //Level 1.2.
		case 13: config.level.level = MVD_H264_LEVEL_1_3; break; //Level 1.3.
		case 20: config.level.level = MVD_H264_LEVEL_2_0; break; //Level 2.0.
		case 21: config.level.level = MVD_H264_LEVEL_2_1; break; //Level 2.1.
		case 22: config.level.level = MVD_H264_LEVEL_2_2; break; //Level 2.2.
		case 30: config.level.level = MVD_H264_LEVEL_3_0; break; //Level 3.0.
		case 31: config.level.level = MVD_H264_LEVEL_3_1; break; //Level 3.1.
		case 32: config.level.level = MVD_H264_LEVEL_3_2; break; //Level 3.2.
		case 40: config.level.level = MVD_H264_LEVEL_4_0; break; //Level 4.0.
		case 41: config.level.level = MVD_H264_LEVEL_4_1; break; //Level 4.1.
		case 42: config.level.level = MVD_H264_LEVEL_4_2; break; //Level 4.2.
		case 50: config.level.level = MVD_H264_LEVEL_5_0; break; //Level 5.0.
		case 51: config.level.level = MVD_H264_LEVEL_5_1; break; //Level 5.1.
		case 52: config.level.level = MVD_H264_LEVEL_5_2; break; //Level 5.2.

		case 60: DEF_LOG_STRING("Level 6.0 is NOT supported!!!!!"); break;
		case 61: DEF_LOG_STRING("Level 6.1 is NOT supported!!!!!"); break;
		case 62: DEF_LOG_STRING("Level 6.2 is NOT supported!!!!!"); break;

		default:
		{
			DEF_LOG_STRING("Unknown level!!!!!");
			DEF_LOG_INT(DecoderVideoSoft_codec_context(session, 0)->level);
			break;
		}
	}

	if(config.level.level == 0xFF)
		goto unsupported_levels;

	DEF_LOG_FORMAT("%" PRIu8 "(%" PRIi32 ") %" PRIu32 "x%" PRIu32, config.level.level, DecoderVideoSoft_codec_context(session, 0)->level, config.width, config.height);
	result = mvdstdCalculateBufferSize(&config, &size);

	if(result != DEF_SUCCESS)
	{
		DEF_LOG_RESULT(mvdstdCalculateBufferSize, false, result);
		goto nintendo_api_failed;
	}

	DEF_LOG_FORMAT("%fMB (%" PRIu32 "B)", (size / 1000.0 / 1000.0), size);
	result = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_ABGR32, size, NULL);
	if(result != DEF_SUCCESS)
	{
		DEF_LOG_RESULT(mvdstdInit, false, result);
		goto nintendo_api_failed_1;
	}

	LightLock_Init(&util_mvd_video_decoder_raw_image_mutex[session]);

	util_mvd_video_decoder_max_raw_image[session] = DEF_DECODER_MAX_RAW_IMAGE;
	util_mvd_video_decoder_first = true;
	util_mvd_video_decoder_changeable_buffer_size = true;
	util_mvd_video_decoder_init = true;
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	already_inited:
	return DEF_ERR_ALREADY_INITIALIZED;

	out_of_linear_memory:
	return DEF_ERR_OUT_OF_LINEAR_MEMORY;

	unsupported_levels:
	free(util_mvd_video_decoder_packet);
	util_mvd_video_decoder_packet = NULL;
	return DEF_ERR_OTHER;

	nintendo_api_failed:
	free(util_mvd_video_decoder_packet);
	util_mvd_video_decoder_packet = NULL;
	return result;

	nintendo_api_failed_1:
	free(util_mvd_video_decoder_packet);
	util_mvd_video_decoder_packet = NULL;
	mvdstdExit();
	return result;
}

void DecoderMvd_set_raw_image_buffer_size(uint32_t max_num_of_buffer, uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || max_num_of_buffer < 3 || max_num_of_buffer > DEF_DECODER_MAX_RAW_IMAGE)
		return;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0) || !util_mvd_video_decoder_init
	|| !util_mvd_video_decoder_changeable_buffer_size)
		return;

	util_mvd_video_decoder_max_raw_image[session] = max_num_of_buffer;
}

uint32_t DecoderMvd_get_raw_image_buffer_size(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return 0;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0) || !util_mvd_video_decoder_init)
		return 0;

	return util_mvd_video_decoder_max_raw_image[session];
}

uint32_t DecoderMvd_decode(uint8_t session)
{
	bool got_a_frame = false;
	bool got_a_frame_after_processing_nal_unit = false;
	uint32_t offset = 0;
	uint32_t source_offset = 0;
	uint32_t size = 0;
	uint16_t buffer_num = 0;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t result = DEF_ERR_OTHER;

	if(session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0) || !util_mvd_video_decoder_init)
		goto not_inited;

	if(!DecoderVideoSoft_is_packet_ready(session, 0))
	{
		//DEF_LOG_STRING("No packets are available!!!!!");
		goto try_again;
	}

	util_mvd_video_decoder_changeable_buffer_size = false;
	if(util_mvd_video_decoder_available_raw_image[session] + 1 >= util_mvd_video_decoder_max_raw_image[session])
	{
		//DEF_LOG_STRING("Queues are full!!!!!");
		goto try_again;
	}

	buffer_num = util_mvd_video_decoder_raw_image_current_index[session];
	width = DecoderVideoSoft_codec_context(session, 0)->width;
	height = DecoderVideoSoft_codec_context(session, 0)->height;
	if(width % 16 != 0)
		width += 16 - width % 16;
	if(height % 16 != 0)
		height += 16 - height % 16;

	util_mvd_video_decoder_raw_image[session][buffer_num] = av_frame_alloc();
	if(!util_mvd_video_decoder_raw_image[session][buffer_num])
	{
		DEF_LOG_RESULT(av_frame_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		goto ffmpeg_api_failed;
	}

	util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] = (uint8_t*)linearAlloc(width * height * 4);
	if(!util_mvd_video_decoder_raw_image[session][buffer_num]->data[0])
		goto out_of_linear_memory;

	if(DecoderVideoSoft_packet(session, 0)->size > (int32_t)util_mvd_video_decoder_packet_size)
	{
		util_mvd_video_decoder_packet_size = DecoderVideoSoft_packet(session, 0)->size;
		free(util_mvd_video_decoder_packet);
		util_mvd_video_decoder_packet = NULL;
		util_mvd_video_decoder_packet = (uint8_t*)linearAlloc(util_mvd_video_decoder_packet_size);
		if(!util_mvd_video_decoder_packet)
		{
			util_mvd_video_decoder_packet_size = 0;
			goto out_of_linear_memory;
		}
	}

	if(util_mvd_video_decoder_first)
	{
		mvdstdGenerateDefaultConfig(&util_decoder_mvd_config, width, height, width, height, NULL, NULL, NULL);
		util_decoder_mvd_config.output_type = MVD_OUTPUT_ABGR32;//Force ABGR32 output (GenerateDefaultConfig may reset this to the mvdstdInit value).

		//Set extra data.
		offset = 0;
		memset(util_mvd_video_decoder_packet, 0x0, 0x2);
		offset += 2;
		memset(util_mvd_video_decoder_packet + offset, 0x1, 0x1);
		offset += 1;
		memcpy(util_mvd_video_decoder_packet + offset, DecoderVideoSoft_codec_context(session, 0)->extradata + 8, *(DecoderVideoSoft_codec_context(session, 0)->extradata + 7));
		offset += *(DecoderVideoSoft_codec_context(session, 0)->extradata + 7);

		mvdstdProcessVideoFrame(util_mvd_video_decoder_packet, offset, 0, NULL);

		offset = 0;
		memset(util_mvd_video_decoder_packet, 0x0, 0x2);
		offset += 2;
		memset(util_mvd_video_decoder_packet + offset, 0x1, 0x1);
		offset += 1;
		memcpy(util_mvd_video_decoder_packet + offset, DecoderVideoSoft_codec_context(session, 0)->extradata + 11 + *(DecoderVideoSoft_codec_context(session, 0)->extradata + 7), *(DecoderVideoSoft_codec_context(session, 0)->extradata + 10 + *(DecoderVideoSoft_codec_context(session, 0)->extradata + 7)));
		offset += *(DecoderVideoSoft_codec_context(session, 0)->extradata + 10 + *(DecoderVideoSoft_codec_context(session, 0)->extradata + 7));

		mvdstdProcessVideoFrame(util_mvd_video_decoder_packet, offset, 0, NULL);
	}
	util_decoder_mvd_config.physaddr_outdata0 = osConvertVirtToPhys(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);

	offset = 0;
	source_offset = 0;

	while((source_offset + 4) < (uint32_t)DecoderVideoSoft_packet(session, 0)->size)
	{
		//Get nal size.
		size = *((uint32_t*)(DecoderVideoSoft_packet(session, 0)->data + source_offset));
		size = __builtin_bswap32(size);
		source_offset += 4;
		if((source_offset + size) > (uint32_t)DecoderVideoSoft_packet(session, 0)->size || size == 0)
		{
			if(size == 0)
				DEF_LOG_FORMAT("unexpected nal size : %" PRIu32, size);
			else
			{
				DEF_LOG_FORMAT("unexpected nal size : %" PRIu32 " (%" PRIu32 " > %" PRIu32 ")", size,
				(source_offset + size), (uint32_t)DecoderVideoSoft_packet(session, 0)->size);
			}

			goto ffmpeg_api_failed;
		}

		//Set nal prefix 0x0 0x0 0x1.
		memset(util_mvd_video_decoder_packet + offset, 0x0, 0x2);
		offset += 2;
		memset(util_mvd_video_decoder_packet + offset, 0x1, 0x1);
		offset += 1;

		//Copy raw nal data.
		memcpy(util_mvd_video_decoder_packet + offset, (DecoderVideoSoft_packet(session, 0)->data + source_offset), size);
		offset += size;
		source_offset += size;
	}

	//Set 0x11 to top-left, top-right, bottom-left and bottom-right then check them later.
	//For more information, see : https://gbatemp.net/threads/release-video-player-for-3ds.586094/page-20#post-9915780
	*util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] = 0x11;
	*(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + (width * 2 - 1)) = 0x11;
	*(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + ((width * height * 2) - (width * 2))) = 0x11;
	*(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + (width * height * 2 - 1)) = 0x11;

	// DEF_LOG_STRING("-------------------------------");

	MVDSTD_SetConfig(&util_decoder_mvd_config);

	if(!util_mvd_video_decoder_should_skip_process_nal_unit)
	{
		// DEF_LOG_STRING("util_mvd_video_decoder_should_skip_process_nal_unit is not set, so call mvdstdProcessVideoFrame()");
		result = mvdstdProcessVideoFrame(util_mvd_video_decoder_packet, offset, 0, NULL);

		//Save pts
		util_mvd_video_decoder_cached_pts[util_mvd_video_decoder_next_cached_pts_index] = DecoderVideoSoft_packet(session, 0)->dts;
		if(util_mvd_video_decoder_next_cached_pts_index + 1 < 32)
			util_mvd_video_decoder_next_cached_pts_index++;
		else
			util_mvd_video_decoder_next_cached_pts_index = 0;

		if(util_mvd_video_decoder_first)
		{
			//Do I need to send same nal data at first frame?
			result = mvdstdProcessVideoFrame(util_mvd_video_decoder_packet, offset, 0, NULL);
			util_mvd_video_decoder_first = false;
		}

		//If any of them got changed, it means MVD service wrote the frame data to the buffer.
		if(*util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] != 0x11
		|| *(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + (width * 2 - 1)) != 0x11
		|| *(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + ((width * height * 2) - (width * 2))) != 0x11
		|| *(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + (width * height * 2 - 1)) != 0x11)
		{
			// DEF_LOG_STRING("got a frame after mvdstdProcessVideoFrame()");
			got_a_frame = true;
			util_mvd_video_decoder_should_skip_process_nal_unit = true;
			got_a_frame_after_processing_nal_unit = true;
		}
		// else
		// 	DEF_LOG_STRING("no frames after mvdstdProcessVideoFrame()");

		if(result != MVD_STATUS_FRAMEREADY && result != MVD_STATUS_PARAMSET)
		{
			DEF_LOG_RESULT(mvdstdProcessVideoFrame, false, result);
			goto nintendo_api_failed;
		}
	}
	// else
	// 	DEF_LOG_STRING("util_mvd_video_decoder_should_skip_process_nal_unit is set, so skip mvdstdProcessVideoFrame()");

	if(!got_a_frame)
	{
		// DEF_LOG_STRING("got_a_frame is not set, so call mvdstdRenderVideoFrame()");
		while(true)
		{
			//You need to use a custom libctru to use NULL here. https://github.com/Core-2-Extreme/libctru_custom/tree/3ds
			result = mvdstdRenderVideoFrame(NULL, false);

			//If any of them got changed, it means MVD service wrote the frame data to the buffer.
			if(*util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] != 0x11
			|| *(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + (width * 2 - 1)) != 0x11
			|| *(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + ((width * height * 2) - (width * 2))) != 0x11
			|| *(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] + (width * height * 2 - 1)) != 0x11)
			{
				// DEF_LOG_STRING("got a frame after mvdstdRenderVideoFrame()");
				result = MVD_STATUS_OK;
				got_a_frame = true;
			}
			// else
			// 	DEF_LOG_STRING("no frames after mvdstdRenderVideoFrame()");

			if(result != MVD_STATUS_BUSY || got_a_frame)
				break;
			// else
			// 	DEF_LOG_STRING("mvdstdRenderVideoFrame() returned MVD_STATUS_BUSY, so try again");
		}

		if(result != MVD_STATUS_OK)
		{
			DEF_LOG_RESULT(mvdstdRenderVideoFrame, false, result);
			goto nintendo_api_failed;
		}
	}
	// else
	// 	DEF_LOG_STRING("got_a_frame is set, so skip mvdstdRenderVideoFrame()");

	if(!got_a_frame && util_mvd_video_decoder_should_skip_process_nal_unit)
	{
		// DEF_LOG_STRING("util_mvd_video_decoder_should_skip_process_nal_unit is set, and got no frames");
		util_mvd_video_decoder_should_skip_process_nal_unit = false;
		goto try_again_no_output;
	}
	else if(!got_a_frame)
	{
		// DEF_LOG_STRING("Got no frames");
		goto need_more_packet;
	}

	//Restore cached pts.
	util_mvd_video_decoder_raw_image[session][buffer_num]->pts = util_mvd_video_decoder_cached_pts[util_mvd_video_decoder_current_cached_pts_index];
	util_mvd_video_decoder_raw_image[session][buffer_num]->duration = DecoderVideoSoft_packet(session, 0)->duration;
	if(util_mvd_video_decoder_current_cached_pts_index + 1 < 32)
		util_mvd_video_decoder_current_cached_pts_index++;
	else
		util_mvd_video_decoder_current_cached_pts_index = 0;

	if(buffer_num + 1 < util_mvd_video_decoder_max_raw_image[session])
		util_mvd_video_decoder_raw_image_current_index[session]++;
	else
		util_mvd_video_decoder_raw_image_current_index[session] = 0;

	LightLock_Lock(&util_mvd_video_decoder_raw_image_mutex[session]);
	util_mvd_video_decoder_available_raw_image[session]++;
	LightLock_Unlock(&util_mvd_video_decoder_raw_image_mutex[session]);

	if(util_mvd_video_decoder_should_skip_process_nal_unit && !got_a_frame_after_processing_nal_unit)
	{
		// DEF_LOG_STRING("util_mvd_video_decoder_should_skip_process_nal_unit is set, and got a frame");
		goto try_again_with_output;
	}

	DecoderVideoSoft_release_ready_packet(session, 0);
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	return DEF_ERR_TRY_AGAIN;

	try_again_no_output:
	free(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);
	util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] = NULL;
	av_frame_free(&util_mvd_video_decoder_raw_image[session][buffer_num]);
	return DEF_ERR_DECODER_TRY_AGAIN_NO_OUTPUT;

	try_again_with_output:
	return DEF_ERR_DECODER_TRY_AGAIN;

	need_more_packet:
	DecoderVideoSoft_release_ready_packet(session, 0);
	free(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);
	util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] = NULL;
	av_frame_free(&util_mvd_video_decoder_raw_image[session][buffer_num]);
	return DEF_ERR_NEED_MORE_INPUT;

	out_of_linear_memory:
	DecoderVideoSoft_release_ready_packet(session, 0);
	free(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);
	util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] = NULL;
	av_frame_free(&util_mvd_video_decoder_raw_image[session][buffer_num]);
	return DEF_ERR_OUT_OF_LINEAR_MEMORY;

	ffmpeg_api_failed:
	DecoderVideoSoft_release_ready_packet(session, 0);
	free(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);
	util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] = NULL;
	av_frame_free(&util_mvd_video_decoder_raw_image[session][buffer_num]);
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;

	nintendo_api_failed:
	DecoderVideoSoft_release_ready_packet(session, 0);
	free(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);
	util_mvd_video_decoder_raw_image[session][buffer_num]->data[0] = NULL;
	av_frame_free(&util_mvd_video_decoder_raw_image[session][buffer_num]);
	return result;
}


void DecoderMvd_clear_raw_image(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0) || !util_mvd_video_decoder_init)
		return;

	for(uint16_t i = 0; i < util_mvd_video_decoder_max_raw_image[session]; i++)
	{
		if(util_mvd_video_decoder_raw_image[session][i])
		{
			free(util_mvd_video_decoder_raw_image[session][i]->data[0]);
			for(uint8_t k = 0; k < AV_NUM_DATA_POINTERS; k++)
				util_mvd_video_decoder_raw_image[session][i]->data[k] = NULL;
		}
		av_frame_free(&util_mvd_video_decoder_raw_image[session][i]);
	}

	util_mvd_video_decoder_available_raw_image[session] = 0;
	util_mvd_video_decoder_raw_image_ready_index[session] = 0;
	util_mvd_video_decoder_raw_image_current_index[session] = 0;
}

uint16_t DecoderMvd_get_available_raw_image_num(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return 0;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0) || !util_mvd_video_decoder_init)
		return 0;
	else
		return util_mvd_video_decoder_available_raw_image[session];
}

uint32_t DecoderMvd_get_image(uint8_t** raw_data, double* current_pos, uint32_t width, uint32_t height, uint8_t session)
{
	uint16_t buffer_num = 0;
	double framerate = 0;
	double current_frame = 0;
	double timebase = 0;

	if(!raw_data || !current_pos || width == 0 || height == 0 || session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0) || !util_mvd_video_decoder_init)
		goto not_inited;

	if(util_mvd_video_decoder_available_raw_image[session] == 0)
	{
		//DEF_LOG_STRING("No packets are available!!!!!");
		goto try_again;
	}

	free(*raw_data);
	*raw_data = NULL;
	*raw_data = (uint8_t*)linearAlloc(width * height * 4);
	if(!*raw_data)
		goto out_of_memory;

	*current_pos = 0;
	buffer_num = util_mvd_video_decoder_raw_image_ready_index[session];
	framerate = (double)DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, 0)]->avg_frame_rate.num / DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, 0)]->avg_frame_rate.den;
	if(util_mvd_video_decoder_raw_image[session][buffer_num]->duration != 0)
		current_frame = (double)util_mvd_video_decoder_raw_image[session][buffer_num]->pts / util_mvd_video_decoder_raw_image[session][buffer_num]->duration;

	timebase = av_q2d(DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, 0)]->time_base);
	if(timebase != 0)
		*current_pos = (double)util_mvd_video_decoder_raw_image[session][buffer_num]->pts * timebase * 1000;//Calc pos.
	else if(framerate != 0.0)
		*current_pos = current_frame * (1000 / framerate);//Calc frame pos.

	memcpy_asm(*raw_data, util_mvd_video_decoder_raw_image[session][buffer_num]->data[0], width * height * 4);

	buffer_num = util_mvd_video_decoder_raw_image_ready_index[session];

	if(util_mvd_video_decoder_raw_image[session][buffer_num])
	{
		free(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);//data[0] is NULL: free(NULL) is safe no-op.
		for(uint8_t i = 0; i < AV_NUM_DATA_POINTERS; i++)
			util_mvd_video_decoder_raw_image[session][buffer_num]->data[i] = NULL;
	}
	av_frame_free(&util_mvd_video_decoder_raw_image[session][buffer_num]);

	if(util_mvd_video_decoder_raw_image_ready_index[session] + 1 < util_mvd_video_decoder_max_raw_image[session])
		util_mvd_video_decoder_raw_image_ready_index[session]++;
	else
		util_mvd_video_decoder_raw_image_ready_index[session] = 0;

	LightLock_Lock(&util_mvd_video_decoder_raw_image_mutex[session]);
	util_mvd_video_decoder_available_raw_image[session]--;
	LightLock_Unlock(&util_mvd_video_decoder_raw_image_mutex[session]);
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	return DEF_ERR_TRY_AGAIN;

	out_of_memory:
	return DEF_ERR_OUT_OF_MEMORY;
}

void DecoderMvd_skip_image(double* current_pos, uint8_t session)
{
	uint16_t buffer_num = 0;
	double framerate = 0;
	double current_frame = 0;
	double timebase = 0;

	if(!current_pos || session >= DEF_DECODER_MAX_SESSIONS)
		return;

	if(!DecoderDemux_is_opened(session) || !DecoderVideoSoft_is_track_initialized(session, 0) || !util_mvd_video_decoder_init)
		return;

	if(util_mvd_video_decoder_available_raw_image[session] == 0)
		return;

	*current_pos = 0;
	buffer_num = util_mvd_video_decoder_raw_image_ready_index[session];
	framerate = (double)DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, 0)]->avg_frame_rate.num / DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, 0)]->avg_frame_rate.den;
	if(util_mvd_video_decoder_raw_image[session][buffer_num]->duration != 0)
		current_frame = (double)util_mvd_video_decoder_raw_image[session][buffer_num]->pts / util_mvd_video_decoder_raw_image[session][buffer_num]->duration;

	timebase = av_q2d(DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, 0)]->time_base);
	if(timebase != 0)
		*current_pos = (double)util_mvd_video_decoder_raw_image[session][buffer_num]->pts * timebase * 1000;//Calc pos.
	else if(framerate != 0.0)
		*current_pos = current_frame * (1000 / framerate);//Calc frame pos.

	if(util_mvd_video_decoder_raw_image[session][buffer_num])
	{
		free(util_mvd_video_decoder_raw_image[session][buffer_num]->data[0]);
		for(uint8_t i = 0; i < AV_NUM_DATA_POINTERS; i++)
			util_mvd_video_decoder_raw_image[session][buffer_num]->data[i] = NULL;
	}
	av_frame_free(&util_mvd_video_decoder_raw_image[session][buffer_num]);

	if(util_mvd_video_decoder_raw_image_ready_index[session] + 1 < util_mvd_video_decoder_max_raw_image[session])
		util_mvd_video_decoder_raw_image_ready_index[session]++;
	else
		util_mvd_video_decoder_raw_image_ready_index[session] = 0;

	LightLock_Lock(&util_mvd_video_decoder_raw_image_mutex[session]);
	util_mvd_video_decoder_available_raw_image[session]--;
	LightLock_Unlock(&util_mvd_video_decoder_raw_image_mutex[session]);
}

void DecoderMvd_exit(uint8_t session)
{
	if(!util_mvd_video_decoder_init)
		return;

	util_mvd_video_decoder_init = false;
	mvdstdExit();
	free(util_mvd_video_decoder_packet);
	util_mvd_video_decoder_packet = NULL;
	util_mvd_video_decoder_available_raw_image[session] = 0;
	util_mvd_video_decoder_raw_image_ready_index[session] = 0;
	util_mvd_video_decoder_raw_image_current_index[session] = 0;
	for(uint16_t i = 0; i < util_mvd_video_decoder_max_raw_image[session]; i++)
	{
		if(util_mvd_video_decoder_raw_image[session][i])
		{
			free(util_mvd_video_decoder_raw_image[session][i]->data[0]);
			for(uint8_t k = 0; k < AV_NUM_DATA_POINTERS; k++)
				util_mvd_video_decoder_raw_image[session][i]->data[k] = NULL;
		}
		av_frame_free(&util_mvd_video_decoder_raw_image[session][i]);
	}
}
