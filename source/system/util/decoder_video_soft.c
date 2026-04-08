#include "system/util/decoder_video_soft.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "3ds.h"

#include "system/util/converter_types.h"
#include "system/util/decoder_demux.h"
#include "system/util/decoder_types.h"
#include "system/util/err_types.h"
#include "system/util/fake_pthread.h"
#include "system/util/log.h"
#include "system/util/media_types.h"
#include "system/util/util.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"

extern void memcpy_asm(uint8_t*, uint8_t*, uint32_t);

static bool util_video_decoder_init[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static bool util_video_decoder_frame_cores[4] = { 0, };
static bool util_video_decoder_slice_cores[4] = { 0, };
static int util_video_decoder_worker_start_core = 0;
static bool util_video_decoder_changeable_buffer_size[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static bool util_video_decoder_cache_packet_ready[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static bool util_video_decoder_packet_ready[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static uint16_t util_video_decoder_available_raw_image[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static uint16_t util_video_decoder_raw_image_ready_index[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static uint16_t util_video_decoder_raw_image_current_index[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static uint16_t util_video_decoder_max_raw_image[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static LightLock util_video_decoder_raw_image_mutex[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static AVPacket* util_video_decoder_packet[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static AVPacket* util_video_decoder_cache_packet[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static AVFrame* util_video_decoder_raw_image[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS][DEF_DECODER_MAX_RAW_IMAGE] = { 0, };
static AVCodecContext* util_video_decoder_context[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };
static const AVCodec* util_video_decoder_codec[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };

//Translation table for AVPixelFormat -> Raw_pixel.
static Raw_pixel util_video_decoder_pixel_format_table[AV_PIX_FMT_NB] =
{
	RAW_PIXEL_YUV420P,
	RAW_PIXEL_YUYV422,
	RAW_PIXEL_RGB888,
	RAW_PIXEL_BGR888,
	RAW_PIXEL_YUV422P,
	RAW_PIXEL_YUV444P,
	RAW_PIXEL_YUV410P,
	RAW_PIXEL_YUV411P,
	RAW_PIXEL_GRAY8,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_YUVJ420P,
	RAW_PIXEL_YUVJ422P,
	RAW_PIXEL_YUVJ444P,
	RAW_PIXEL_UYVY422,
	RAW_PIXEL_UYYVYY411,
	RAW_PIXEL_BGR332,
	RAW_PIXEL_BGR121,
	RAW_PIXEL_BGR121_BYTE,
	RAW_PIXEL_RGB332,
	RAW_PIXEL_RGB121,
	RAW_PIXEL_RGB121_BYTE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_ARGB8888,
	RAW_PIXEL_RGBA8888,
	RAW_PIXEL_ABGR8888,
	RAW_PIXEL_BGRA8888,
	RAW_PIXEL_GRAY16BE,
	RAW_PIXEL_GRAY16LE,
	RAW_PIXEL_YUV440P,
	RAW_PIXEL_YUVJ440P,
	RAW_PIXEL_YUVA420P,
	RAW_PIXEL_RGB161616BE,
	RAW_PIXEL_RGB161616LE,
	RAW_PIXEL_RGB565BE,
	RAW_PIXEL_RGB565LE,
	RAW_PIXEL_RGB555BE,
	RAW_PIXEL_RGB555LE,
	RAW_PIXEL_BGR565BE,
	RAW_PIXEL_BGR565LE,
	RAW_PIXEL_BGR555BE,
	RAW_PIXEL_BGR555LE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_YUV420P16LE,
	RAW_PIXEL_YUV420P16BE,
	RAW_PIXEL_YUV422P16LE,
	RAW_PIXEL_YUV422P16BE,
	RAW_PIXEL_YUV444P16LE,
	RAW_PIXEL_YUV444P16BE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_RGB444LE,
	RAW_PIXEL_RGB444BE,
	RAW_PIXEL_BGR444LE,
	RAW_PIXEL_BGR444BE,
	RAW_PIXEL_GRAYALPHA88,
	RAW_PIXEL_BGR161616BE,
	RAW_PIXEL_BGR161616LE,
	RAW_PIXEL_YUV420P9BE,
	RAW_PIXEL_YUV420P9LE,
	RAW_PIXEL_YUV420P10BE,
	RAW_PIXEL_YUV420P10LE,
	RAW_PIXEL_YUV422P10BE,
	RAW_PIXEL_YUV422P10LE,
	RAW_PIXEL_YUV444P9BE,
	RAW_PIXEL_YUV444P9LE,
	RAW_PIXEL_YUV444P10BE,
	RAW_PIXEL_YUV444P10LE,
	RAW_PIXEL_YUV422P9BE,
	RAW_PIXEL_YUV422P9LE,
	RAW_PIXEL_GBR888P,
	RAW_PIXEL_GBR999PBE,
	RAW_PIXEL_GBR999PLE,
	RAW_PIXEL_GBR101010PBE,
	RAW_PIXEL_GBR101010PLE,
	RAW_PIXEL_GBR161616PBE,
	RAW_PIXEL_GBR161616PLE,
	RAW_PIXEL_YUVA422P,
	RAW_PIXEL_YUVA444P,
	RAW_PIXEL_YUVA420P9BE,
	RAW_PIXEL_YUVA420P9LE,
	RAW_PIXEL_YUVA422P9BE,
	RAW_PIXEL_YUVA422P9LE,
	RAW_PIXEL_YUVA444P9BE,
	RAW_PIXEL_YUVA444P9LE,
	RAW_PIXEL_YUVA420P10BE,
	RAW_PIXEL_YUVA420P10LE,
	RAW_PIXEL_YUVA422P10BE,
	RAW_PIXEL_YUVA422P10LE,
	RAW_PIXEL_YUVA444P10BE,
	RAW_PIXEL_YUVA444P10LE,
	RAW_PIXEL_YUVA420P16BE,
	RAW_PIXEL_YUVA420P16LE,
	RAW_PIXEL_YUVA422P16BE,
	RAW_PIXEL_YUVA422P16LE,
	RAW_PIXEL_YUVA444P16BE,
	RAW_PIXEL_YUVA444P16LE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_RGBA16161616BE,
	RAW_PIXEL_RGBA16161616LE,
	RAW_PIXEL_BGRA16161616BE,
	RAW_PIXEL_BGRA16161616LE,
	RAW_PIXEL_YVYU422,
	RAW_PIXEL_GRAYALPHA1616BE,
	RAW_PIXEL_GRAYALPHA1616LE,
	RAW_PIXEL_GBRA8888P,
	RAW_PIXEL_GBRA16161616PBE,
	RAW_PIXEL_GBRA16161616PLE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_YUV420P12BE,
	RAW_PIXEL_YUV420P12LE,
	RAW_PIXEL_YUV420P14BE,
	RAW_PIXEL_YUV420P14LE,
	RAW_PIXEL_YUV422P12BE,
	RAW_PIXEL_YUV422P12LE,
	RAW_PIXEL_YUV422P14BE,
	RAW_PIXEL_YUV422P14LE,
	RAW_PIXEL_YUV444P12BE,
	RAW_PIXEL_YUV444P12LE,
	RAW_PIXEL_YUV444P14BE,
	RAW_PIXEL_YUV444P14LE,
	RAW_PIXEL_GBR121212PBE,
	RAW_PIXEL_GBR121212PLE,
	RAW_PIXEL_GBR141414PBE,
	RAW_PIXEL_GBR141414PLE,
	RAW_PIXEL_YUVJ411P,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_YUV440P10LE,
	RAW_PIXEL_YUV440P10BE,
	RAW_PIXEL_YUV440P12LE,
	RAW_PIXEL_YUV440P12BE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_GBRA12121212PBE,
	RAW_PIXEL_GBRA12121212PLE,
	RAW_PIXEL_GBRA10101010PBE,
	RAW_PIXEL_GBRA10101010PLE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_GRAY12BE,
	RAW_PIXEL_GRAY12LE,
	RAW_PIXEL_GRAY10BE,
	RAW_PIXEL_GRAY10LE,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
	RAW_PIXEL_INVALID,
};

static void dvs_frame_buffer_free(void *opaque, uint8_t *data)
{
	(void)opaque;
	free(data);
}

//We can't get rid of this "int" because library uses "int" type as args.
static int dvs_video_allocate_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
	(void)flags;

	if(avctx->codec_type == AVMEDIA_TYPE_VIDEO)
	{
		uint32_t width = 0;
		uint32_t height = 0;
		int32_t buffer_size = 0;
		uint8_t* buffer = NULL;

		for (uint8_t i = 0; i < AV_NUM_DATA_POINTERS; i++)
		{
			frame->data[i] = NULL;
			frame->linesize[i] = 0;
			frame->buf[i] = NULL;
		}
		width = frame->width;
		height = frame->height;

		if(avctx->codec_id == AV_CODEC_ID_AV1)
		{
			if(width % 128 != 0)
				width += 128 - width % 128;
			if(height % 128 != 0)
				height += 128 - height % 128;
		}
		else
		{
			if(width % 16 != 0)
				width += 16 - width % 16;
			if(height % 16 != 0)
				height += 16 - height % 16;
		}

		buffer_size = av_image_get_buffer_size(avctx->pix_fmt, width, height, 1);
		if(buffer_size <= 0)
			return -1;

		buffer = (uint8_t*)linearAlloc(buffer_size);
		if(!buffer)
			return -1;

		if(av_image_fill_arrays(frame->data, frame->linesize, buffer, avctx->pix_fmt, width, height, 1) <= 0)
		{
			free(buffer);
			buffer = NULL;
			return -1;
		}

		for(uint8_t i = 0; i < AV_NUM_DATA_POINTERS; i++)
		{
			if(frame->data[i])
			{
				frame->buf[i] = av_buffer_create(frame->data[i], 0, dvs_frame_buffer_free, NULL, 0);
				if(!frame->buf[i])
				{
					for(uint8_t k = 0; k < AV_NUM_DATA_POINTERS; k++)
						frame->buf[k] = NULL;

					free(buffer);
					buffer = NULL;
					return -1;
				}
			}
		}

		return 0;
	}
	else
		return -1;
}

void DecoderVideoSoft_set_enabled_cores(const bool frame_threading_cores[4], const bool slice_threading_cores[4], int worker_start_core)
{
	if(!frame_threading_cores[0] && !frame_threading_cores[1] && !frame_threading_cores[2] && !frame_threading_cores[3]
	&& !slice_threading_cores[0] && !slice_threading_cores[1] && !slice_threading_cores[2] && !slice_threading_cores[3])
		return;

	for(uint8_t i = 0; i < 4; i++)
	{
		util_video_decoder_frame_cores[i] = frame_threading_cores[i];
		util_video_decoder_slice_cores[i] = slice_threading_cores[i];
	}
	util_video_decoder_worker_start_core = worker_start_core;
}

uint32_t DecoderVideoSoft_init(uint8_t low_resolution, uint8_t num_of_video_tracks, uint8_t num_of_threads, Media_thread_type thread_type, uint8_t session)
{
	int32_t ffmpeg_result = 0;

	if(num_of_video_tracks == 0 || thread_type <= MEDIA_THREAD_TYPE_INVALID || thread_type >= MEDIA_THREAD_TYPE_MAX
	|| num_of_video_tracks > DEF_DECODER_MAX_VIDEO_TRACKS || session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session))
		goto not_inited;

	for(uint8_t i = 0; i < DEF_DECODER_MAX_VIDEO_TRACKS; i++)
	{
		if(util_video_decoder_init[session][i])
			goto already_inited;
	}

	for(uint8_t i = 0; i < num_of_video_tracks; i++)
	{
		if(DecoderDemux_video_stream_num(session, i) == UINT8_MAX)
			goto invalid_arg;
	}

	for (uint8_t i = 0; i < num_of_video_tracks; i++)
	{
		util_video_decoder_raw_image_current_index[session][i] = 0;
		util_video_decoder_raw_image_ready_index[session][i] = 0;
		util_video_decoder_available_raw_image[session][i] = 0;
		util_video_decoder_max_raw_image[session][i] = 0;
	}

	for(uint8_t i = 0; i < num_of_video_tracks; i++)
	{
		util_video_decoder_codec[session][i] = avcodec_find_decoder(DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, i)]->codecpar->codec_id);
		if(!util_video_decoder_codec[session][i])
		{
			DEF_LOG_RESULT(avcodec_find_decoder, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
			goto ffmpeg_api_failed;
		}

		util_video_decoder_context[session][i] = avcodec_alloc_context3(util_video_decoder_codec[session][i]);
		if(!util_video_decoder_context[session][i])
		{
			DEF_LOG_RESULT(avcodec_alloc_context3, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
			goto ffmpeg_api_failed;
		}

		ffmpeg_result = avcodec_parameters_to_context(util_video_decoder_context[session][i], DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, i)]->codecpar);
		if(ffmpeg_result != 0)
		{
			DEF_LOG_RESULT(avcodec_parameters_to_context, false, ffmpeg_result);
			goto ffmpeg_api_failed;
		}

		if(util_video_decoder_codec[session][i]->max_lowres < low_resolution)
			util_video_decoder_context[session][i]->lowres = util_video_decoder_codec[session][i]->max_lowres;
		else
			util_video_decoder_context[session][i]->lowres = low_resolution;

		util_video_decoder_context[session][i]->flags = AV_CODEC_FLAG_OUTPUT_CORRUPT;

		//If there is more than 1 video tracks, limit number of threads to avoid poor performance due to too many threads.
		if(num_of_video_tracks >= 2)
			util_video_decoder_context[session][i]->thread_count = (num_of_threads > 4 ? 4 : num_of_threads);
		else
			util_video_decoder_context[session][i]->thread_count = num_of_threads;

		if(thread_type == MEDIA_THREAD_TYPE_AUTO)
		{
			if(util_video_decoder_codec[session][i]->capabilities & AV_CODEC_CAP_FRAME_THREADS)
				util_video_decoder_context[session][i]->thread_type = FF_THREAD_FRAME;
			else if(util_video_decoder_codec[session][i]->capabilities & AV_CODEC_CAP_SLICE_THREADS)
				util_video_decoder_context[session][i]->thread_type = FF_THREAD_SLICE;
			else
				util_video_decoder_context[session][i]->thread_type = FF_THREAD_FRAME;
		}
		else if(thread_type == MEDIA_THREAD_TYPE_SLICE && util_video_decoder_codec[session][i]->capabilities & AV_CODEC_CAP_SLICE_THREADS)
			util_video_decoder_context[session][i]->thread_type = FF_THREAD_SLICE;
		else if(thread_type == MEDIA_THREAD_TYPE_FRAME && util_video_decoder_codec[session][i]->capabilities & AV_CODEC_CAP_FRAME_THREADS)
			util_video_decoder_context[session][i]->thread_type = FF_THREAD_FRAME;
		else
		{
			util_video_decoder_context[session][i]->thread_type = 0;
			util_video_decoder_context[session][i]->thread_count = 1;
		}


		util_video_decoder_context[session][i]->thread_type = FF_THREAD_SLICE;

		if(util_video_decoder_context[session][i]->thread_type == FF_THREAD_FRAME)
			Util_fake_pthread_set_enabled_core(util_video_decoder_frame_cores, util_video_decoder_worker_start_core);
		else if(util_video_decoder_context[session][i]->thread_type == FF_THREAD_SLICE)
			Util_fake_pthread_set_enabled_core(util_video_decoder_slice_cores, util_video_decoder_worker_start_core);

		util_video_decoder_context[session][i]->get_buffer2 = dvs_video_allocate_buffer;

		ffmpeg_result = avcodec_open2(util_video_decoder_context[session][i], util_video_decoder_codec[session][i], NULL);
		if(ffmpeg_result != 0)
		{
			DEF_LOG_RESULT(avcodec_open2, false, ffmpeg_result);
			goto ffmpeg_api_failed;
		}

		LightLock_Init(&util_video_decoder_raw_image_mutex[session][i]);

		util_video_decoder_max_raw_image[session][i] = DEF_DECODER_MAX_RAW_IMAGE;
		util_video_decoder_changeable_buffer_size[session][i] = true;
		util_video_decoder_init[session][i] = true;
	}

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	already_inited:
	return DEF_ERR_ALREADY_INITIALIZED;

	ffmpeg_api_failed:
	for(uint8_t i = 0; i < DEF_DECODER_MAX_VIDEO_TRACKS; i++)
	{
		util_video_decoder_init[session][i] = false;
		avcodec_free_context(&util_video_decoder_context[session][i]);
	}

	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}

void DecoderVideoSoft_get_info(Media_v_info* video_info, uint8_t video_index, uint8_t session)
{
	uint16_t multiple_of = 0;
	uint32_t size = 0;
	AVRational sar = { 0, };

	if(!video_info || video_index >= DEF_DECODER_MAX_VIDEO_TRACKS || session >= DEF_DECODER_MAX_SESSIONS)
		return;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][video_index])
		return;

	sar = av_guess_sample_aspect_ratio(DecoderDemux_format_context(session), DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, video_index)], NULL);
	if(sar.num == 0)
	{
		video_info->sar_width = 1;
		video_info->sar_height = 1;
	}
	else
	{
		video_info->sar_width = 1;
		video_info->sar_height = (double)sar.den / sar.num;
	}

	video_info->width = util_video_decoder_context[session][video_index]->width;
	video_info->height = util_video_decoder_context[session][video_index]->height;

	if(util_video_decoder_context[session][video_index]->codec_id == AV_CODEC_ID_AV1)
		multiple_of = 128;//AV1 buffer size is multiple of 128.
	else
		multiple_of = 16;//Other codecs are multiple of 16.

	if(video_info->width % multiple_of != 0)
		video_info->codec_width = video_info->width + (multiple_of - (video_info->width % multiple_of));
	else
		video_info->codec_width = video_info->width;

	if(video_info->height % multiple_of != 0)
		video_info->codec_height = video_info->height + (multiple_of - (video_info->height % multiple_of));
	else
		video_info->codec_height = video_info->height;

	if(DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, video_index)]->avg_frame_rate.den == 0)
		video_info->framerate = 0;
	else
		video_info->framerate = (double)DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, video_index)]->avg_frame_rate.num / DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, video_index)]->avg_frame_rate.den;

	video_info->duration = (double)DecoderDemux_format_context(session)->duration / AV_TIME_BASE;
	if(util_video_decoder_context[session][video_index]->thread_type == FF_THREAD_FRAME)
		video_info->thread_type = MEDIA_THREAD_TYPE_FRAME;
	else if(util_video_decoder_context[session][video_index]->thread_type == FF_THREAD_SLICE)
		video_info->thread_type = MEDIA_THREAD_TYPE_SLICE;
	else
		video_info->thread_type = MEDIA_THREAD_TYPE_NONE;

	if(util_video_decoder_context[session][video_index]->pix_fmt < 0 || util_video_decoder_context[session][video_index]->pix_fmt >= AV_PIX_FMT_NB)
		video_info->pixel_format = RAW_PIXEL_INVALID;
	else
		video_info->pixel_format = util_video_decoder_pixel_format_table[util_video_decoder_context[session][video_index]->pix_fmt];

	size = (util_video_decoder_codec[session][video_index]->long_name ? strlen(util_video_decoder_codec[session][video_index]->long_name) : 0);
	if(size > 0)
	{
		size = Util_min(size, (sizeof(video_info->format_name) - 1));
		memcpy(video_info->format_name, util_video_decoder_codec[session][video_index]->long_name, size);
	}
	video_info->format_name[size] = 0x00;

	size = (util_video_decoder_codec[session][video_index]->name ? strlen(util_video_decoder_codec[session][video_index]->name) : 0);
	if(size > 0)
	{
		size = Util_min(size, (sizeof(video_info->short_format_name) - 1));
		memcpy(video_info->short_format_name, util_video_decoder_codec[session][video_index]->name, size);
	}
	video_info->short_format_name[size] = 0x00;

	video_info->is_av1_codec = (util_video_decoder_context[session][video_index]->codec_id == AV_CODEC_ID_AV1);
}

