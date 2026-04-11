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


There have been so many changes and simplifications that I can no longer keep track of them all.
