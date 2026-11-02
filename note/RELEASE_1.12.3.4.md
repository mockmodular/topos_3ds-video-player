# 1.12.3.4

## Direct decoded-frame input for Y2R x2

- Software-decoded YUV420P frames can now feed Y2R x2 directly in both 2D and side-by-side 3D playback, eliminating the intermediate YUV buffer allocation and frame copy.
- Transfer the AVFrame reference to the conversion thread and retain it until hardware conversion completes or cancellation is acknowledged. Use the actual plane addresses and row strides.
- Keep compatibility with input layouts that require copying. Preserve the existing RGB output fallback for 2D playback with 1024-wide textures while still avoiding the YUV input copy.
- Represent each contiguous decoded-image allocation with one correctly sized AVBufferRef and a single release callback.

## Audio track selection

- Add `track` immediately below `volume` in the root settings menu, with choices `3`, `2`, `1`, and `0`. The default is `0`.
- Save the preferred track in persistent settings. Existing settings files default to track `0`.
- Apply the current in-memory preference when opening a video. If the requested track does not exist, use track `0` without overwriting the preference.
- Changing the setting does not switch the current playback session. Reopening the same video starts it from the beginning with the new selection; seeking retains the session's active track.
- Use the active track's audio parameters for speaker setup, buffer timing, and codec display.

## Validation

- Built and linked the Nintendo 3DS application and generated the CIA package.
- Passed host checks for frame-reference lifetime, DMA addressing, conversion error handling, audio-track routing, settings round-trips, and compatibility with older settings files.
- Passed the existing CIA package structure checks.

## Download

- `topos.cia`
