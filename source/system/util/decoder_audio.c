#include "system/util/decoder_audio.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "3ds.h"

#include "system/util/decoder_demux.h"
#include "system/util/err_types.h"
#include "system/util/log.h"
#include "system/util/util.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

static bool util_audio_decoder_init[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static bool util_audio_decoder_packet_ready[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static bool util_audio_decoder_cache_packet_ready[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static AVPacket* util_audio_decoder_packet[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static AVPacket* util_audio_decoder_cache_packet[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static AVFrame* util_audio_decoder_raw_data[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static AVCodecContext* util_audio_decoder_context[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };
static const AVCodec* util_audio_decoder_codec[DEF_DECODER_MAX_SESSIONS][DEF_DECODER_MAX_AUDIO_TRACKS] = { 0, };

static Raw_sample util_audio_decoder_sample_format_table[AV_SAMPLE_FMT_NB] =
{
	RAW_SAMPLE_U8,
	RAW_SAMPLE_S16,
	RAW_SAMPLE_S32,
	RAW_SAMPLE_FLOAT32,
	RAW_SAMPLE_DOUBLE64,
	RAW_SAMPLE_U8P,
	RAW_SAMPLE_S16P,
	RAW_SAMPLE_S32P,
	RAW_SAMPLE_FLOAT32P,
	RAW_SAMPLE_DOUBLE64P,
	RAW_SAMPLE_S64,
	RAW_SAMPLE_S64P,
};

static uint8_t util_audio_decoder_sample_format_size_table[] =
{
	sizeof(uint8_t),
	sizeof(int16_t),
	sizeof(int32_t),
	sizeof(float),
	sizeof(double),
	sizeof(uint8_t),
	sizeof(int16_t),
	sizeof(int32_t),
	sizeof(float),
	sizeof(double),
	sizeof(int64_t),
	sizeof(int64_t),
};

uint32_t DecoderAudio_init(uint8_t num_of_audio_tracks, uint8_t session)
{
	int32_t ffmpeg_result = 0;

	if(num_of_audio_tracks == 0 || num_of_audio_tracks > DEF_DECODER_MAX_AUDIO_TRACKS || session >= DEF_DECODER_MAX_SESSIONS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session))
		goto not_inited;

	for(uint8_t i = 0; i < DEF_DECODER_MAX_AUDIO_TRACKS; i++)
	{
		if(util_audio_decoder_init[session][i])
			goto already_inited;
	}

	for(uint8_t i = 0; i < num_of_audio_tracks; i++)
	{
		if(DecoderDemux_audio_stream_num(session, i) == UINT8_MAX)
			goto invalid_arg;
	}

	for(uint8_t i = 0; i < num_of_audio_tracks; i++)
	{
		util_audio_decoder_codec[session][i] = avcodec_find_decoder(DecoderDemux_format_context(session)->streams[DecoderDemux_audio_stream_num(session, i)]->codecpar->codec_id);
		if(!util_audio_decoder_codec[session][i])
		{
			DEF_LOG_RESULT(avcodec_find_decoder, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
			goto ffmpeg_api_failed;
		}

		util_audio_decoder_context[session][i] = avcodec_alloc_context3(util_audio_decoder_codec[session][i]);
		if(!util_audio_decoder_context[session][i])
		{
			DEF_LOG_RESULT(avcodec_alloc_context3, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
			goto ffmpeg_api_failed;
		}

		ffmpeg_result = avcodec_parameters_to_context(util_audio_decoder_context[session][i], DecoderDemux_format_context(session)->streams[DecoderDemux_audio_stream_num(session, i)]->codecpar);
		if(ffmpeg_result != 0)
		{
			DEF_LOG_RESULT(avcodec_parameters_to_context, false, ffmpeg_result);
			goto ffmpeg_api_failed;
		}

		util_audio_decoder_context[session][i]->flags = AV_CODEC_FLAG_OUTPUT_CORRUPT;
		ffmpeg_result = avcodec_open2(util_audio_decoder_context[session][i], util_audio_decoder_codec[session][i], NULL);
		if(ffmpeg_result != 0)
		{
			DEF_LOG_RESULT(avcodec_open2, false, ffmpeg_result);
			goto ffmpeg_api_failed;
		}

		util_audio_decoder_init[session][i] = true;
	}

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	already_inited:
	return DEF_ERR_ALREADY_INITIALIZED;

	ffmpeg_api_failed:
	for(uint8_t i = 0; i < DEF_DECODER_MAX_AUDIO_TRACKS; i++)
	{
		util_audio_decoder_init[session][i] = false;
		avcodec_free_context(&util_audio_decoder_context[session][i]);
	}
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}

void DecoderAudio_exit(uint8_t session)
{
	for(uint8_t i = 0; i < DEF_DECODER_MAX_AUDIO_TRACKS; i++)
	{
		if(util_audio_decoder_init[session][i])
		{
			util_audio_decoder_init[session][i] = false;
			util_audio_decoder_cache_packet_ready[session][i] = false;
			util_audio_decoder_packet_ready[session][i] = false;
			avcodec_free_context(&util_audio_decoder_context[session][i]);
			av_packet_free(&util_audio_decoder_packet[session][i]);
			av_packet_free(&util_audio_decoder_cache_packet[session][i]);
			av_frame_free(&util_audio_decoder_raw_data[session][i]);
		}
	}
}

void DecoderAudio_get_info(Media_a_info* audio_info, uint8_t audio_index, uint8_t session)
{
	uint32_t size = 0;
	const char lang_und[] = "und";
	AVDictionaryEntry *data = NULL;

	if(!audio_info || audio_index >= DEF_DECODER_MAX_AUDIO_TRACKS || session >= DEF_DECODER_MAX_SESSIONS)
		return;

	if(!DecoderDemux_is_opened(session) || !util_audio_decoder_init[session][audio_index])
		return;

	audio_info->bitrate = util_audio_decoder_context[session][audio_index]->bit_rate;
	audio_info->sample_rate = util_audio_decoder_context[session][audio_index]->sample_rate;
	audio_info->ch = util_audio_decoder_context[session][audio_index]->ch_layout.nb_channels;
	audio_info->duration = (double)DecoderDemux_format_context(session)->duration / AV_TIME_BASE;
	if(util_audio_decoder_context[session][audio_index]->sample_fmt < 0 || util_audio_decoder_context[session][audio_index]->sample_fmt >= AV_SAMPLE_FMT_NB)
		audio_info->sample_format = RAW_SAMPLE_INVALID;
	else
		audio_info->sample_format = util_audio_decoder_sample_format_table[util_audio_decoder_context[session][audio_index]->sample_fmt];

	if(DecoderDemux_format_context(session)->streams[DecoderDemux_audio_stream_num(session, audio_index)]->metadata)
		data = av_dict_get(DecoderDemux_format_context(session)->streams[DecoderDemux_audio_stream_num(session, audio_index)]->metadata, "language", data, AV_DICT_IGNORE_SUFFIX);

	size = (data ? strlen(data->value) : strlen(lang_und));
	size = Util_min(size, (sizeof(audio_info->track_lang) - 1));
	if(data)
		memcpy(audio_info->track_lang, data->value, size);
	else
		memcpy(audio_info->track_lang, lang_und, size);

	audio_info->track_lang[size] = 0x00;

	size = (util_audio_decoder_codec[session][audio_index]->long_name ? strlen(util_audio_decoder_codec[session][audio_index]->long_name) : 0);
	if(size > 0)
	{
		size = Util_min(size, (sizeof(audio_info->format_name) - 1));
		memcpy(audio_info->format_name, util_audio_decoder_codec[session][audio_index]->long_name, size);
	}
	audio_info->format_name[size] = 0x00;

	size = (util_audio_decoder_codec[session][audio_index]->name ? strlen(util_audio_decoder_codec[session][audio_index]->name) : 0);
	if(size > 0)
	{
		size = Util_min(size, (sizeof(audio_info->short_format_name) - 1));
		memcpy(audio_info->short_format_name, util_audio_decoder_codec[session][audio_index]->name, size);
	}
	audio_info->short_format_name[size] = 0x00;
}

uint32_t DecoderAudio_route_from_demux(uint8_t session, uint8_t track, AVPacket* front_pkt)
{
	int32_t ffmpeg_result = 0;

	util_audio_decoder_cache_packet[session][track] = av_packet_alloc();
	if(!util_audio_decoder_cache_packet[session][track])
	{
		DEF_LOG_RESULT(av_packet_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	}

	av_packet_unref(util_audio_decoder_cache_packet[session][track]);
	ffmpeg_result = av_packet_ref(util_audio_decoder_cache_packet[session][track], front_pkt);
	if(ffmpeg_result != 0)
	{
		av_packet_free(&util_audio_decoder_cache_packet[session][track]);
		DEF_LOG_RESULT(av_packet_ref, false, ffmpeg_result);
		return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	}

	util_audio_decoder_cache_packet_ready[session][track] = true;
	return DEF_SUCCESS;
}

uint32_t DecoderAudio_ready_packet(uint8_t packet_index, uint8_t session)
{
	int32_t ffmpeg_result = 0;

	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_AUDIO_TRACKS)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !util_audio_decoder_init[session][packet_index])
		goto not_inited;

	if(!util_audio_decoder_cache_packet_ready[session][packet_index])
	{
		goto try_again;
	}

	if(util_audio_decoder_packet_ready[session][packet_index])
	{
		goto try_again;
	}

	av_packet_free(&util_audio_decoder_packet[session][packet_index]);
	util_audio_decoder_packet[session][packet_index] = av_packet_alloc();
	if(!util_audio_decoder_packet[session][packet_index])
	{
		DEF_LOG_RESULT(av_packet_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		goto ffmpeg_api_failed;
	}

	av_packet_unref(util_audio_decoder_packet[session][packet_index]);
	ffmpeg_result = av_packet_ref(util_audio_decoder_packet[session][packet_index], util_audio_decoder_cache_packet[session][packet_index]);
	if(ffmpeg_result != 0)
	{
		DEF_LOG_RESULT(av_packet_ref, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	util_audio_decoder_cache_packet_ready[session][packet_index] = false;
	util_audio_decoder_packet_ready[session][packet_index] = true;
	av_packet_free(&util_audio_decoder_cache_packet[session][packet_index]);
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	return DEF_ERR_TRY_AGAIN;

	ffmpeg_api_failed:
	av_packet_free(&util_audio_decoder_packet[session][packet_index]);
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}

void DecoderAudio_skip_packet(uint8_t packet_index, uint8_t session)
{
	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_AUDIO_TRACKS)
		return;

	if(!DecoderDemux_is_opened(session))
		return;

	if(!util_audio_decoder_cache_packet_ready[session][packet_index])
		return;

	av_packet_free(&util_audio_decoder_cache_packet[session][packet_index]);
	util_audio_decoder_cache_packet_ready[session][packet_index] = false;
}

uint32_t DecoderAudio_decode(uint32_t* samples, uint8_t** raw_data, double* current_pos, uint8_t packet_index, uint8_t session)
{
	int32_t ffmpeg_result = 0;
	uint32_t copy_size_per_ch = 0;
	double current_frame = 0;
	double timebase = 0;

	if(session >= DEF_DECODER_MAX_SESSIONS || packet_index >= DEF_DECODER_MAX_AUDIO_TRACKS || !samples || !raw_data || !current_pos)
		goto invalid_arg;

	if(!DecoderDemux_is_opened(session) || !util_audio_decoder_init[session][packet_index])
		goto not_inited;

	if(!util_audio_decoder_packet_ready[session][packet_index])
	{
		goto try_again;
	}

	if(util_audio_decoder_packet[session][packet_index]->duration != 0)
		current_frame = (double)util_audio_decoder_packet[session][packet_index]->dts / util_audio_decoder_packet[session][packet_index]->duration;

	*samples = 0;
	*current_pos = 0;

	util_audio_decoder_raw_data[session][packet_index] = av_frame_alloc();
	if(!util_audio_decoder_raw_data[session][packet_index])
	{
		DEF_LOG_RESULT(av_frame_alloc, false, DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS);
		goto ffmpeg_api_failed;
	}

	ffmpeg_result = avcodec_send_packet(util_audio_decoder_context[session][packet_index], util_audio_decoder_packet[session][packet_index]);
	if(ffmpeg_result != 0)
	{
		DEF_LOG_RESULT(avcodec_send_packet, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	ffmpeg_result = avcodec_receive_frame(util_audio_decoder_context[session][packet_index], util_audio_decoder_raw_data[session][packet_index]);
	if(ffmpeg_result != 0)
	{
		DEF_LOG_RESULT(avcodec_receive_frame, false, ffmpeg_result);
		goto ffmpeg_api_failed;
	}

	timebase = av_q2d(DecoderDemux_format_context(session)->streams[DecoderDemux_audio_stream_num(session, packet_index)]->time_base);
	if(timebase != 0)
		*current_pos = (double)util_audio_decoder_packet[session][packet_index]->dts * timebase * 1000;
	else
		*current_pos = current_frame * ((1000.0 / util_audio_decoder_raw_data[session][packet_index]->sample_rate) * util_audio_decoder_raw_data[session][packet_index]->nb_samples);

	copy_size_per_ch = util_audio_decoder_raw_data[session][packet_index]->nb_samples * util_audio_decoder_sample_format_size_table[util_audio_decoder_context[session][packet_index]->sample_fmt];
	free(*raw_data);
	*raw_data = NULL;
	*raw_data = (uint8_t*)linearAlloc(copy_size_per_ch * util_audio_decoder_context[session][packet_index]->ch_layout.nb_channels);
	if(!*raw_data)
		goto out_of_memory;

	if(util_audio_decoder_context[session][packet_index]->sample_fmt == AV_SAMPLE_FMT_U8P || util_audio_decoder_context[session][packet_index]->sample_fmt == AV_SAMPLE_FMT_S16P
	|| util_audio_decoder_context[session][packet_index]->sample_fmt == AV_SAMPLE_FMT_S32P || util_audio_decoder_context[session][packet_index]->sample_fmt == AV_SAMPLE_FMT_S64P
	|| util_audio_decoder_context[session][packet_index]->sample_fmt == AV_SAMPLE_FMT_FLTP || util_audio_decoder_context[session][packet_index]->sample_fmt == AV_SAMPLE_FMT_DBLP)
	{
		for(uint8_t i = 0; i < util_audio_decoder_context[session][packet_index]->ch_layout.nb_channels; i++)
			memcpy(((*raw_data) + (copy_size_per_ch * i)), util_audio_decoder_raw_data[session][packet_index]->data[i], copy_size_per_ch);
	}
	else
		memcpy(*raw_data, util_audio_decoder_raw_data[session][packet_index]->data[0], copy_size_per_ch * util_audio_decoder_context[session][packet_index]->ch_layout.nb_channels);

	*samples = util_audio_decoder_raw_data[session][packet_index]->nb_samples;

	util_audio_decoder_packet_ready[session][packet_index] = false;
	av_packet_free(&util_audio_decoder_packet[session][packet_index]);
	av_frame_free(&util_audio_decoder_raw_data[session][packet_index]);
	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	not_inited:
	return DEF_ERR_NOT_INITIALIZED;

	try_again:
	return DEF_ERR_TRY_AGAIN;

	out_of_memory:
	util_audio_decoder_packet_ready[session][packet_index] = false;
	av_packet_free(&util_audio_decoder_packet[session][packet_index]);
	av_frame_free(&util_audio_decoder_raw_data[session][packet_index]);
	return DEF_ERR_OUT_OF_MEMORY;

	ffmpeg_api_failed:
	util_audio_decoder_packet_ready[session][packet_index] = false;
	free(*raw_data);
	*raw_data = NULL;
	av_packet_free(&util_audio_decoder_packet[session][packet_index]);
	av_frame_free(&util_audio_decoder_raw_data[session][packet_index]);
	return DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
}
