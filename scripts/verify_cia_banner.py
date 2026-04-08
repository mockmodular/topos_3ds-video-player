#!/usr/bin/env python3
"""
verify_cia_banner.py — sanity-check banner audio
Usage:
  python scripts/verify_cia_banner.py path/to/file.cia
  python scripts/verify_cia_banner.py path/to/banner.bnr

Checks align with 3dbrew CBMD/BCWAV notes (2 channels, audio <= 3s, loop, CWAV magic).
"""
from __future__ import annotations

import struct
import sys


def u32(b: bytes, o: int) -> int:
    return struct.unpack_from("<I", b, o)[0]


def align64(v: int) -> int:
    m = v % 0x40
    return v if m == 0 else v + (0x40 - m)


def find_ncch_and_exefs(cia: bytes) -> tuple[int, int]:
    hdr_sz = u32(cia, 0)
    cert_sz = u32(cia, 8)
    tick_sz = u32(cia, 0xC)
    tmd_sz = u32(cia, 0x10)
    cont_off = align64(align64(align64(align64(hdr_sz) + cert_sz) + tick_sz) + tmd_sz)
    exefs_off_mu = u32(cia, cont_off + 0x1A0)
    exefs_abs = cont_off + exefs_off_mu * 0x200
    return cont_off, exefs_abs


def exefs_file(cia: bytes, exefs: int, name: str) -> tuple[int, int] | None:
    for i in range(10):
        base = exefs + i * 0x10
        n = cia[base : base + 8].split(b"\x00", 1)[0].decode("ascii", "replace")
        if n == name:
            off = u32(cia, base + 8)
            sz = u32(cia, base + 12)
            return exefs + 0x200 + off, sz
    return None


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: python scripts/verify_cia_banner.py <file.cia|banner.bnr>")
        return 2
    path = sys.argv[1]
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        print(f"ERROR: cannot read {path}: {e}")
        return 1

    print(f"File: {path} ({len(raw)} bytes)\n")

    if raw[:4] == b"CBMD":
        chunk = raw
    else:
        cia = raw
        cont, exefs = find_ncch_and_exefs(cia)
        if cia[cont + 0x100 : cont + 0x104] != b"NCCH":
            print(f"WARN: NCCH magic not at 0x{cont:X} (unexpected CIA layout?)")

        banner = exefs_file(cia, exefs, "banner")
        if not banner:
            print("FAIL: ExeFS has no 'banner' file.")
            return 1
        b_abs, b_sz = banner
        chunk = cia[b_abs : b_abs + b_sz]
    if chunk[:4] != b"CBMD":
        print(f"FAIL: banner does not start with CBMD (got {chunk[:4]!r})")
        return 1

    cwav_rel = u32(chunk, 0x84)
    cwav_abs = cwav_rel
    if cwav_rel >= len(chunk):
        print("FAIL: CBMD cwav offset out of range")
        return 1
    cw = chunk[cwav_abs:]
    if cw[:4] != b"CWAV":
        print(f"FAIL: no CWAV at CBMD+0x{cwav_rel:X} (got {cw[:4]!r})")
        return 1

    hdr_sz = struct.unpack_from("<H", cw, 6)[0]
    info_off = hdr_sz
    if info_off + 0x20 > len(cw):
        print("FAIL: truncated CWAV INFO")
        return 1

    enc = cw[info_off + 8]
    loop = cw[info_off + 9]
    sr = u32(cw, info_off + 0x0C)
    lstart = u32(cw, info_off + 0x10)
    lend = u32(cw, info_off + 0x14)
    ch = u32(cw, info_off + 0x1C)

    ok = True
    print("CBMD: OK (magic CBMD)")
    print(f"CWAV: encoding={enc} (want 1=PCM16)  loop={loop} (0=one-shot from -a WAV, 1=from makecwav -l)")
    print(f"      sampleRate={sr} Hz  channels={ch}  loopStart={lstart} loopEnd={lend}")
    sec = lend / sr if sr else 0
    print(f"      implied duration from loopEnd/sampleRate ~ {sec:.3f}s (must be <= 3)")

    if enc != 1:
        print("  !! PCM16 strongly recommended for banner")
        ok = False
    if ch != 2:
        print("  !! 3dbrew: CBMD BCWAV total channels must be 2")
        ok = False
    if loop not in (0, 1):
        print("  !! unexpected loop flag")
        ok = False
    if lend > sr * 3:
        print("  !! audio longer than 3s may break playback")
        ok = False

    data_magic = cw.find(b"DATA", 0, min(len(cw), 0x200))
    if data_magic < 0:
        print("WARN: DATA block not found in first 0x200 bytes of CWAV")
    else:
        dsz = u32(cw, data_magic + 4)
        pcm_bytes = dsz - 8 if dsz > 8 else 0
        samples = pcm_bytes // (2 * ch) if ch else 0
        print(f"DATA chunk size field={dsz}  ~{samples} PCM16 frames/channel")

    print()
    if ok:
        print("SUMMARY: binary checks passed. If hardware is still silent:")
        print("  - Test on real hardware (Citra often has no/limit banner audio).")
        print("  - Uninstall title, reboot, reinstall (clear HOME icon cache).")
        print("  - Try 2D PNG banner: make BANNER_USE_FLAT_PNG=1 clean && make")
        print("  - System volume up; HOME: parental controls can limit software.")
        print("  - Luma: disable per-game patching for this title as a test.")
        print("  - Install any CIA you know has banner sound; if that is silent too, it is console/settings.")
        print("  - Some guides: dump dsp firm to sdmc:/3ds/dspfirm.cdc (DSP1) if other homebrew audio is broken.")
    else:
        print("SUMMARY: fix the issues marked with !! then rebuild banner.bnr / CIA.")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
