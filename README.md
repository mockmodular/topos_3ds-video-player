
## What's included in the release

**FFmpeg encoding scripts** — the best settings I've found for encoding SBS video for 3DS, for both H.264 and MPEG-2. Drop your SBS source in and get a file that plays correctly on both N3DS and O3DS. Also included in the release as ready-to-run `.bat` files.

**H.264 (New 3DS)**
```bat
"%FFMPEG%" -y -stats -stats_period 1 -i "%FILE_IN%" -filter_complex "[0:v]scale=iw*sar:ih,setsar=1,split[L][R];[L]crop=iw/2:ih:0:0[Lh];[R]crop=iw/2:ih:iw/2:0[Rh];[Lh]crop=min(iw\,ih*400/240):ih[Lc];[Rh]crop=min(iw\,ih*400/240):ih[Rc];[Lc][Rc]hstack[Vs];[Vs]format=yuv420p16le,zscale=w=800:h=240:m=bt709:min=bt709:filter=spline36,format=yuv420p,setsar=1[V]" -map "[V]" -map "0:a?" -sws_dither ed -c:v libx264 -preset slow -crf 14 -profile:v high -level 3.1 -fps_mode cfr -x264-params "aq-mode=3:aq-strength=1.0:qcomp=0.65:ref=4:bframes=4:no-fast-pskip=1" -color_primaries bt709 -color_trc bt709 -colorspace bt709 -c:a aac -b:a 128k -ac 2 -ar 48000 "%FILE_OUT%"
```

**MPEG-2 (Old 3DS)**
```bat
"%FFMPEG%" -y -stats -stats_period 1 -i "%FILE_IN%" -filter_complex "[0:v]split[L][R];[L]crop=iw/2:ih:0:0[Lh];[R]crop=iw/2:ih:iw/2:0[Rh];[Lh]crop=min(iw\,ih*400/240):ih[Lc];[Rh]crop=min(iw\,ih*400/240):ih[Rc];[Lc][Rc]hstack[Vs];[Vs]format=yuv420p16le,zscale=w=800:h=240:m=bt709:min=bt709:filter=spline36,format=yuv420p[V]" -map "[V]" -map "0:a?" -c:v mpeg2video -b:v 2000k -maxrate 2000k -bufsize 4000k -g 30 -bf 0 -profile:v main -level:v main -fps_mode cfr -color_primaries bt709 -color_trc bt709 -colorspace bt709 -c:a mp2 -b:a 96k -ac 2 -ar 48000 "%FILE_OUT%"
```

Both scripts handle SAR correction, aspect-ratio-safe cropping, high-quality spline36 downscaling to 800×240, and proper BT.709 color tagging. The H.264 script targets N3DS MVD hardware decode limits (High profile, Level 3.1). The MPEG-2 script is tuned for O3DS software decode headroom (2000k CBR, no B-frames).

**Test videos** — I've encoded the *Avatar: Fire and Ash* SBS trailer in both H.264 and MPEG-2 at 800×240 and included them in the release. Load them up and you can immediately verify playback on your hardware without needing to source or encode anything yourself.


---


## What it can do

**New 3DS** — H.264 SBS 3D at 800×240 @ 24 fps, hardware decoded by the MVD engine. CPU sits near zero during playback. The hardware is doing all the work.

**Old 3DS** — MPEG-2 SBS 3D at 800×240 @ 24 fps, software decoded. Runs smooth.

Both with real parallax barrier 3D, and both at full **RGB888 true color** — no more RGB565 color banding.


---


## What makes this different

### True SBS 3D that actually works

SBS content is automatically detected and the frame is split into left/right eye images for the parallax barrier. The detection is simple and reliable — if `height == 240 && width >= 640`, it's SBS. Right-eye alignment always tracks left-eye geometry perfectly. You can also switch manually: 3D / 2D / auto.

If you play mono content with the 3D slider turned up, the right eye now correctly mirrors the left instead of going black (a bug in the original).

### Full RGB888 color — everywhere

The entire render pipeline runs at RGBA8. Top screen, bottom screen, every decode path. The original used RGB565 on the top screen, which shows as color banding on gradients. This fork doesn't have that.

### N3DS: H.264 hardware decode (MVD)

H.264 files go through the 3DS's built-in MVD hardware decoder. CPU load during H.264 SBS playback is essentially zero — it's the way 3DS video playback was meant to work.

### O3DS: software decode that's actually fast