uint32_t DecoderVideoSoft_route_from_demux(uint8_t session, uint8_t track, AVPacket* front_pkt)
{
	int32_t ffmpeg_result = 0;

	util_video_decoder_cache_packet[session][track] = av_packet_alloc();
	if(!util_video_decoder_cache_packet[session][track])
	{
		DEF_LOG_RESULT(av_packet_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	}

	av_packet_unref(util_video_decoder_cache_packet[session][track]);
	ffmpeg_result = av_packet_ref(util_video_decoder_cache_packet[session][track], front_pkt);
	if(ffmpeg_result != 0)
	{
		av_packet_free(&util_video_decoder_cache_packet[session][track]);
		DEF_LOG_RESULT(av_packet_ref, false, ffmpeg_result);
		return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	}

	util_video_decoder_cache_packet_ready[session][track] = true;
	return DEF_SUCCESS;
}

uint32_t DecoderVideoSoft_ready_packet(int8_t packet_index, int8_t session)
{
	int32_t ffmpeg_result = 0;

	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index])
		goto not_inited;

	if(!util_video_decoder_cache_packet_ready[session][packet_index])
	{
		//DEF_LOG_STRING("No packets are available!!!!!");
		goto try_again;
	}

	if(util_video_decoder_packet_ready[session][packet_index])
	{
		//DEF_LOG_STRING("Queues are full!!!!!");
		goto try_again;
	}

	av_packet_free(&util_video_decoder_packet[session][packet_index]);
	util_video_decoder_packet[session][packet_index] = av_packet_alloc();
	if(!util_video_decoder_packet[session][packet_index])
	{
		DEF_LOG_RESULT(av_packet_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		goto ffmpeg_api_failed;
	}

	av_packet_unref(util_video_decoder_packet[session][packet_index]);
	ffmpeg_result = av_packet_ref(util_video_decoder_packet[session][packet_index], util_video_decoder_cache_packet[session][packet_index]);
	if(ffmpeg_result != 0)
	{
		DEF_LOG_RESULT(av_packet_ref, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	util_video_decoder_cache_packet_ready[session][packet_index] = false;
	util_video_decoder_packet_ready[session][packet_index] = true;
	av_packet_free(&util_video_decoder_cache_packet[session][packet_index]);
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	return DEF_ERR_TRY_AGAIN;

	ffmpeg_api_failed:
	av_packet_free(&util_video_decoder_packet[session][packet_index]);
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}

void DecoderVideoSoft_skip_packet(uint8_t packet_index, uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return;

	if(!DecoderDemux_is_opened(session))
		return;

	if(!util_video_decoder_cache_packet_ready[session][packet_index])
		return;

	av_packet_free(&util_video_decoder_cache_packet[session][packet_index]);
	util_video_decoder_cache_packet_ready[session][packet_index] = false;
}

void DecoderVideoSoft_set_raw_image_buffer_size(uint32_t max_num_of_buffer, uint8_t packet_index, uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS
	|| max_num_of_buffer < 3 || max_num_of_buffer > DEF_DECODER_MAX_RAW_IMAGE)
		return;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index]
	|| !util_video_decoder_changeable_buffer_size[session][packet_index])
		return;

	util_video_decoder_max_raw_image[session][packet_index] = max_num_of_buffer;
}

