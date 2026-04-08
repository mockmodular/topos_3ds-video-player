# One-off bootstrap: extracted MVD from decoder.c into decoder_mvd.c.
# Maintain decoder_mvd.c directly; re-run only if re-splitting from a known-good decoder.c snapshot (update line ranges).
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEC = ROOT / "source/system/util/decoder.c"
OUT = ROOT / "source/system/util/decoder_mvd.c"

lines = DEC.read_text(encoding="utf-8").splitlines()

def grab(a: int, b: int) -> str:
    return "\n".join(lines[a - 1 : b])

chunks = [
    (7, 9),  # MVD_OUTPUT comment + define
    (47, 63),  # MVD statics
    (97, 226),  # mvd_init
    (283, 293),  # mvd_set_raw
    (300, 309),  # mvd_get_raw_size
    (321, 618),  # mvd_decode
    (624, 647),  # mvd_clear
    (654, 663),  # mvd_get_available
    (670, 740),  # mvd_get_image
    (747, 791),  # mvd_skip
    (813, 835),  # mvd_exit
]

body = "\n\n".join(grab(a, b) for a, b in chunks)

replacements = [
    ("Util_decoder_mvd_init", "DecoderMvd_init"),
    ("Util_decoder_mvd_set_raw_image_buffer_size", "DecoderMvd_set_raw_image_buffer_size"),
    ("Util_decoder_mvd_get_raw_image_buffer_size", "DecoderMvd_get_raw_image_buffer_size"),
    ("Util_decoder_mvd_decode", "DecoderMvd_decode"),
    ("Util_decoder_mvd_clear_raw_image", "DecoderMvd_clear_raw_image"),
    ("Util_decoder_mvd_get_available_raw_image_num", "DecoderMvd_get_available_raw_image_num"),
    ("Util_decoder_mvd_get_image", "DecoderMvd_get_image"),
    ("Util_decoder_mvd_skip_image", "DecoderMvd_skip_image"),
    ("static void Util_decoder_mvd_exit", "void DecoderMvd_exit"),
]
for old, new in replacements:
    body = body.replace(old, new)

header = r'''#include "system/util/decoder_mvd.h"

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

'''

OUT.write_text(header + "\n" + body + "\n", encoding="utf-8")
print("Wrote", OUT)