A lot of work went into making software decode competitive. FFmpeg was rebuilt from scratch with ARMv6/ARMv5TE hand-written assembly enabled — the original build had `--disable-asm`, meaning IDCT and motion compensation were running pure C on ARM11 the whole time. Thread affinity is tuned per core: Core 0 is dedicated to FFmpeg decode calls, everything else runs on Core 1. MPEG-2 gets intra-frame SLICE threading across both cores. The result is smooth MPEG-2 SBS at 800×240 @ 24 fps on O3DS.

### Hardware color conversion — zero CPU copy

For software-decoded YUV420P content, two Y2RU hardware passes write directly into left and right eye texture memory using DMA gap addressing. No CPU memcpy, no per-frame heap allocation. The previous approach was doing ~48 `linearAlloc`/`free` calls per second just for this step. Switchable in Advanced settings: CPU / Y2R / Y2R×2.


### it automatically uses nearest-neighbor or bicubic filtering.

You definitely wouldn't want a video encoded specifically for the 3DS at 400×240 or 800×240 to have bicubic filtering blindly applied during playback and degrade the image quality. Topos thoughtfully auto-detects the resolution and uses nearest-neighbor filtering instead of bicubic in the following cases — 2D mode: (y=240 and x≤400) or (x=400 and y≤240); 3D mode: (y=240 and x≤800) or (x=800 and y≤240). In all other cases, it uses proportional full-screen scaling with nearest-neighbor filtering."

### Runs clean — no slowdown over time

Seeking fully reopens the decoder instead of just flushing buffers. The original accumulated heap fragmentation during long playback sessions, which caused frame drops that got worse the longer you watched. That's fixed.

Also fixed: the heap low-memory probe was running ~3,000 times per second (each call doing a test 2.5 MB malloc + free). Now cached. `SwsContext` was being rebuilt every frame. Now created once. SBS right-eye buffer was being malloc'd and free'd every single frame. Now pre-allocated. These weren't dramatic individually, but stacked up they were quietly eating a lot of cycles.

### Bottom screen panel — fully rewritten

The top screen is always full video. No OSD, no status bar, no UI elements — ever. The video is never scaled or displaced when the UI is open.

The bottom screen panel has been rebuilt from scratch with three navigation sections: file browser, player controls, and settings. It includes:

- Live CPU usage bar charts (C0–C3 on N3DS, C0–C1 on O3DS)
- Playback diagnostics: codec, resolution, fps, decode mode, texture filter, A/V sync info
- Seek bar with time display and clear status feedback
- Error overlay for file open failures, correctly layered above all other UI

### Settings — restructured into submenus

Settings are now organized into four submenus instead of a flat list:

- **Volume** — playback volume
- **Custom** — seek step size, remember playback position, auto-dim, power saving, root directory, UI mode
- **Video** — 3D/2D/auto mode, texture filter (Bilinear / Nearest / Auto), scaling mode, left-right eye swap
- **Advanced** — hardware decode, color conversion mode (CPU / Y2R / Y2R×2), model simulation, multi-thread decode, audio/video track toggles

Settings persist to `/3ds/topos/topos_setting.txt` across reboots.


---


## Bugs fixed (partial list)

- Right eye showing black when playing mono content with 3D slider on
- SBS right-eye frame misalignment at non-standard resolutions
- Y2R texture corruption caused by DMA stride bug (disabled DMA, fixed upload path)
- `SwsContext` rebuilt every frame causing unnecessary CPU overhead
- Heap fragmentation causing progressive frame drops during long playback
- Heap probe running thousands of times per second
- Z-fighting / black screen caused by misuse of citro2d Z values (all draw calls unified to 0.5f, draw order controls layering)
- Error overlay not covering file list correctly
- Settings text disappearing near the chrome footer
- CPU usage display rendering off-screen (original was at x=360 on a 320px-wide screen)
- FFmpeg NEON mis-detection crashing ARM11 (ARM11 has no NEON; bare-metal toolchain incorrectly reported it as available)
- `receive=EAGAIN` from FFmpeg during FRAME threading startup incorrectly treated as an error
- Audio thread blocking on notification queue, consuming other threads' notifications and breaking seek/abort state machine


---


## What's been removed from the original

A lot of dead code and unused features have been cleaned out:

- Telemetry upload thread (~130 lines)
- All non-English locale files (18 files) and language-specific font-scale hacks
- Screen recording module, error log export, cURL-related code
- External font (Exfont) system — replaced with Citro2D system font
- Player language pack system (Vid_msg enum / vid.txt / Util_load_msg)
- Controls help overlay, C-Stick video panning, L/R zoom
- Bottom screen fade-out timer
- Night mode toggle and flash timer — night mode is always on
- High-RAM CIA build target
- MVD upload mode runtime switching — fixed to the optimal Unroll4 mode


---


## Notes

- Requires **devkitARM r65 (GCC 14.2.0)**. r66 / GCC 15.1 causes data abort crashes — this is a known toolchain regression, not a bug in the player.
- English only.
- This is built around the SBS 3D + clean UI use case. It's not trying to be a universal player.

Source is all on GitHub if you want to dig in. Happy to answer questions.


There are also way too many improvements and changes for me to keep track of or remember them all — please everyone test and try it out for yourselves!

except avatar fire and h trailer i have encoded Avatar: The Way of Water, Furiosa: A Mad Max Saga, Angry Birds, Puss in Boots: The Last Wish, and Zootopia. I used my GitHub release .bat script with FFmpeg to do the encoding. Any SBS source is automatically processed by center-symmetrically cropping each eye's portion of every frame to a 400:240 (5:3) ratio, then encoding it at 400x240 resolution. This means you can just drag in a 3840×1080 (full-screen) or 1920×1080 (half-screen) SBS source and run the script — no stretching will occur, because it automatically center-crops each eye's 1920×1080 region to 1800×1080 (5:3), discarding the pixels on both sides, and then scales it down to 400×240. It works great — I think it's a really excellent script.write by me 😆.I haven't directly released the video files I encoded, but you can find SBS sources on sites like btdig and encode them yourself.😆

The 3D video playback quality — especially H.264 SBS 800×240 video on the New 3DS — absolutely crushes the 3D movies available on the official 3DS eShop. And I mean crushes. On top of that, you can throw in any SBS 3D video you want. Any of them.


















readme version2

This project is based on Video_player_for_3DS by Core-2-Extreme. Many thanks to Core-2-Extreme for all their work — they are truly amazing.
https://github.com/Core-2-Extreme/Video_player_for_3DS

# topos — Video Player for 3DS

A 3DS video player built around H.264 MVD hardware decoding, true-color rendering, real SBS 3D, and a fully redesigned bottom-screen UI.

---

## Highlights

### Playback Performance

| Device | Codec | Resolution | FPS | Method | CPU load |
|--------|-------|-----------|-----|--------|----------|
| New 3DS | H.264 | 800×240 SBS | 24 | MVD hardware decode | Near zero |
| Old 3DS | MPEG-2 | 800×240 SBS | 24 | FFmpeg software decode | Smooth |

- **New 3DS** uses the MVD hardware block for H.264. The CPU barely moves while playing 800×240 24 fps SBS video.
- **Old 3DS** can smoothly software-decode MPEG-2 800×240 24 fps SBS video via an optimized multi-threaded FFmpeg pipeline.

---

## Changes from Upstream

### Decoding & Color Pipeline

- **H.264 MVD hardware decode** — single-track SBS source, output `MVD_OUTPUT_ABGR32`, full true-color path. `mvdstdGenerateDefaultConfig` output type is explicitly forced to `ABGR32` after every call (upstream left this unset).
- **Full RGB888/RGBA8 pipeline** — all textures use `GPU_RGBA8`. Upstream used `RGB565` on the top screen. `GPU_RGB8` (3-byte) is not used — it causes triple-image corruption on PICA200.
- **Y2R hardware color conversion** — new `YUV420P → Y2RU → GPU_RGBA8` path for software-decoded frames. Zero CPU copy: `Y2RU transfer_gap` writes directly into texture memory with hardware DMA. Selectable in Settings → Advanced: `CPU` (swscale), `y2r×2` (dual-pass SBS, default), `NEONy2r` (single-pass + tile-row split).
- **FFmpeg software decode thread tuning** — automatically selects `FF_THREAD_FRAME` (H.264), `FF_THREAD_SLICE` (HEVC, MPEG-2), or single-thread (AV1/dav1d). Fixed `receive=EAGAIN` pipeline-fill false error for frame-threaded codecs. FFmpeg built with `--disable-neon` to prevent ARM11 crash on O3DS.

### SBS 3D

