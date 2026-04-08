"""
gen_banner_wav.py
生成 3DS HOME 横幅音效 → resource/banner.wav

设计意图（像「光标停到播放器图标 → 上屏出模型 + 一小段提示音」）:
  - 用 525 Hz + 630 Hz 叠成偏暖的和声（5:6，略带「就绪 / 可以播放」感），再极轻加 1050 Hz 提亮。
  - 525、630、1050 均整除 22050，合成波形周期为 210 样本 → 1 秒内 105 整节，无缝循环、无拍音。
  - 包络 sin²(π·n/(N-1))：两端趋近静音，中间最响，像一次柔和的「选中 / 展开」强调；循环首尾都是 0，接下一圈不爆音。
  - 立体声两路相同（HOME 横幅常见做法），音量适中、不刺耳。

规格: 22050 Hz, 16-bit PCM, 立体声, 1.00 s

用法: python scripts/gen_banner_wav.py
"""
import math
import os
import struct

OUT = os.path.join(os.path.dirname(__file__), "..", "resource", "banner.wav")
os.makedirs(os.path.dirname(OUT), exist_ok=True)
try:
    if os.path.isfile(OUT):
        os.remove(OUT)
except OSError:
    pass

SAMPLE_RATE = 22050
CHANNELS = 2
BPS = 16
num_samples = 22050

# 均与 22050 整数对齐: 22050/525=42, /630=35, /1050=21 → lcm 周期 210 样本
F1, F2, F3 = 525.0, 630.0, 1050.0
W1, W2, W3 = 1.0, 0.58, 0.18
PEAK = 0.46

nm1 = max(num_samples - 1, 1)

mono_samples = []
for i in range(num_samples):
    t = i / SAMPLE_RATE
    env = math.sin(math.pi * i / nm1)
    env *= env
    v = (
        W1 * math.sin(2 * math.pi * F1 * t)
        + W2 * math.sin(2 * math.pi * F2 * t)
        + W3 * math.sin(2 * math.pi * F3 * t)
    )
    v *= env * PEAK
    mono_samples.append(int(max(-32767, min(32767, round(v * 32767)))))

mono_samples[0] = 0
mono_samples[-1] = 0

stereo = []
for s in mono_samples:
    stereo.extend((s, s))

data = struct.pack(f"<{len(stereo)}h", *stereo)
data_size = len(data)
byte_rate = SAMPLE_RATE * CHANNELS * BPS // 8
block_align = CHANNELS * BPS // 8

with open(OUT, "wb") as f:
    f.write(b"RIFF")
    f.write(struct.pack("<I", 36 + data_size))
    f.write(b"WAVE")
    f.write(b"fmt ")
    f.write(struct.pack("<I", 16))
    f.write(struct.pack("<H", 1))
    f.write(struct.pack("<H", CHANNELS))
    f.write(struct.pack("<I", SAMPLE_RATE))
    f.write(struct.pack("<I", byte_rate))
    f.write(struct.pack("<H", block_align))
    f.write(struct.pack("<H", BPS))
    f.write(b"data")
    f.write(struct.pack("<I", data_size))
    f.write(data)

mx = max(abs(s) for s in mono_samples)
print(f"banner.wav: player-style chime, 525+630+1050 Hz, sin^2 envelope, 1.00 s @ {SAMPLE_RATE} Hz stereo")
print(f"  peak |sample|={mx}")
print(f"  {os.path.abspath(OUT)}")
