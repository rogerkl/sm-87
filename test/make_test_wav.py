#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Roger Klaveness
"""A WAV for tools/mkrom.py's test: a 440 Hz sine at 44.1 kHz (not one of the sampler's rates: it is resampled), 0.5 s,
its smpl chunk's loop over the last 2000 samples and root A3 (69).  test/make_test_wav.py OUT.wav"""
import math
import struct
import sys

n, rate = 22050, 44100
data = b"".join(struct.pack("<h", int(20000 * math.sin(2 * math.pi * 440 * i / rate))) for i in range(n))
smpl = struct.pack("<9I", 0, 0, 22676, 69, 0, 0, 0, 1, 0) + struct.pack("<6I", 0, 0, n - 2000, n - 1, 0, 0)
fmt = struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16)
body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data)) + data
body += b"smpl" + struct.pack("<I", len(smpl)) + smpl
open(sys.argv[1], "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)