uint32_t DecoderVideoSoft_get_raw_image_buffer_size(uint8_t packet_index, uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return 0;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index])
		return 0;

	return util_video_decoder_max_raw_image[session][packet_index];
}

uint32_t DecoderVideoSoft_decode(uint8_t packet_index, uint8_t session)
{
	int32_t send_ffmpeg_result = 0;
	int32_t receive_ffmpeg_result = 0;
	uint16_t buffer_num = 0;

	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index])
		goto not_inited;

	if(!util_video_decoder_packet_ready[session][packet_index])
	{
		//DEF_LOG_STRING("No packets are available!!!!!");
		goto try_again;
	}

	util_video_decoder_changeable_buffer_size[session][packet_index] = false;
	if(util_video_decoder_available_raw_image[session][packet_index] + 1 >= util_video_decoder_max_raw_image[session][packet_index])
	{
		//DEF_LOG_STRING("Queues are full!!!!!");
		goto try_again;
	}

	buffer_num = util_video_decoder_raw_image_current_index[session][packet_index];

	util_video_decoder_raw_image[session][packet_index][buffer_num] = av_frame_alloc();
	if(!util_video_decoder_raw_image[session][packet_index][buffer_num])
	{
		DEF_LOG_RESULT(av_frame_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		goto ffmpeg_api_failed;
	}

	send_ffmpeg_result = avcodec_send_packet(util_video_decoder_context[session][packet_index], util_video_decoder_packet[session][packet_index]);
	//Some decoders (such as av1 and vp9) may return EAGAIN if so, ignore it and call avcodec_receive_frame().
	if(send_ffmpeg_result != 0 && send_ffmpeg_result != AVERROR(EAGAIN))
	{
		DEF_LOG_RESULT(avcodec_send_packet, false, send_ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	receive_ffmpeg_result = avcodec_receive_frame(util_video_decoder_context[session][packet_index], util_video_decoder_raw_image[session][packet_index][buffer_num]);
	if(receive_ffmpeg_result != 0)
	{
		if(send_ffmpeg_result == AVERROR(EAGAIN))
		{
			//Decoder has output pending but didn't accept the new packet yet.
			//Packet is NOT consumed; call receive first, then retry send.
			goto try_again_no_output;
		}
		else if(receive_ffmpeg_result == AVERROR(EAGAIN))
		{
			//Packet was accepted (send returned 0) but the frame-threading pipeline is
			//still filling up.  Packet IS consumed; signal caller to feed the next packet.
			goto need_more_input;
		}
		else
		{
			DEF_LOG_RESULT(avcodec_receive_frame, false, receive_ffmpeg_result);
			goto ffmpeg_api_failed;
		}
	}

	if(buffer_num + 1 < util_video_decoder_max_raw_image[session][packet_index])
		util_video_decoder_raw_image_current_index[session][packet_index]++;
	else
		util_video_decoder_raw_image_current_index[session][packet_index] = 0;

	LightLock_Lock(&util_video_decoder_raw_image_mutex[session][packet_index]);
	util_video_decoder_available_raw_image[session][packet_index]++;
	LightLock_Unlock(&util_video_decoder_raw_image_mutex[session][packet_index]);

	if(send_ffmpeg_result == AVERROR(EAGAIN))
		goto try_again_with_output;

	util_video_decoder_packet_ready[session][packet_index] = false;
	av_packet_free(&util_video_decoder_packet[session][packet_index]);
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	return DEF_ERR_TRY_AGAIN;

	need_more_input:
	//Packet was consumed but no frame yet (frame-thread pipeline still filling up).
	av_frame_free(&util_video_decoder_raw_image[session][packet_index][buffer_num]);
	util_video_decoder_packet_ready[session][packet_index] = false;
	av_packet_free(&util_video_decoder_packet[session][packet_index]);
	return DEF_ERR_NEED_MORE_INPUT;

	try_again_no_output:
	av_frame_free(&util_video_decoder_raw_image[session][packet_index][buffer_num]);
	return DEF_ERR_DECODER_TRY_AGAIN_NO_OUTPUT;

	try_again_with_output:
	return DEF_ERR_DECODER_TRY_AGAIN;

	ffmpeg_api_failed:
	util_video_decoder_packet_ready[session][packet_index] = false;
	av_packet_free(&util_video_decoder_packet[session][packet_index]);
	av_frame_free(&util_video_decoder_raw_image[session][packet_index][buffer_num]);
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}

void DecoderVideoSoft_clear_raw_image(uint8_t packet_index, uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index])
		return;

	for(uint16_t k = 0; k < util_video_decoder_max_raw_image[session][packet_index]; k++)
		av_frame_free(&util_video_decoder_raw_image[session][packet_index][k]);

	util_video_decoder_available_raw_image[session][packet_index] = 0;
	util_video_decoder_raw_image_ready_index[session][packet_index] = 0;
	util_video_decoder_raw_image_current_index[session][packet_index] = 0;
}