- **Auto SBS detection**: `h == 240 && w >= 640` triggers SBS mode, SAR forced to 1:1.
- **Left/right eye split** — CPU row-by-row memcpy for the linear path; Y2R `stride-split` hardware path for the Y2R path.
- **Parallax barrier 3D** — right-eye frame follows left-eye geometry via `Vid_fit_to_screen`.
- 3D/2D/Auto mode selectable in Settings.

### Performance Optimizations

- **`__wrap_malloc` heap-probe cache** — upstream called `__real_malloc(2.5 MB)` + `free` on *every* malloc to check heap headroom (~3000 probes/sec during decode). Now cached with a 100 ms refresh → ~10 probes/sec.
- **SwsContext/SwrContext created once per stream** — upstream rebuilt the swscale context every frame (30 times/sec). Now created at stream-open, reused each frame, freed at stream-close.
- **Y2R_X2 zero alloc/free** — old SBS Y2R path did `free + linearAlloc` 48 times/sec. New direct-write path allocates nothing per frame.
- **Seek re-opens decoder** — seeking triggers a decoder restart (`video_player.c:~5466`) to release fragmented heap blocks and prevent playback degrading over time.
- **Diagnostic values cached 1 s** — `mallinfo` and CPU usage queries are cached statically and refreshed once per second, not every frame.

### Thread Scheduling

- **New 3DS**: decode_video_thread on C0; FFmpeg workers rotate across C1/C2/C3 starting from C1 (`102102102` order).
- **Old 3DS**: All main threads (decode, convert, read_packet, audio_decode) on C0; FFmpeg workers on C1.
- **Audio decode separated** — audio PCM conversion, volume scaling, and ndsp output moved to a dedicated `Vid_audio_decode_thread` on C1, decoupled from the main decode pipeline.

### UI Redesign

- **Top screen is always full-video** — no OSD, no status bar overlaid on the top screen. Opening a file no longer forces full-screen mode.
- **Bottom screen three-panel UI** (new, `vid_panel.c`): **Player**, **File Browser**, **Settings** — switchable via Y / Start / B.
- **Settings panel** — all options navigable by D-pad + A/B. Submenus: LCD (3D/2D/Auto, brightness), Video (SBS mode), Advanced (HW color conv, power saving, fake-model).
- **File browser** — touch or D-pad navigation, long-press acceleration, touch flash feedback, error overlay when a file cannot be opened.
- **Night mode fixed on** — light mode and flash timer removed.
- **Eco mode fixed on** — renders only when watch variables change or `Draw_set_refresh_needed` is set; no busy-loop.
- **No automatic sleep prevention** — headphone-inserted sleep block removed; sleep policy is controlled solely by the system lid.
- **Top-screen UI bar removed** — `Draw_top_ui()` calls deleted from menu.c and sem.c (time, WiFi, green-leaf indicators gone).
- **citro2d z-value**: all draw calls use `z = 0.5f`; draw order controls layering (z is unreliable on PICA200 — `GPU_GEQUAL` but `z=0.0f` overwrites everything).

### Settings Menu (sem) Cleanup

- **Control submenu removed** — scroll speed hard-coded to `0.5`.
- **LCD off / sleep timers removed** — both hard-coded to `0` (disabled).
- **CPU usage graph removed from top screen** — moved to bottom-screen diagnostic area.
- **Night / Flash buttons removed** — `is_night` permanently `true`.
- **Non-English message files removed** — only `_en` locale kept.

### Dead Code & Feature Removal

- **Telemetry threads removed** — `Menu_send_app_info_thread` / `Menu_update_thread` (~130 lines) deleted.
- **C-stick move / L·R zoom removed** — `move_content_mode`, `VID_CMD_SE0_MOVE_MODE`, size_changes logic all deleted.
- **Controls overlay removed** — `is_displaying_controls`, `control.t3x` texture, related commands deleted.
- **Multi-language support removed** — 18 non-English message files deleted; language-specific font-size hacks removed.
- **`high_ram` CIA variant removed** — build outputs a single `.cia`.

### Architecture Refactoring

`video_player.c` (originally ~5135 lines) has been fully decomposed into focused modules:

