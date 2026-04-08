"""
生成 0.5 秒静音 WAV，用于 3DS banner 音频占位。
输出: resource/banner.wav
"""
import struct, os

OUT = os.path.join(os.path.dirname(__file__), "..", "resource", "banner.wav")
os.makedirs(os.path.dirname(OUT), exist_ok=True)

sample_rate   = 22050
num_channels  = 1
bits_per_sample = 16
duration_sec  = 0.5
num_samples   = int(sample_rate * duration_sec)
data_size     = num_samples * num_channels * (bits_per_sample // 8)

with open(OUT, "wb") as f:
    # RIFF header
    f.write(b"RIFF")
    f.write(struct.pack("<I", 36 + data_size))
    f.write(b"WAVE")
    # fmt chunk
    f.write(b"fmt ")
    f.write(struct.pack("<I", 16))           # chunk size
    f.write(struct.pack("<H", 1))            # PCM
    f.write(struct.pack("<H", num_channels))
    f.write(struct.pack("<I", sample_rate))
    f.write(struct.pack("<I", sample_rate * num_channels * bits_per_sample // 8))
    f.write(struct.pack("<H", num_channels * bits_per_sample // 8))
    f.write(struct.pack("<H", bits_per_sample))
    # data chunk
    f.write(b"data")
    f.write(struct.pack("<I", data_size))
    f.write(b"\x00" * data_size)

print(f"✅ 静音 WAV 已生成: {os.path.abspath(OUT)}")