uint16_t DecoderVideoSoft_get_available_raw_image_num(uint8_t packet_index, uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return 0;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index])
		return 0;
	else
		return util_video_decoder_available_raw_image[session][packet_index];
}

uint32_t DecoderVideoSoft_get_image(uint8_t** raw_data, double* current_pos, uint32_t width, uint32_t height, uint8_t packet_index, uint8_t session)
{
	bool is_linear = true;
	bool av1_cpu_copy = false;
	uint32_t cpy_size = 0;
	uint16_t buffer_num = 0;
	uint32_t y_offset = 0;
	uint32_t u_offset = 0;
	uint32_t v_offset = 0;
	uint32_t y_size = width * height;
	uint32_t uv_size = width * height / 4;
	double framerate = 0;
	double current_frame = 0;
	double timebase = 0;

#if DEF_DECODER_DMA_ENABLE
	uint32_t dma_result[3] = { 0, 0, 0, };
	Handle dma_handle[3] = { 0, 0, 0, };
	DmaConfig dma_config;
#endif //DEF_DECODER_DMA_ENABLE

	if(!raw_data || !current_pos || width == 0 || height == 0
	|| packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS || session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index])
		goto not_inited;

	if(util_video_decoder_available_raw_image[session][packet_index] == 0)
	{
		//DEF_LOG_STRING("No packets are available!!!!!");
		goto try_again;
	}

	cpy_size = av_image_get_buffer_size(util_video_decoder_context[session][packet_index]->pix_fmt, width, height, 1);
	*current_pos = 0;
	free(*raw_data);
	*raw_data = NULL;
	*raw_data = (uint8_t*)linearAlloc(cpy_size);
	if(!*raw_data)
		goto out_of_memory;

	buffer_num = util_video_decoder_raw_image_ready_index[session][packet_index];
	framerate = (double)DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, packet_index)]->avg_frame_rate.num / DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, packet_index)]->avg_frame_rate.den;
	if(util_video_decoder_raw_image[session][packet_index][buffer_num]->duration != 0)
		current_frame = (double)util_video_decoder_raw_image[session][packet_index][buffer_num]->pts / util_video_decoder_raw_image[session][packet_index][buffer_num]->duration;

	timebase = av_q2d(DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, packet_index)]->time_base);
	if(timebase != 0)
	{
		if(util_video_decoder_raw_image[session][packet_index][buffer_num]->pts == AV_NOPTS_VALUE)//If pts is not available, use dts instead.
			*current_pos = (double)util_video_decoder_raw_image[session][packet_index][buffer_num]->pkt_dts * timebase * 1000;//Calc pos.
		else
			*current_pos = (double)util_video_decoder_raw_image[session][packet_index][buffer_num]->pts * timebase * 1000;//Calc pos.
	}
	else if(framerate != 0.0)
		*current_pos = current_frame * (1000 / framerate);//Calc frame pos.

	y_offset = (uint32_t)util_video_decoder_raw_image[session][packet_index][buffer_num]->data[0];
	u_offset = (uint32_t)util_video_decoder_raw_image[session][packet_index][buffer_num]->data[1];
	v_offset = (uint32_t)util_video_decoder_raw_image[session][packet_index][buffer_num]->data[2];

	//Check if the decoded data is in linear format (some decoder return in not linear format).
	//Currently, only check for YUV420P because it only occurs in h263p afaik.
	if(util_video_decoder_context[session][packet_index]->pix_fmt == AV_PIX_FMT_YUV420P
	&& (y_offset + y_size != u_offset || u_offset + uv_size != v_offset))
		is_linear = false;

	av1_cpu_copy = (util_video_decoder_context[session][packet_index]->codec_id == AV_CODEC_ID_AV1);

	if(av1_cpu_copy)
	{
		/* AV1 dedicated path: always use CPU memcpy to avoid DMA instability. */
		if(is_linear)
			memcpy_asm(*raw_data, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[0], cpy_size);
		else
		{
			memcpy_asm(*raw_data, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[0], y_size);
			memcpy_asm((*raw_data) + y_size, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[1], uv_size);
			memcpy_asm((*raw_data) + y_size + uv_size, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[2], uv_size);
		}
	}
	else
	{
#if DEF_DECODER_DMA_ENABLE
	if(is_linear)
	{
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, y_offset, cpy_size);

		dmaConfigInitDefault(&dma_config);
		dma_result[0] = svcStartInterProcessDma(&dma_handle[0], CUR_PROCESS_HANDLE, (uint32_t)*raw_data, CUR_PROCESS_HANDLE, (uint32_t)util_video_decoder_raw_image[session][packet_index][buffer_num]->data[0], cpy_size, &dma_config);

		if(dma_result[0] == DEF_SUCCESS)
			svcWaitSynchronization(dma_handle[0], INT64_MAX);

		svcCloseHandle(dma_handle[0]);

		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (uint32_t)*raw_data, cpy_size);
	}
	else
	{
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, y_offset, y_size);
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, u_offset, uv_size);
		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, v_offset, uv_size);

		dmaConfigInitDefault(&dma_config);
		dma_result[0] = svcStartInterProcessDma(&dma_handle[0], CUR_PROCESS_HANDLE, (uint32_t)*raw_data, CUR_PROCESS_HANDLE, (uint32_t)util_video_decoder_raw_image[session][packet_index][buffer_num]->data[0], y_size, &dma_config);
		dma_result[1] = svcStartInterProcessDma(&dma_handle[1], CUR_PROCESS_HANDLE, (uint32_t)(*raw_data) + y_size, CUR_PROCESS_HANDLE, (uint32_t)util_video_decoder_raw_image[session][packet_index][buffer_num]->data[1], uv_size, &dma_config);
		dma_result[2] = svcStartInterProcessDma(&dma_handle[2], CUR_PROCESS_HANDLE, (uint32_t)(*raw_data) + y_size + uv_size, CUR_PROCESS_HANDLE, (uint32_t)util_video_decoder_raw_image[session][packet_index][buffer_num]->data[2], uv_size, &dma_config);

		for(uint8_t i = 0; i < 3; i++)
		{
			if(dma_result[i] == DEF_SUCCESS)
				svcWaitSynchronization(dma_handle[i], INT64_MAX);

			svcCloseHandle(dma_handle[i]);
		}

		svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (uint32_t)*raw_data, cpy_size);
	}
