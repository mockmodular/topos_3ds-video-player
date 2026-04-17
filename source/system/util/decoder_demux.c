#include "system/util/decoder_demux.h"

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "system/util/err_types.h"
#include "system/util/log.h"
#include "system/util/media_types.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"

#include "3ds.h"

/* Wall-clock cap for avformat_seek_file (FFmpeg interrupt_callback). */
#ifndef DEF_DEMUX_SEEK_INTERRUPT_MS
#define DEF_DEMUX_SEEK_INTERRUPT_MS ((uint64_t)15000)
#endif

static bool dd_opened_file[DEF_DECODER_MAX_SESSIONS] = { 0, };
static uint8_t dd_audio_stream_num[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static uint8_t dd_video_stream_num[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_VIDEO_TRACKS] = { 0, };

static uint16_t dd_available_cache_packet[DEF_DECODER_MAX_SESSIONS] = { 0, };
static uint16_t dd_cache_packet_ready_index[DEF_DECODER_MAX_SESSIONS] = { 0, };
static uint16_t dd_cache_packet_current_index[DEF_DECODER_MAX_SESSIONS] = { 0, };
static LightLock dd_cache_packet_mutex[DEF_DECODER_MAX_SESSIONS] = { 0, };
static AVPacket* dd_cache_packet[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_CACHE_PACKETS] = { 0, };
static AVFormatContext* dd_format_context[DEF_DECODER_MAX_SESSIONS] = { 0, };

bool DecoderDemux_is_opened(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return false;
	return dd_opened_file[session];
}

void DecoderDemux_set_opened(uint8_t session, bool opened)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return;
	dd_opened_file[session] = opened;
}