| Module | Responsibility |
|--------|---------------|
| `vid_state.h` | Shared enums / structs / constants, no logic |
| `vid_settings.c` | Settings init / load / save |
| `vid_texture.c` | GPU texture allocation, upload, crop |
| `vid_sync.c` | A/V desync tracking |
| `vid_screen.c` | Fit-to-screen, zoom, brightness, full-screen transitions |
| `vid_hid.c` | HID → command queue (input side only) |
| `vid_cmd.c` | Ring-buffer command queue (`VidCmdId`) |
| `vid_input.c` | Command dispatcher (`Vid_process_hid_cmd_queue`) |
| `vid_seek_engine.c` | Seek state machine, progress-bar geometry |
| `vid_worker.c` | Decode statistics, frame timing, decode/convert threads |
| `vid_decode.c` | Decode pipeline: read-packet, decode, audio-decode threads |
| `vid_draw.c` | Main render loop (`Vid_main`), init/exit screen |
| `vid_lifecycle.c` | `Vid_init` / `Vid_exit` / global state; `video_player.c` deleted |

---

## Build

Requires **devkitARM r65 (GCC 14.2.0)**. r66 / GCC 15.1 causes a data abort crash at runtime.

```
build.bat   (double-click from project root)
```


Releasing topos, a heavily modified fork of Core-2-Extreme's Video_player_for_3DS. After fixing more bugs than I care to count and pushing the hardware about as far as I think it can go, it's ready to share.
https://github.com/mockmodular/topos_3ds-video-player/releases/tag/v0.11.0
https://github.com/mockmodular/topos_3ds-video-player/tree/v0.11.0
What's included in the release
FFmpeg encoding scripts — the best settings I've found for encoding SBS video for 3DS, for both H.264 and MPEG-2. Drop your SBS source in and get a file that plays correctly on both N3DS and O3DS. Also included in the release as ready-to-run .bat files.


**H.264 (New 3DS)**
```bat
"%FFMPEG%" -y -stats -stats_period 1 -i "%FILE_IN%" ^
-filter_complex "[0:v]scale=iw*sar:ih,setsar=1,split[L][R];
[L]crop=iw/2:ih:0:0[Lh];[R]crop=iw/2:ih:iw/2:0[Rh];
[Lh]crop=min(iw\,ih*400/240):ih[Lc];[Rh]crop=min(iw\,ih*400/240):ih[Rc];
[Lc][Rc]hstack[Vs];
[Vs]format=yuv420p16le,zscale=w=800:h=240:m=bt709:min=bt709:filter=spline36,format=yuv420p,setsar=1[V]" ^
-map "[V]" -map "0:a?" ^
-c:v libx264 -preset slow -crf 14 -profile:v high -level 3.1 -fps_mode cfr ^
-x264-params "aq-mode=3:aq-strength=1.0:qcomp=0.65:ref=4:bframes=4:no-fast-pskip=1" ^
-color_primaries bt709 -color_trc bt709 -colorspace bt709 ^
-c:a aac -b:a 128k -ac 2 -ar 48000 "%FILE_OUT%"
```

**MPEG-2 (Old 3DS)**
```bat
"%FFMPEG%" -y -stats -stats_period 1 -i "%FILE_IN%" ^
-filter_complex "[0:v]split[L][R];
[L]crop=iw/2:ih:0:0[Lh];[R]crop=iw/2:ih:iw/2:0[Rh];
[Lh]crop=min(iw\,ih*400/240):ih[Lc];[Rh]crop=min(iw\,ih*400/240):ih[Rc];
[Lc][Rc]hstack[Vs];
[Vs]format=yuv420p16le,zscale=w=800:h=240:m=bt709:min=bt709:filter=spline36,format=yuv420p[V]" ^
-map "[V]" -map "0:a?" ^
-c:v mpeg2video -b:v 2000k -maxrate 2000k -bufsize 4000k ^
-g 30 -bf 0 -profile:v main -level:v main -fps_mode cfr ^
-color_primaries bt709 -color_trc bt709 -colorspace bt709 ^
-c:a mp2 -b:a 96k -ac 2 -ar 48000 "%FILE_OUT%"
```


Both scripts handle SAR correction, aspect-ratio-safe cropping, high-quality spline36 downscaling to 800×240, and proper BT.709 color tagging. The H.264 script targets N3DS MVD hardware decode limits (High profile, Level 3.1). The MPEG-2 script is tuned for O3DS software decode headroom (2000k CBR, no B-frames).
Test videos — I've encoded the Avatar: Fire and Ash SBS trailer in both H.264 and MPEG-2 at 800×240 and included them in the release. Load them up and you can immediately verify playback on your hardware without needing to source or encode anything yourself.

