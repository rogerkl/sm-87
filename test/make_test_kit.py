#!/usr/bin/env python3
"""A drum kit for the tests (tools/mkkit.py's input): four WAV files made here, at the sampler's 26,400 Hz, and a kit
list as tools/kit.txt's (the crash's sound tuned down 3 for the ride).  test/make_test_kit.py DIR  (writes DIR/kit.txt)"""
import math
import random
import struct
import sys
from pathlib import Path

RATE = 26400


def wav(path, x):
    data = b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 30000)))) for v in x)
    fmt = struct.pack("<HHIIHH", 1, 1, RATE, RATE * 2, 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data)) + data
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def main():
    d = Path(sys.argv[1])
    d.mkdir(parents=True, exist_ok=True)
    rnd = random.Random(87)
    n = lambda s: int(s * RATE)   # noqa: E731
    wav(d / "kick.wav", [math.sin(2 * math.pi * 60 * i / RATE) * math.exp(-i / n(0.05)) for i in range(n(0.2))])
    wav(d / "snare.wav", [rnd.uniform(-1, 1) * math.exp(-i / n(0.04)) for i in range(n(0.15))])
    wav(d / "hat.wav", [rnd.uniform(-1, 1) * math.exp(-i / n(0.01)) for i in range(n(0.05))])
    wav(d / "cymbal.wav", [rnd.uniform(-1, 1) * math.exp(-i / n(0.15)) for i in range(n(0.4))])
    (d / "kit.txt").write_text("""sound KICK  kick.wav
sound SNARE snare.wav
sound HAT   hat.wav
sound CYMBL cymbal.wav
pad KICK  KICK  note=36
pad SNARE SNARE note=38
pad CLHAT HAT   note=42 group=1
pad OPHAT HAT   note=46 group=1 tune=-2
pad CRASH CYMBL note=49 poly=2
pad RIDE  CYMBL note=51 tune=-3 poly=2
""")


if __name__ == "__main__":
    main()
