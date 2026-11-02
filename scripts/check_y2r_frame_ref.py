"""Host fault-injection checks against the production frame/Y2R functions.

Uses the bundled FFmpeg types and mocked FFmpeg allocation / 3DS services.
Run with a host GCC: python scripts/check_y2r_frame_ref.py [--cc gcc].
This checks ownership and DMA setup, not physical Y2R hardware or timing.
"""
import argparse
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(path, name):
    source = (ROOT / path).read_text(encoding="utf-8")
    position = source.index(name + "(")
    start = source.rfind("\n", 0, position) + 1
    opening = source.index("{", position)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "system/util/err_types.h"
#define DEF_DECODER_MAX_SESSIONS 2
#define DEF_DECODER_MAX_VIDEO_TRACKS 2
#define OS_FCRAM_VADDR ((uintptr_t)storage)
#define OS_FCRAM_SIZE sizeof(storage)
#define OS_OLD_FCRAM_VADDR OS_FCRAM_VADDR
#define OS_OLD_FCRAM_SIZE OS_FCRAM_SIZE
#define CUR_PROCESS_HANDLE 1
#define INPUT_YUV420_INDIV_8 0
#define OUTPUT_RGB_32 0
#define ROTATION_NONE 0
#define BLOCK_8_BY_8 0
#define COEFFICIENT_ITU_R_BT_709_SCALING 0
typedef uint32_t Handle;
typedef struct { int input_format, output_format, rotation, block_alignment;
 int input_line_width, input_lines, standard_coefficient, alpha; } Y2RU_ConversionParams;