What it can do
New 3DS — H.264 SBS 3D at 800×240 @ 24 fps, hardware decoded by the MVD engine. CPU sits near zero during playback. The hardware is doing all the work.
Old 3DS — MPEG-2 SBS 3D at 800×240 @ 24 fps, software decoded. Runs smooth.
Both with real parallax barrier 3D, and both at full RGB888 true color — no more RGB565 color banding.

What makes this different
True SBS 3D that actually works
SBS content is automatically detected and the frame is split into left/right eye images for the parallax barrier. The detection is simple and reliable — if height == 240 && width >= 640, it's SBS. Right-eye alignment always tracks left-eye geometry perfectly. If you play mono content with the 3D slider turned up, the right eye now correctly mirrors the left instead of going black (a bug in the original).
Full RGB888 color — everywhere
The entire render pipeline runs at RGBA8. Top screen, bottom screen, every decode path. The original used RGB565 on the top screen, which shows as color banding on gradients. This fork doesn't have that.
N3DS: H.264 hardware decode (MVD)
H.264 files go through the 3DS's built-in MVD hardware decoder. CPU load during H.264 SBS playback is essentially zero — it's the way 3DS video playback was meant to work.
O3DS: software decode that's actually fast
A lot of work went into making software decode competitive. FFmpeg was rebuilt from scratch with ARMv6/ARMv5TE hand-written assembly enabled — the original build had --disable-asm, meaning IDCT and motion compensation were running pure C on ARM11 the whole time. Thread affinity is tuned per core: Core 0 is dedicated to FFmpeg decode calls, everything else runs on Core 1. MPEG-2 gets intra-frame SLICE threading across both cores. The result is smooth MPEG-2 SBS at 800×240 @ 24 fps on O3DS.
Hardware color conversion — zero CPU copy
For software-decoded YUV420P content, two Y2RU hardware passes write directly into left and right eye texture memory using DMA gap addressing. No CPU memcpy, no per-frame heap allocation. The previous approach was doing ~48 linearAlloc/free calls per second just for this step.
Runs clean — no slowdown over time
Seeking fully reopens the decoder instead of just flushing buffers. The original accumulated heap fragmentation during long playback sessions, which caused frame drops that got worse the longer you watched. That's fixed.
Also fixed: the heap low-memory probe was running ~3,000 times per second (each call doing a test 2.5 MB malloc + free). Now cached. SwsContext was being rebuilt every frame. Now created once. SBS right-eye buffer was being malloc'd and free'd every single frame. Now pre-allocated. These weren't dramatic individually, but stacked up they were quietly eating a lot of cycles.
Clean interface
The top screen is always full video. No OSD, no status bar, no UI elements. Everything — file browser, settings, playback info — lives on the bottom screen, and opening the UI never scales or moves the video.
The interface has been stripped back intentionally. What's here works, and it's not cluttered.

Bugs fixed (partial list)

Right eye showing black when playing mono content with 3D slider on
SBS right-eye frame misalignment at non-standard resolutions
Y2R texture corruption caused by DMA stride bug (disabled DMA, fixed upload path)
SwsContext rebuilt every frame causing unnecessary CPU overhead
Heap fragmentation causing progressive frame drops during long playback
Heap probe running thousands of times per second
Z-fighting / black screen caused by misuse of citro2d Z values (all draw calls unified to 0.5f, draw order controls layering)
Error overlay not covering file list correctly
Settings text disappearing near the chrome footer
CPU usage display rendering off-screen (original was at x=360 on a 320px-wide screen)
FFmpeg NEON mis-detection crashing ARM11 (ARM11 has no NEON; bare-metal toolchain incorrectly reported it as available)
receive=EAGAIN from FFmpeg during FRAME threading startup incorrectly treated as an error
Audio thread blocking on notification queue, consuming other threads' notifications and breaking seek/abort state machine


Notes

Requires devkitARM r65 (GCC 14.2.0). r66 / GCC 15.1 causes data abort crashes — this is a known toolchain regression, not a bug in the player.
English only. Non-English locale files have been removed.
Some original features are gone: C-Stick video panning, L/R zoom, the Controls help overlay, bottom screen fade-out timer, and the telemetry upload thread.
This is built around the SBS 3D + clean UI use case. It's not trying to be a universal player.

Source is all on GitHub if you want to dig in. Happy to answer questions.


There are also way too many improvements and changes for me to keep track of or remember them all — please everyone test and try it out for yourselves!
