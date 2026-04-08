# One-off: extract video-soft implementation from decoder.c into decoder_video_soft.c
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEC = ROOT / "source/system/util/decoder.c"
OUT = ROOT / "source/system/util/decoder_video_soft.c"

lines = DEC.read_text(encoding="utf-8").splitlines()


def grab(a: int, b: int) -> str:
    return "\n".join(lines[a - 1 : b])


pixel = "\n".join(lines[86 - 1 : 311])
statics = "\n".join(lines[51 - 1 : 66])

chunks = [
    (325, 403),  # buffer free + allocate
    (415, 424),  # set cores
    (428, 554),  # video init
    (692, 767),  # video get_info
    (769, 791),  # route from demux
    (818, 878),  # ready video packet
    (880, 893),  # skip video
    (895, 906),  # set raw buffer size
    (920, 929),  # get raw buffer size
    (947, 1038),  # video decode
    (1344, 1358),  # clear raw image
    (1384, 1393),  # get available raw image num
    (1406, 1543),  # get image
    (1617, 1660),  # skip image
    (1728, 1747),  # video exit
]

body = "\n\n".join(grab(a, b) for a, b in chunks)

replacements = [
    ("static void Util_decoder_video_free", "static void dvs_frame_buffer_free"),
    ("Util_decoder_video_free", "dvs_frame_buffer_free"),
    ("static int Util_decoder_video_allocate_buffer", "static int dvs_video_allocate_buffer"),
    ("Util_decoder_video_allocate_buffer", "dvs_video_allocate_buffer"),
    ("void Util_decoder_video_set_enabled_cores", "void DecoderVideoSoft_set_enabled_cores"),
    ("uint32_t Util_decoder_video_init", "uint32_t DecoderVideoSoft_init"),
    ("void Util_decoder_video_get_info", "void DecoderVideoSoft_get_info"),
    ("static uint32_t util_decoder_demux_route_video", "uint32_t DecoderVideoSoft_route_from_demux"),
    ("uint32_t Util_decoder_ready_video_packet", "uint32_t DecoderVideoSoft_ready_packet"),
    ("void Util_decoder_skip_video_packet", "void DecoderVideoSoft_skip_packet"),
    ("void Util_decoder_video_set_raw_image_buffer_size", "void DecoderVideoSoft_set_raw_image_buffer_size"),
    ("uint32_t Util_decoder_video_get_raw_image_buffer_size", "uint32_t DecoderVideoSoft_get_raw_image_buffer_size"),
    ("uint32_t Util_decoder_video_decode", "uint32_t DecoderVideoSoft_decode"),
    ("void Util_decoder_video_clear_raw_image", "void DecoderVideoSoft_clear_raw_image"),
    ("uint16_t Util_decoder_video_get_available_raw_image_num", "uint16_t DecoderVideoSoft_get_available_raw_image_num"),
    ("uint32_t Util_decoder_video_get_image", "uint32_t DecoderVideoSoft_get_image"),
    ("void Util_decoder_video_skip_image", "void DecoderVideoSoft_skip_image"),
    ("static void Util_decoder_video_exit", "void DecoderVideoSoft_exit"),
]

for old, new in replacements:
    body = body.replace(old, new)

header = r'''#include "system/util/decoder_video_soft.h"

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

'''

bridge = r'''

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
'''

OUT.write_text(
    header + statics + "\n\n" + pixel + "\n\n" + body + bridge,
    encoding="utf-8",
    newline="\n",
)
print("Wrote", OUT)