uint32_t DecoderDemux_open(const char* path, uint8_t* num_of_audio_tracks, uint8_t* num_of_video_tracks, uint8_t session)
{
	int32_t ffmpeg_result = 0;
	uint8_t audio_index = 0;
	uint8_t video_index = 0;

	if(!path || !num_of_audio_tracks || !num_of_video_tracks || session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(dd_opened_file[session])
		goto already_inited;

	*num_of_video_tracks = 0;
	*num_of_audio_tracks = 0;
	dd_cache_packet_ready_index[session] = 0;
	dd_cache_packet_current_index[session] = 0;
	dd_available_cache_packet[session] = 0;
	for(uint8_t i = 0; i < DEF_DECODER_MAX_AUDIO_TRACKS; i++)
		dd_audio_stream_num[session][i] = UINT8_MAX;

	for(uint8_t i = 0; i < DEF_DECODER_MAX_VIDEO_TRACKS; i++)
		dd_video_stream_num[session][i] = UINT8_MAX;

	dd_format_context[session] = avformat_alloc_context();
	if(!dd_format_context[session])
	{
		DEF_LOG_RESULT(avformat_alloc_context, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		goto ffmpeg_api_failed;
	}

	ffmpeg_result = avformat_open_input(&dd_format_context[session], path, NULL, NULL);
	if(ffmpeg_result != 0)
	{
		DEF_LOG_RESULT(avformat_open_input, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	ffmpeg_result = avformat_find_stream_info(dd_format_context[session], NULL);
	if(ffmpeg_result != 0)
	{
		DEF_LOG_RESULT(avformat_find_stream_info, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	LightLock_Init(&dd_cache_packet_mutex[session]);

	for(uint8_t i = 0; i < dd_format_context[session]->nb_streams; i++)
	{
		if(dd_format_context[session]->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < DEF_DECODER_MAX_AUDIO_TRACKS)
		{
			dd_audio_stream_num[session][audio_index] = i;
			audio_index++;
		}
		else if(dd_format_context[session]->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < DEF_DECODER_MAX_VIDEO_TRACKS)
		{
			dd_video_stream_num[session][video_index] = i;
			video_index++;
		}
	}

	if(audio_index == 0 && video_index == 0)
	{
		DEF_LOG_STRING("No audio and video were found.");
		goto other;
	}

	*num_of_audio_tracks = audio_index;
	*num_of_video_tracks = video_index;
	dd_opened_file[session] = true;

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	already_inited:
	return DEF_ERR_ALREADY_INITIALIZED;

	ffmpeg_api_failed:
	avformat_free_context(dd_format_context[session]);
	dd_format_context[session] = NULL;
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;

	other:
	avformat_free_context(dd_format_context[session]);
	dd_format_context[session] = NULL;
	return DEF_ERR_OTHER;
}

AVFormatContext* DecoderDemux_format_context(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return NULL;
	return dd_format_context[session];
}

uint8_t DecoderDemux_audio_stream_num(uint8_t session, uint8_t audio_index)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || audio_index >= DEF_DECODER_MAX_AUDIO_TRACKS)
		return UINT8_MAX;
	return dd_audio_stream_num[session][audio_index];
}

uint8_t DecoderDemux_video_stream_num(uint8_t session, uint8_t video_index)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || video_index >= DEF_DECODER_MAX_VIDEO_TRACKS)
		return UINT8_MAX;
	return dd_video_stream_num[session][video_index];
}

void DecoderDemux_clear_cache_packet(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return;

	if(!dd_opened_file[session])
		return;

	for(uint16_t i = 0; i < DEF_DECODER_MAX_CACHE_PACKETS; i++)
		av_packet_free(&dd_cache_packet[session][i]);

	dd_available_cache_packet[session] = 0;
	dd_cache_packet_current_index[session] = 0;
	dd_cache_packet_ready_index[session] = 0;
}

uint16_t DecoderDemux_get_available_packet_num(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return 0;

	if(!dd_opened_file[session])
		return 0;
	else
		return dd_available_cache_packet[session];
}

uint32_t DecoderDemux_read_packet(uint8_t session)
{
	int32_t ffmpeg_result = 0;

	if(session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!dd_opened_file[session])
		goto not_inited;

	LightLock_Lock(&dd_cache_packet_mutex[session]);
	if(dd_available_cache_packet[session] + 1 >= DEF_DECODER_MAX_CACHE_PACKETS)
	{
		goto try_again;
	}
	LightLock_Unlock(&dd_cache_packet_mutex[session]);

	dd_cache_packet[session][dd_cache_packet_ready_index[session]] = av_packet_alloc();
	if(!dd_cache_packet[session][dd_cache_packet_ready_index[session]])
	{
		DEF_LOG_RESULT(av_packet_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		goto ffmpeg_api_failed;
	}

	ffmpeg_result = av_read_frame(dd_format_context[session], dd_cache_packet[session][dd_cache_packet_ready_index[session]]);
	if(ffmpeg_result != 0)
	{
		DEF_LOG_RESULT(av_read_frame, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	if(dd_cache_packet_ready_index[session] + 1 < DEF_DECODER_MAX_CACHE_PACKETS)
		dd_cache_packet_ready_index[session]++;
	else
		dd_cache_packet_ready_index[session] = 0;

	LightLock_Lock(&dd_cache_packet_mutex[session]);
	dd_available_cache_packet[session]++;
	LightLock_Unlock(&dd_cache_packet_mutex[session]);

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	LightLock_Unlock(&dd_cache_packet_mutex[session]);
	return DEF_ERR_TRY_AGAIN;

	ffmpeg_api_failed:
	av_packet_free(&dd_cache_packet[session][dd_cache_packet_ready_index[session]]);
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}

uint32_t DecoderDemux_parse_packet(Media_packet_type* type, uint8_t* packet_index, bool* key_frame, uint8_t session,
	DecoderDemuxRouteAudioFn route_audio,
	DecoderDemuxRouteVideoFn route_video)
{
	uint32_t route_result = 0;

	if(session >= DEF_DECODER_MAX_SESSIONS || !type || !packet_index || !key_frame || !route_audio || !route_video)
		goto invalid_arg;

	if(!dd_opened_file[session])
		goto not_inited;

	*key_frame = false;
	*packet_index = UINT8_MAX;
	*type = MEDIA_PACKET_TYPE_UNKNOWN;

	LightLock_Lock(&dd_cache_packet_mutex[session]);
	if(dd_available_cache_packet[session] == 0)
	{
		goto try_again;
	}
	LightLock_Unlock(&dd_cache_packet_mutex[session]);

	for(uint8_t i = 0; i < DEF_DECODER_MAX_AUDIO_TRACKS; i++)
	{
		if(dd_audio_stream_num[session][i] == UINT8_MAX)
			continue;

		if(dd_cache_packet[session][dd_cache_packet_current_index[session]]->stream_index == dd_audio_stream_num[session][i])
		{
			route_result = route_audio(session, i, dd_cache_packet[session][dd_cache_packet_current_index[session]]);
			if(route_result != DEF_SUCCESS)
				goto route_failed;

			*packet_index = i;
			*type = MEDIA_PACKET_TYPE_AUDIO;
			break;
		}
	}

	if(*type == MEDIA_PACKET_TYPE_UNKNOWN)
	{
		for(uint8_t i = 0; i < DEF_DECODER_MAX_VIDEO_TRACKS; i++)
		{
			if(dd_video_stream_num[session][i] == UINT8_MAX)
				continue;

			if(dd_cache_packet[session][dd_cache_packet_current_index[session]]->stream_index == dd_video_stream_num[session][i])
			{
				route_result = route_video(session, i, dd_cache_packet[session][dd_cache_packet_current_index[session]]);
				if(route_result != DEF_SUCCESS)
					goto route_failed;

				*packet_index = i;
				*type = MEDIA_PACKET_TYPE_VIDEO;
				*key_frame = (dd_cache_packet[session][dd_cache_packet_current_index[session]]->flags & AV_PKT_FLAG_KEY) != 0;
				break;
			}
		}
	}

	av_packet_free(&dd_cache_packet[session][dd_cache_packet_current_index[session]]);
	if(dd_cache_packet_current_index[session] + 1 < DEF_DECODER_MAX_CACHE_PACKETS)
		dd_cache_packet_current_index[session]++;
	else
		dd_cache_packet_current_index[session] = 0;

	LightLock_Lock(&dd_cache_packet_mutex[session]);
	dd_available_cache_packet[session]--;
	LightLock_Unlock(&dd_cache_packet_mutex[session]);
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	LightLock_Unlock(&dd_cache_packet_mutex[session]);
	return DEF_ERR_TRY_AGAIN;

	route_failed:
	return route_result;
}

static int DecoderDemux_seek_interrupt(void *opaque)
{
	const uint64_t *deadline = (const uint64_t *)opaque;

	if(!deadline)
		return 0;
	return osGetTime() >= *deadline ? 1 : 0;
}

uint32_t DecoderDemux_seek(uint64_t seek_pos, Media_seek_flag flag, uint8_t session)
{
	int32_t ffmpeg_result = 0;
	int32_t ffmpeg_seek_flag = 0;
	AVFormatContext *fmt = NULL;
	AVIOInterruptCB saved_interrupt;
	uint64_t deadline = 0;

	if(session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!dd_opened_file[session])
		goto not_inited;

	fmt = dd_format_context[session];
	if(!fmt)
		goto not_inited;

	if(flag & MEDIA_SEEK_FLAG_BACKWARD)
		ffmpeg_seek_flag |= AVSEEK_FLAG_BACKWARD;
	if(flag & MEDIA_SEEK_FLAG_BYTE)
		ffmpeg_seek_flag |= AVSEEK_FLAG_BYTE;
	if(flag & MEDIA_SEEK_FLAG_ANY)
		ffmpeg_seek_flag |= AVSEEK_FLAG_ANY;
	if(flag & MEDIA_SEEK_FLAG_FRAME)
		ffmpeg_seek_flag |= AVSEEK_FLAG_FRAME;

	deadline = osGetTime() + DEF_DEMUX_SEEK_INTERRUPT_MS;
	saved_interrupt = fmt->interrupt_callback;
	fmt->interrupt_callback.callback = DecoderDemux_seek_interrupt;
	fmt->interrupt_callback.opaque = &deadline;

	ffmpeg_result = avformat_seek_file(fmt, -1, INT64_MIN, (int64_t)(seek_pos * 1000), INT64_MAX, ffmpeg_seek_flag);

	fmt->interrupt_callback = saved_interrupt;

	if(ffmpeg_result < 0)
	{
		if(ffmpeg_result == AVERROR_EXIT)
			DEF_LOG_STRING("avformat_seek_file interrupted (time limit).");
		else
			DEF_LOG_RESULT(avformat_seek_file, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	ffmpeg_api_failed:
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}

void DecoderDemux_free_cache_and_close_format(uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS)
		return;

	for(uint16_t i = 0; i < DEF_DECODER_MAX_CACHE_PACKETS; i++)
		av_packet_free(&dd_cache_packet[session][i]);

	dd_available_cache_packet[session] = 0;
	dd_cache_packet_current_index[session] = 0;
	dd_cache_packet_ready_index[session] = 0;
	avformat_close_input(&dd_format_context[session]);
}
