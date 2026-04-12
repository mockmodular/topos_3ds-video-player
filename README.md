Releasing **topos**, a deep fork of [Core_2_Extreme Video_player_for_3DS](https://github.com/Core-2-Extreme/Video_player_for_3DS). This is not a small-patch fork — almost every core module has been rewritten, simplified, or replaced. The architecture, render pipeline, thread model, and UI have all been redesigned from the ground up. After fixing more bugs than I care to count and pushing the hardware about as far as I think it can go, it's ready to share.

GitHub:
https://github.com/mockmodular/topos_3ds-video-player/releases/tag/v0.12.0
https://github.com/mockmodular/topos_3ds-video-player/tree/v0.12.0




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