#else
	if(is_linear)
		memcpy_asm(*raw_data, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[0], cpy_size);
	else
	{
		memcpy_asm(*raw_data, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[0], y_size);
		memcpy_asm((*raw_data) + y_size, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[1], uv_size);
		memcpy_asm((*raw_data) + y_size + uv_size, util_video_decoder_raw_image[session][packet_index][buffer_num]->data[2], uv_size);
	}
#endif //DEF_DECODER_DMA_ENABLE
	}

	av_frame_free(&util_video_decoder_raw_image[session][packet_index][buffer_num]);

	if(util_video_decoder_raw_image_ready_index[session][packet_index] + 1 < util_video_decoder_max_raw_image[session][packet_index])
		util_video_decoder_raw_image_ready_index[session][packet_index]++;
	else
		util_video_decoder_raw_image_ready_index[session][packet_index] = 0;

	LightLock_Lock(&util_video_decoder_raw_image_mutex[session][packet_index]);
	util_video_decoder_available_raw_image[session][packet_index]--;
	LightLock_Unlock(&util_video_decoder_raw_image_mutex[session][packet_index]);
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

void DecoderVideoSoft_skip_image(double* current_pos, uint8_t packet_index, uint8_t session)
{
	uint16_t buffer_num = 0;
	double framerate = 0;
	double current_frame = 0;
	double timebase = 0;

	if(!current_pos || packet_index >= DEF_DECODER_MAX_VIDEO_TRACKS || session >= DEF_DECODER_MAX_SESSIONS)
		return;

	if(!DecoderDemux_is_opened(session) || !util_video_decoder_init[session][packet_index])
		return;

	if(util_video_decoder_available_raw_image[session][packet_index] == 0)
		return;

	*current_pos = 0;
	buffer_num = util_video_decoder_raw_image_ready_index[session][packet_index];
	framerate = (double)DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, packet_index)]->avg_frame_rate.num / DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, packet_index)]->avg_frame_rate.den;
	if(util_video_decoder_raw_image[session][packet_index][buffer_num]->duration != 0)
		current_frame = (double)util_video_decoder_raw_image[session][packet_index][buffer_num]->pts / util_video_decoder_raw_image[session][packet_index][buffer_num]->duration;

	timebase = av_q2d(DecoderDemux_format_context(session)->streams[DecoderDemux_video_stream_num(session, packet_index)]->time_base);
	if(timebase != 0)
	{
		if(util_video_decoder_raw_image[session][packet_index][buffer_num]->pts == AV_NOPTS_VALUE)//If pts is not available, use dts instead.
			*current_pos = (double)util_video_decoder_raw_image[session][packet_index][buffer_num]->pkt_dts * timebase * 1000;//Calc pos.
		else
			*current_pos = (double)util_video_decoder_raw_image[session][packet_index][buffer_num]->pts * timebase * 1000;//Calc pos.
	}
	else if(framerate != 0.0)
		*current_pos = current_frame * (1000 / framerate);//Calc frame pos.

	av_frame_free(&util_video_decoder_raw_image[session][packet_index][buffer_num]);

	if(util_video_decoder_raw_image_ready_index[session][packet_index] + 1 < util_video_decoder_max_raw_image[session][packet_index])
		util_video_decoder_raw_image_ready_index[session][packet_index]++;
	else
		util_video_decoder_raw_image_ready_index[session][packet_index] = 0;

	LightLock_Lock(&util_video_decoder_raw_image_mutex[session][packet_index]);
	util_video_decoder_available_raw_image[session][packet_index]--;
	LightLock_Unlock(&util_video_decoder_raw_image_mutex[session][packet_index]);
}