static uint8_t storage[1024*1024], left[512*256*4], right[512*256*4];
struct AVBuffer { int refs; void (*release)(void*, uint8_t*); void* opaque; uint8_t* data; };
static int frees, allocations, fail_buffer, fail_output_alloc;
static bool allocating_output;
static void* last_output;
static uint8_t* buffered_rgb;
static int output_tex_width=512;
static void tracked_free(void* p) { assert(p == storage); frees++; }
static void* linearAlloc(size_t size) {
 assert(size <= sizeof(storage)); allocations++;
 if(allocating_output) return last_output=fail_output_alloc?NULL:malloc(size);
 return storage;
}
AVBufferRef* av_buffer_create(uint8_t* p, size_t size, void (*release)(void*, uint8_t*), void* opaque, int flags)
{
 (void)flags; if(fail_buffer) return NULL;
 AVBufferRef* r = calloc(1,sizeof(*r)); r->buffer = calloc(1,sizeof(*r->buffer));
 *r->buffer = (struct AVBuffer){1,release,opaque,p}; r->data=p; r->size=size; return r;
}
AVBufferRef* av_buffer_ref(const AVBufferRef* r) { AVBufferRef* n=malloc(sizeof(*n)); *n=*r; n->buffer->refs++; return n; }
void av_buffer_unref(AVBufferRef** r) {
 if(!*r) return; AVBufferRef* old=*r; *r=NULL;
 if(!--old->buffer->refs) { old->buffer->release(old->buffer->opaque,old->buffer->data); free(old->buffer); }
 free(old);
}
AVFrame* av_frame_alloc(void) { return calloc(1,sizeof(AVFrame)); }
void av_frame_unref(AVFrame* f) { for(int i=0;i<8;i++) av_buffer_unref(&f->buf[i]); memset(f,0,sizeof(*f)); }
void av_frame_free(AVFrame** f) { if(*f) { av_frame_unref(*f); free(*f); *f=NULL; } }
void av_frame_move_ref(AVFrame* dst, AVFrame* src) { *dst=*src; memset(src,0,sizeof(*src)); }
AVBufferRef* av_frame_get_plane_buffer(const AVFrame* f, int plane) {
 for(int i=0;i<8 && f->buf[i];i++) {
  uintptr_t p=(uintptr_t)f->data[plane], b=(uintptr_t)f->buf[i]->data;
  if(p>=b && p-b<f->buf[i]->size) return f->buf[i];
 } return NULL;
}
int av_image_get_buffer_size(enum AVPixelFormat fmt, int w, int h, int align) {
 (void)align; assert(fmt==AV_PIX_FMT_YUV420P); return w*h*3/2;
}
int av_image_fill_arrays(uint8_t* data[4], int stride[4], const uint8_t* src, enum AVPixelFormat fmt, int w, int h, int align) {
 (void)fmt;(void)align; data[0]=(uint8_t*)src; data[1]=data[0]+w*h; data[2]=data[1]+w*h/4;
 stride[0]=w; stride[1]=stride[2]=w/2; return w*h*3/2;
}
static bool util_video_decoder_init[2][2];
static uint16_t util_video_decoder_available_raw_image[2][2], util_video_decoder_raw_image_ready_index[2][2];
static uint16_t util_video_decoder_max_raw_image[2][2];
static int util_video_decoder_raw_image_mutex[2][2];
static AVFrame* util_video_decoder_raw_image[2][2][128];
static AVStream stream; static AVStream* streams[]={&stream}; static AVFormatContext format;
static bool DecoderDemux_is_opened(uint8_t s) { (void)s; return true; }
static AVFormatContext* DecoderDemux_format_context(uint8_t s) { (void)s; return &format; }
static unsigned DecoderDemux_video_stream_num(uint8_t s,uint8_t t) { (void)s;(void)t;return 0; }
static void LightLock_Lock(int* lock) { (void)lock; }
static void LightLock_Unlock(int* lock) { (void)lock; }
static bool util_y2r_init=true, busy;
static int calls, fail_at, starts, closed, events, flushes, stops, stop_failures, mode, polls;
static int eye_width, image_height, input_width, input_height;
static uint64_t clock_ms;
static const uint8_t* expected[3]; static int expected_stride[3];
static struct { const uint8_t* data; uint32_t size; int unit,gap; } sending[3];
static uint32_t api(void) { return ++calls==fail_at ? 42 : 0; }
static uint32_t svcFlushProcessDataCache(Handle h,uint32_t p,uint32_t size) {
 (void)h;(void)p;assert(size>0);flushes++;return api();
}
static uint32_t Y2RU_GetTransferEndEvent(Handle* h) { uint32_t r=api();if(!r){*h=7;events++;}return r; }
static uint32_t Y2RU_SetConversionParams(const Y2RU_ConversionParams* p) {
 eye_width=p->input_line_width; image_height=p->input_lines;return api();
}
static uint32_t send_plane(int i,const void* p,uint32_t size,int unit,int gap) {
 sending[i].data=p;sending[i].size=size;sending[i].unit=unit;sending[i].gap=gap;return api();
}
static uint32_t Y2RU_SetSendingY(const void* p,uint32_t size,int16_t u,int16_t g) { return send_plane(0,p,size,u,g); }
static uint32_t Y2RU_SetSendingU(const void* p,uint32_t size,int16_t u,int16_t g) { return send_plane(1,p,size,u,g); }
static uint32_t Y2RU_SetSendingV(const void* p,uint32_t size,int16_t u,int16_t g) { return send_plane(2,p,size,u,g); }
static uint32_t Y2RU_SetReceiving(void* p,uint32_t size,int16_t unit,int16_t gap) {
 assert(p==(output_tex_width>=1024?last_output:(starts?right:left)));
 assert(size==(uint32_t)eye_width*image_height*4);
 if(output_tex_width>=1024) assert(unit==eye_width*16 && gap==0);
 else assert(unit==eye_width*32 && unit+gap==output_tex_width*32);
 return api();
}
static uint32_t svcClearEvent(Handle h) { assert(h==7);assert(!busy);return api(); }
static uint32_t Y2RU_StartConversion(void) {
 assert(!frees);assert(!busy);busy=true;int eye=starts++;
 for(int i=0;i<3;i++) {
  int w=i?eye_width/2:eye_width, rows=i?input_height/2:input_height;
  assert(sending[i].data==expected[i]+eye*w);
  assert(sending[i].unit==w && sending[i].gap==expected_stride[i]-w);
  assert(sending[i].size==(uint32_t)w*rows);
  /* Compare every DMA row against the intended eye, including padded strides. */
  for(int y=0;y<rows;y++) assert(!memcmp(sending[i].data+y*(w+sending[i].gap),expected[i]+y*expected_stride[i]+eye*w,(size_t)w));
 } return api();
}
static uint32_t svcWaitSynchronization(Handle h,int64_t timeout) {
 (void)timeout;assert(h==7);assert(busy);
 if(mode==2) return 99; /* genuine timeout */
 if(mode!=1) busy=false;
 return mode==3?99:0; /* missed event, but DMA completed */
}
static uint32_t Y2RU_IsDoneReceiving(bool* done) {
 uint32_t r=api(); if(mode==1 && ++polls>=2) busy=false;
 *done=!busy;return r;
}
static uint32_t Y2RU_StopConversion(void) { stops++;if(stop_failures-->0)return 42;busy=false;return 0; }
static uint32_t svcCloseHandle(Handle h) { assert(h==7);closed++;return 0; }
static void svcSleepThread(int64_t ns) { (void)ns;clock_ms++; }
static uint64_t osGetTime(void) { return clock_ms; }
'''

TESTS = r'''
static void reset_hardware(AVFrame* f,int width) {
 calls=fail_at=starts=closed=events=flushes=stops=stop_failures=mode=polls=0;
 clock_ms=0;busy=false;input_width=width;input_height=240;
 allocating_output=true;output_tex_width=512;buffered_rgb=NULL;fail_output_alloc=0;
 for(int i=0;i<3;i++){expected[i]=f->data[i];expected_stride[i]=f->linesize[i];}
}
static AVFrame* make_frame(int width) {
 allocating_output=false;
 AVFrame* f=av_frame_alloc();f->width=width;f->height=240;f->format=AV_PIX_FMT_YUV420P;f->pts=9000;f->duration=3750;
 AVCodecContext c={0};c.codec_type=AVMEDIA_TYPE_VIDEO;c.codec_id=AV_CODEC_ID_MPEG2VIDEO;c.pix_fmt=AV_PIX_FMT_YUV420P;
 assert(dvs_video_allocate_buffer(&c,f,0)==0);return f;
}
static void enqueue(AVFrame* f,int index) {
 format.streams=streams;stream.time_base=(AVRational){1,90000};stream.avg_frame_rate=(AVRational){24,1};
 util_video_decoder_init[0][0]=true;util_video_decoder_max_raw_image[0][0]=128;
 util_video_decoder_raw_image_ready_index[0][0]=index;util_video_decoder_available_raw_image[0][0]=1;
 util_video_decoder_raw_image[0][0][index]=f;
}
static uint32_t convert(AVFrame* f,bool sbs) {
 const uint8_t* planes[]={f->data[0],f->data[1],f->data[2]};
 return Util_converter_y2r_yuv420p_planes_to_rgba8888(planes,f->linesize,left,sbs?right:NULL,(uint16_t)output_tex_width,(uint16_t)input_width,240,&buffered_rgb);
}
int main(void) {
 for(size_t i=0;i<sizeof(storage);i++)storage[i]=(uint8_t)((i*31+i/800)%251);
 AVFrame* f=make_frame(800);AVFrame* held=av_frame_alloc();double pos=-1;
 assert(f->buf[0]->size==288000 && !f->buf[1] && allocations==1);
 AVBufferRef* decoder_reference=av_buffer_ref(f->buf[0]);enqueue(f,127);
 uint8_t* original=f->data[0];
 assert(DecoderVideoSoft_get_image_ref(held,&pos,800,240,0,0)==0);
 assert(held->data[0]==original && pos==100 && !util_video_decoder_raw_image[0][0][127]);
 assert(!util_video_decoder_available_raw_image[0][0] && !util_video_decoder_raw_image_ready_index[0][0]);
 assert(allocations==1 && !frees);
 reset_hardware(held,800);assert(convert(held,true)==0);
 assert(starts==2 && flushes==1 && events==1 && closed==1 && !busy && !stops);
 int normal_calls=calls;
 for(int failure=1;failure<=normal_calls;failure++) {
  reset_hardware(held,800);fail_at=failure;
  assert(convert(held,true)==42);assert(!busy && !frees && closed==events);
 }
 reset_hardware(held,800);mode=1;assert(convert(held,true)==0 && !busy); /* early event */
 reset_hardware(held,800);mode=3;assert(convert(held,true)==0 && !busy); /* missed event */
 reset_hardware(held,800);mode=2;stop_failures=1;
 assert(convert(held,true)==99 && stops==2 && !busy && !frees);
 av_frame_unref(held);assert(!frees);av_buffer_unref(&decoder_reference);assert(frees==1);

 /* Unsupported input must not consume the frame needed by the copying fallback. */
 frees=0;f=make_frame(816);enqueue(f,0);int stride=f->linesize[0];
 f->linesize[0]=-stride;assert(DecoderVideoSoft_get_image_ref(held,&pos,800,240,0,0)==DEF_ERR_DISABLED);
 f->linesize[0]=stride;size_t size=f->buf[0]->size;f->buf[0]->size=1;
 assert(DecoderVideoSoft_get_image_ref(held,&pos,800,240,0,0)==DEF_ERR_DISABLED);f->buf[0]->size=size;
 original=f->data[0];f->data[0]=(uint8_t*)(uintptr_t)1;
 assert(DecoderVideoSoft_get_image_ref(held,&pos,800,240,0,0)==DEF_ERR_DISABLED);f->data[0]=original;
 f->format=AV_PIX_FMT_NV12;assert(DecoderVideoSoft_get_image_ref(held,&pos,800,240,0,0)==DEF_ERR_DISABLED);f->format=AV_PIX_FMT_YUV420P;
 assert(util_video_decoder_raw_image[0][0][0]==f && util_video_decoder_available_raw_image[0][0]==1);
 f->pts=AV_NOPTS_VALUE;f->pkt_dts=18000;
 assert(DecoderVideoSoft_get_image_ref(held,&pos,800,240,0,0)==0 && pos==200);
 reset_hardware(held,800);assert(convert(held,true)==0 && flushes==1); /* padded rows */
 /* Independent plane offsets and unequal U/V strides. */
 held->buf[0]->size=sizeof(storage);held->data[1]=storage+200000;held->data[2]=storage+260000;
 held->linesize[1]=432;held->linesize[2]=448;
 reset_hardware(held,800);assert(convert(held,true)==0 && flushes==3);
 av_frame_unref(held);assert(frees==1);

 frees=0;f=make_frame(400);reset_hardware(f,400);assert(convert(f,false)==0);
 assert(starts==1 && flushes==1 && closed==1 && !busy);av_frame_free(&f);assert(frees==1);
 /* 800/1024-wide 2D: preserve RGB staging, eliminate YUV staging. */
 for(int w=800;w<=1024;w+=224) {
  frees=0;f=make_frame(w);reset_hardware(f,w);output_tex_width=1024;
  assert(convert(f,false)==0 && buffered_rgb==last_output);
  assert(starts==1 && flushes==1 && !busy);free(buffered_rgb);
  int buffered_calls=calls;
  for(int failure=1;failure<=buffered_calls;failure++) {
   reset_hardware(f,w);output_tex_width=1024;fail_at=failure;
   assert(convert(f,false)==42 && !busy && !frees && closed==events);free(buffered_rgb);
  }
  reset_hardware(f,w);output_tex_width=1024;fail_output_alloc=1;
  assert(convert(f,false)==DEF_ERR_OUT_OF_LINEAR_MEMORY && !starts && !events);
  av_frame_free(&f);assert(frees==1);
 }
 assert(Util_converter_y2r_can_convert_frame(1024,800,240,false));
 assert(!Util_converter_y2r_can_convert_frame(1024,800,240,true));
 assert(!Util_converter_y2r_can_convert_frame(512,800,239,true));
 assert(!Util_converter_y2r_can_convert_frame(512,808,240,true));
 assert(DecoderVideoSoft_get_image_ref(held,&pos,800,240,0,0)==DEF_ERR_TRY_AGAIN);
 /* Allocation-wrapper failure releases the allocation exactly once. */
 frees=0;fail_buffer=1;allocating_output=false;f=av_frame_alloc();f->width=800;f->height=240;
 AVCodecContext c={0};c.codec_type=AVMEDIA_TYPE_VIDEO;c.pix_fmt=AV_PIX_FMT_YUV420P;
 assert(dvs_video_allocate_buffer(&c,f,0)==-1 && frees==1 && !f->data[0]);
 av_frame_free(&f);assert(frees==1);av_frame_free(&held);
 puts("PASS: ownership, reference lifetime, queue fallback/wrap, 2D/SBS DMA addressing, padded/separate planes, 800/1024-wide 2D RGB fallback, all API failure points, early/missed events, timeout cancellation");
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    soft = "source/system/util/decoder_video_soft.c"
    converter = "source/system/util/converter.c"
    code = PREAMBLE + "\n#define free tracked_free\n"
    code += function(soft, "dvs_frame_buffer_free")
    code += function(soft, "dvs_video_allocate_buffer")
    code += "\n#undef free\n" + function(soft, "DecoderVideoSoft_get_image_ref")
    for name in ("Util_converter_y2r_can_convert_frame", "converter_y2r_wait_for_frame",
                 "converter_y2r_cancel_frame", "Util_converter_y2r_yuv420p_planes_to_rgba8888"):
        code += function(converter, name)
    code += TESTS
    with tempfile.TemporaryDirectory(prefix="y2r-frame-ref-") as temp:
        source = pathlib.Path(temp) / "check.c"
        executable = pathlib.Path(temp) / "check.exe"
        source.write_text(code, encoding="utf-8")
        subprocess.run([args.cc, "-std=c99", "-O2", "-Wall", "-Wextra",
                        "-Wno-pointer-to-int-cast", "-Wno-misleading-indentation",
                        "-I" + str(ROOT / "library/include"), "-I" + str(ROOT / "include"),
                        str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
