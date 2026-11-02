"""Host checks for track UI, real settings round-trips, and playback selection wiring.

Compiles production settings/parser/string code with an in-memory settings store.
3DS playback and speaker hardware still require a console smoke test.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

from check_y2r_frame_ref import function

ROOT = pathlib.Path(__file__).resolve().parents[1]

PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system/util/str.h"
#include "system/util/err_types.h"
#include "system/topos_setting.h"
#include "vid_panel_settings.h"
#define DEF_LOG_STRING(...) ((void)0)
#define DEF_LOG_INT(...) ((void)0)
#define DEF_LOG_FORMAT(...) ((void)0)
#define DEF_LOG_RESULT(...) ((void)0)
#define DEF_LOG_RESULT_SMART(out,expr,...) ((out)=(expr))
#define Util_min(a,b) ((a)<(b)?(a):(b))
#define Util_max(a,b) ((a)>(b)?(a):(b))
#define DEF_UTIL_ARRAY_NUM_OF_ELEMENTS(a) (sizeof(a)/sizeof((a)[0]))
#define VID_FS_BROWSER_ROOT_TF 0
#define VID_TEX_FILTER_AUTO 2
#define VID_TEX_FILTER_BILINEAR 0
#define VID_TEX_FILTER_NEAREST 1
#define VID_SCALE_FIT 1
#define VID_HW_CONV_Y2R_X2 1
#define VID_HW_CONV_NEON_Y2R 2
#define DEF_SEM_SCREEN_MODE_MAX 3
#define DEF_SEM_MODEL_MAX 2
#define DEF_SEM_MODEL_N3DS 1
#define DEF_SEM_MODEL_O3DS 0
#define VID_FIXED_RESTART_PLAYBACK_THRESHOLD 24
static struct {
 bool auto_dim_5s,ui_mod,disable_audio,disable_video,use_hw_decoding,use_hw_decoding_pending;
 bool use_multi_threaded_decoding,sbs_swap_eyes;
 uint8_t fs_browser_root_mode,texture_filter_mode,video_scale_mode,seek_duration;
 uint8_t use_hw_color_conversion,use_hw_color_conversion_pending,num_of_threads;
 uint8_t audio_track,active_audio_track,num_of_audio_tracks;
 uint16_t volume;
} vid_player;
typedef struct { uint8_t screen_mode; bool is_eco; } Sem_config;
static Sem_config sem_config;
static uint8_t fake_model=255;
static char* disk;
static unsigned reads,writes;
static char* copy_text(const char* p) { size_t n=strlen(p)+1;char* q=malloc(n);memcpy(q,p,n);return q; }
static void Sem_get_config(Sem_config* c) { *c=sem_config; }
static void Sem_set_config(Sem_config* c) { sem_config=*c; }
static void Sem_set_fake_model(uint8_t f) { fake_model=f; }
static uint8_t Sem_query_fake_model(void) { return fake_model; }
static uint8_t Vid_get_default_num_of_threads(void) { return 2; }
static void Vid_set_volume(uint16_t v) { vid_player.volume=v; }
static void Vid_log_settings(void) {}
void vid_panel_refit_layout_refresh_tex(void) {}
bool Topos_md_boot_take_vid(char** p,uint32_t* n) { (void)p;(void)n;return false; }
uint32_t Topos_md_read_bundle(Topos_md_bundle* b) {
 reads++;memset(b,0,sizeof(*b));b->fake_model=fake_model;
 if(disk){b->vid_text=copy_text(disk);b->vid_len=(uint32_t)strlen(disk);}return 0;
}
void Topos_md_bundle_free(Topos_md_bundle* b) { free(b->vid_text);free(b->sem_text); }
uint32_t Topos_md_write_bundle(const char* s,uint32_t sn,const char* v,uint32_t vn,uint8_t f) {
 (void)s;(void)sn;(void)vn;(void)f;writes++;free(disk);disk=copy_text(v);return 0;
}
'''

TESTS = r'''
int main(void) {
 assert(!strcmp(s_root_rows[0].label,"volume"));
 assert(s_root_rows[1].id==VP_SETTING_ID_AUDIO_TRACK && !strcmp(s_root_rows[1].label,"track"));
 assert(s_root_rows[1].num_opts==4);
 for(int i=0;i<4;i++)assert(atoi(s_root_rows[1].opt_labels[i])==3-i);
 Vid_init_settings();assert(vid_player.audio_track==0 && get_track()==3);
 Vid_load_settings();assert(vid_player.audio_track==0); /* absent file */
 for(int track=0;track<4;track++) {
  vid_player.active_audio_track=1;unsigned prior_writes=writes;
  set_track(3-track);assert(vid_player.audio_track==track && get_track()==3-track);
  assert(vid_player.active_audio_track==1 && writes==prior_writes+1);
  char expected[32];snprintf(expected,sizeof(expected),"<28>%d</28>",track);
  assert(strstr(disk,expected));
  vid_player.audio_track=255;Vid_load_settings();assert(vid_player.audio_track==track);
 }
 /* The current session is immutable when the menu changes; reopen applies it. */
 set_track(0);vid_player.num_of_audio_tracks=4;open_session(4);assert(vid_player.active_audio_track==3);
 set_track(3);assert(vid_player.audio_track==0 && vid_player.active_audio_track==3);
 for(int i=0;i<4;i++)assert(accept_packet((uint8_t)i)==(i==3));
 open_session(4);assert(vid_player.active_audio_track==0);
 for(int i=0;i<4;i++)assert(accept_packet((uint8_t)i)==(i==0));
 for(int track=0;track<4;track++) {
  const uint8_t expected[4][5]={{0,0,0,0,0},{0,0,1,1,1},{0,0,0,2,2},{0,0,0,0,3}};
  set_track(3-track);
  for(int count=0;count<=4;count++) {
   open_session((uint8_t)count);assert(vid_player.active_audio_track==expected[track][count]);
   assert(vid_player.audio_track==track); /* fallback does not overwrite preference */
  }
 }
 /* Prior settings omit track: preserve older fields and default track to zero. */
 vid_player.volume=37;Vid_save_settings();char* track_tag=strstr(disk,"<28>");assert(track_tag);*track_tag=0;
 vid_player.audio_track=3;vid_player.volume=99;Vid_load_settings();
 assert(vid_player.audio_track==0 && vid_player.volume==37);
 const char* invalid[]={"4","256","-1","999999999999999999999999999999"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
  Vid_save_settings();char* tag=strstr(disk,"<28>");assert(tag);
  size_t prefix=(size_t)(tag-disk);char* bad=malloc(prefix+strlen(invalid[i])+16);
  memcpy(bad,disk,prefix);sprintf(bad+prefix,"<28>%s</28>",invalid[i]);free(disk);disk=bad;
  Vid_load_settings();assert(vid_player.audio_track==0);
 }
 unsigned r=reads,w=writes;vid_player.audio_track=2;assert(get_track()==1 && reads==r && writes==w);
 set_track(-1);set_track(4);assert(vid_player.audio_track==2 && writes==w);
 free(disk);
 puts("PASS: root row/order, default 0, all four choices, persistence round-trip, legacy/malformed settings, immutable active track, reopen/fallback, selected packet routing, in-memory UI reads");
 return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    state = (ROOT / "include/vid_state.h").read_text(encoding="utf-8")
    settings = (ROOT / "source/vid_panel_settings.c").read_text(encoding="utf-8")
    decode = (ROOT / "source/vid_decode.c").read_text(encoding="utf-8")
    # Verify the actual seek-reopen branch never reads the pending preference.
    seek = decode.split("case CONVERT_THREAD_FINISHED_CLEARING_CACHE:", 1)[1].split("//After clearing cache start seeking.", 1)[0]
    assert "vid_player.audio_track" not in seek
    assignments = re.findall(r"vid_player\.active_audio_track\s*=(?!=)", decode)
    assert len(assignments) == 1, "only a fresh playback open may choose the track"
    code = PREAMBLE + "\n" + "\n".join(re.findall(r"^#define SETTINGS_ELEMENTS_.*", state, re.M)) + "\n"
    code += function("source/system/util/util.c", "Util_parse_file")
    for name in ("Vid_init_settings", "Vid_load_settings", "Vid_save_settings"):
        code += function("source/vid_settings.c", name)
    code += settings[settings.index("static const VpSettingRowDesc s_root_rows[]"):].split("};", 1)[0] + "};\n"
    get_case = settings.split("static int get_value", 1)[1].split("case VP_SETTING_ID_AUDIO_TRACK:", 1)[1].split("case ", 1)[0]
    set_case = settings.split("static void set_value", 1)[1].split("case VP_SETTING_ID_AUDIO_TRACK:", 1)[1].split("case ", 1)[0]
    code += "static int get_track(void){" + get_case + "}\n"
    code += "static void set_track(int v){switch(VP_SETTING_ID_AUDIO_TRACK){case VP_SETTING_ID_AUDIO_TRACK:" + set_case + "}}\n"
    snapshot = re.search(r"uint8_t requested_audio_track = [^;]+;", decode).group()
    selection = re.search(r"vid_player\.active_audio_track = [^;]+;", decode).group()
    packet_gate = re.search(r"if\((vid_player.num_of_audio_tracks > packet_index && packet_index == vid_player.active_audio_track)\)", decode).group(1)
    code += "static void open_session(uint8_t num_of_audio_tracks){" + snapshot + selection + "}\n"
    code += "static bool accept_packet(uint8_t packet_index){return " + packet_gate + ";}\n" + TESTS
    with tempfile.TemporaryDirectory(prefix="topos-audio-track-") as temp:
        source = pathlib.Path(temp) / "check.c"
        executable = pathlib.Path(temp) / "check.exe"
        source.write_text(code, encoding="utf-8")
        subprocess.run([args.cc, "-std=c99", "-O2", "-Wall", "-Wextra",
                        "-I" + str(ROOT / "include"), str(source),
                        str(ROOT / "source/system/util/str.c"), "-lm", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