void DecoderVideoSoft_exit(uint8_t session)
{
	for(uint8_t i = 0; i < DEF_DECODER_MAX_VIDEO_TRACKS; i++)
	{
		if(util_video_decoder_init[session][i])
		{
			util_video_decoder_init[session][i] = false;
			util_video_decoder_cache_packet_ready[session][i] = false;
			util_video_decoder_packet_ready[session][i] = false;
			avcodec_free_context(&util_video_decoder_context[session][i]);
			av_packet_free(&util_video_decoder_packet[session][i]);
			av_packet_free(&util_video_decoder_cache_packet[session][i]);
			util_video_decoder_available_raw_image[session][i] = 0;
			util_video_decoder_raw_image_ready_index[session][i] = 0;
			util_video_decoder_raw_image_current_index[session][i] = 0;
			for(uint16_t k = 0; k < util_video_decoder_max_raw_image[session][i]; k++)
				av_frame_free(&util_video_decoder_raw_image[session][i][k]);
		}
	}
}

bool DecoderVideoSoft_is_track_initialized(uint8_t session, uint8_t track)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || track >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return false;
	return util_video_decoder_init[session][track];
}

AVCodecContext* DecoderVideoSoft_codec_context(uint8_t session, uint8_t track)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || track >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return NULL;
	return util_video_decoder_context[session][track];
}

AVPacket* DecoderVideoSoft_packet(uint8_t session, uint8_t track)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || track >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return NULL;
	return util_video_decoder_packet[session][track];
}

bool DecoderVideoSoft_is_packet_ready(uint8_t session, uint8_t track)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || track >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return false;
	return util_video_decoder_packet_ready[session][track];
}

void DecoderVideoSoft_release_ready_packet(uint8_t session, uint8_t track)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || track >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return;
	util_video_decoder_packet_ready[session][track] = false;
	av_packet_free(&util_video_decoder_packet[session][track]);
}
