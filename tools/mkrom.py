#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Roger Klaveness
"""Samples built into the firmware: sounds there from the start (no sampler or computer to send them from), and more
sample memory than the pool alone (the README's "Getting the most samples in").

They go into a firmware package (.fwsc) as it is, a release's or your own build's, with no compiler: the package is
opened, the samples written above the firmware in its slot (the sample bank, app/s700/s7bank.h), and its checksums made
again (documentation/sample-bank.md). The package is changed in place unless -o names another file; --none takes the
samples out again, and gives the package back as it was built, byte for byte.

  .venv/bin/python tools/mkrom.py PACKAGE.fwsc FILE[@SLOT] ...   the samples into the package (slots 1-16; without
                                                                 @, the next free one)
  .venv/bin/python tools/mkrom.py PACKAGE.fwsc [FILE ...] --kit KIT.txt
                                                                 a drum kit too, replacing the firmware's
  .venv/bin/python tools/mkrom.py PACKAGE.fwsc                   what it holds, and the room
  .venv/bin/python tools/mkrom.py PACKAGE.fwsc --none            without samples (and the firmware's own kit)
  .venv/bin/python tools/mkrom.py --emu FILE[@SLOT] ... [--kit KIT.txt]
                                                                 for the host simulator and the browser emulator
  .venv/bin/python tools/mkrom.py --emu --none

FILE is
  - a WAV file (PCM 8/16/24/32 bit or float, mono or mixed down to mono). Axs7's WAV files carry their settings in an
    "axs7" chunk (START, END, the loop, scan mode, the sound, the keygroups in the 32 programs): they are taken as
    they are. Otherwise the smpl chunk's loop and root key; the sample alone in program SLOT, keys C0-D#6 (24-99).
    A rate that isn't one of the sampler's 41 is resampled to the nearest one (windowed sinc).
  - an S700/X7000 QuickDisk image (.qd, as a Gotek or HxC uses them): the sample and all its parameters and programs
    (documentation/akai-s700-x7000/05-quickdisk.md).

KIT.txt is a drum kit's list in resources/kit/kit.txt's form (tools/mkkit.py), its WAV files beside it: up to 64
sounds and 16 pads. It replaces the kit the firmware was built with, sounds and pads; without --kit the firmware's own
plays. Each run makes the whole: samples and kit as it is told.

Up to 16 samples, of up to 32,752 words each, packed two words to three bytes, and the kit's sounds the same, in what
the firmware leaves of its 581,564-byte slot (the package says how much: run it with the package alone).

--emu writes app/rom/emu_bank.h instead (not in the repository): the host simulator and the browser emulator, which
have no firmware slot, build it in while it exists.
"""
import argparse
import gzip
import json
import math
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
EMU_OUT = ROOT / "app" / "rom" / "emu_bank.h"
FM1_TOOLS = ROOT / "fm1-base" / "tools"     # fm1pkg_make.py: the package's ciphers, CRC-16 and layout
MAXWORDS = 32752
NPARAM = 155
# the SPRM values (app/s700/s7.h)
P_UNDEF, P_RPMODE, P_SSTRT, P_LEN1, P_SEND, P_SLOOP, P_LEN2 = 128, 129, 130, 132, 134, 136, 138
P_LFSP, P_LFDP, P_LFDL, P_FLTFRQ, P_RLEASE, P_LDNSS, P_SFNTUN, P_RECNP, P_RECBW, P_RVRSE = range(140, 150)
P_VELFIL, P_VELLDN, P_VELRLS = 150, 151, 152
MODES = {"off": 1, "forward": 2, "alternating": 4, "drum": 9}
# the sampler's periods (ns) for rate codes 36..76
PERIODS = [254817, 240515, 227016, 214275, 202248, 190897, 180183, 170070, 160525, 151515, 143011, 134985, 127409,
           120258, 113508, 107137, 101124, 95449, 90091, 85035, 80262, 75758, 71506, 67492, 63704, 60129, 56754,
           53569, 50562, 47724, 45046, 42518, 40131, 37879, 35753, 33746, 31852, 30064, 28377, 26784, 25281]
RATES = {36 + i: 1e9 / p for i, p in enumerate(PERIODS)}


def default_params():
    """an empty slot's (the X7000's test tone's sound), in no program"""
    p = []
    for _ in range(32):
        p += [24, 0, 0, 0x80 | 60]
    p += [2, 2] + [0] * 10 + [50, 0, 64, 99, 30, 75, 0, 60, 48, 0, 0, 50, 0, 0, 0]
    assert len(p) == NPARAM
    return p


def setword(p, k, v):
    p[k] = v & 0xFF
    p[k + 1] = (v >> 8) & 0xFF


def word(p, k):
    return p[k] | p[k + 1] << 8


def set_points(p, start, end, loop, mode):
    """START, END + 1, the loop, the scan mode, and the two words the sampler keeps"""
    end = max(1, end)
    start = min(start, end - 1)
    loop = min(loop, end - 1)
    p[P_RPMODE] = mode
    setword(p, P_SSTRT, start)
    setword(p, P_SEND, end)
    setword(p, P_LEN1, end - start - 1)
    setword(p, P_SLOOP, loop)
    setword(p, P_LEN2, end - loop - 1)


def nearest_code(rate):
    return min(RATES, key=lambda c: abs(math.log(rate / RATES[c])))


def resample(x, src, dst):
    """windowed sinc (a Blackman window, 32 zero crossings), low-passed below the lower Nyquist"""
    ratio = dst / src
    n = int(len(x) * ratio)
    cutoff = min(1.0, ratio) * 0.95
    half = 16
    t = np.arange(n) / ratio
    out = np.zeros(n)
    base = np.floor(t).astype(np.int64)
    for k in range(-half + 1, half + 1):
        idx = base + k
        d = t - idx
        w = np.where(np.abs(d) < half, 0.42 + 0.5 * np.cos(np.pi * d / half) + 0.08 * np.cos(2 * np.pi * d / half), 0)
        h = cutoff * np.sinc(cutoff * d) * w
        valid = (idx >= 0) & (idx < len(x))
        out += np.where(valid, x[np.clip(idx, 0, len(x) - 1)], 0.0) * h
    return out


def to_words(x):
    """-1..1 floats to 12-bit offset binary"""
    return np.clip(np.round(x * 2047.0) + 2048, 0, 4095).astype(np.int64)


# ------------------------------------------------------------------------------------------------------- WAV ---
def read_wav(path):
    b = Path(path).read_bytes()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a WAV file")
    chunks, i = {}, 12
    while i + 8 <= len(b):
        cid, n = b[i:i + 4], struct.unpack("<I", b[i + 4:i + 8])[0]
        chunks.setdefault(cid, b[i + 8:i + 8 + n])
        i += 8 + n + (n & 1)
    fmt = chunks[b"fmt "]
    tag, ch, rate, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    if tag == 0xFFFE:
        tag = struct.unpack("<H", fmt[24:26])[0]
    raw = chunks[b"data"]
    if tag == 3 and bits == 32:
        x = np.frombuffer(raw[:len(raw) // 4 * 4], "<f4").astype(np.float64)
    elif tag == 1 and bits == 8:
        x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128) / 128
    elif tag == 1 and bits == 16:
        x = np.frombuffer(raw[:len(raw) // 2 * 2], "<i2") / 32768.0
    elif tag == 1 and bits == 24:
        a = np.frombuffer(raw[:len(raw) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((a[:, 0] | a[:, 1] << 8 | a[:, 2] << 16) << 8 >> 8) / 8388608.0
    elif tag == 1 and bits == 32:
        x = np.frombuffer(raw[:len(raw) // 4 * 4], "<i4") / 2147483648.0
    else:
        raise SystemExit(f"{path}: WAV format {tag}, {bits} bits: not read")
    x = x[:len(x) // ch * ch].reshape(-1, ch).mean(axis=1)
    smpl = chunks.get(b"smpl")
    meta = json.loads(chunks[b"axs7"].rstrip(b"\0")) if b"axs7" in chunks else None
    return x, rate, smpl, meta


def from_wav(path, slot):
    x, rate, smpl, meta = read_wav(path)
    p = default_params()
    code = nearest_code(rate)
    ratio = 1.0
    if abs(1200 * math.log2(rate / RATES[code])) > 1:   # not one of the sampler's rates: to the nearest
        x = resample(x, rate, RATES[code])
        ratio = RATES[code] / rate
    p[P_RECBW] = code
    n = len(x)
    s = (meta or {}).get("settings") or {}
    pressure = 0
    if s:
        start, end = int(s.get("start", 0) * ratio), int(s.get("end", n) * ratio)
        ls, le = int(s.get("loopStart", 0) * ratio), int(s.get("loopEnd", end) * ratio)
        mode = MODES.get(s.get("loopMode"), 1)
        if s.get("reverse"):          # the sampler keeps a reversed sample as it plays
            x = x[::-1]
            start, end, ls, le = n - end, n - start, n - le, n - ls
        snd = s.get("sound") or {}
        for key, k in (("level", P_LDNSS), ("velLevel", P_VELLDN), ("filter", P_FLTFRQ), ("velFilter", P_VELFIL),
                       ("release", P_RLEASE), ("velRelease", P_VELRLS), ("lfoSpeed", P_LFSP), ("lfoDepth", P_LFDP),
                       ("lfoDelay", P_LFDL)):
            if key in snd:
                p[k] = max(0, min(99, int(round(snd[key]))))
        pressure = max(0, min(99, int(round(snd.get("pressure", 0)))))
        kgs = s.get("keygroups") or []
    else:
        start, end, ls, le, mode, kgs = 0, n, 0, n, 1, []
        if smpl and len(smpl) >= 36 + 24 and struct.unpack("<I", smpl[28:32])[0]:
            unity = struct.unpack("<I", smpl[12:16])[0]
            ltype, lstart, lend = struct.unpack("<III", smpl[36 + 4:36 + 16])
            ls, le = int(lstart * ratio), int((lend + 1) * ratio)   # smpl's end is the loop's last sample
            mode = 4 if ltype == 1 else 2
            kgs = [{"program": slot, "low": 24, "high": 99, "root": unity}]
        elif smpl and len(smpl) >= 16:
            kgs = [{"program": slot, "low": 24, "high": 99, "root": struct.unpack("<I", smpl[12:16])[0]}]
    if mode in (2, 4):                # the sampler's loop runs to END: a looped sample ends at its loop end
        end = min(end, le)
    end = min(end, MAXWORDS, len(x))
    if not kgs:
        kgs = [{"program": slot, "low": 24, "high": 99, "root": 60}]
    root = max(24, min(96, int(kgs[0].get("root", 60))))
    p[P_RECNP] = root
    p[P_SFNTUN] = int(round(kgs[0].get("cents", 0) / 6.25)) & 0xFF if abs(kgs[0].get("cents", 0)) <= 100 else 0
    for kg in kgs:
        prog = int(kg.get("program", 0))
        if not 0 <= prog < 32:
            continue
        # one root a sample on the sampler: another keygroup's root as its program's transpose
        tr = int(kg.get("semitones", 0)) + root - int(kg.get("root", root))
        tr = max(-60, min(60, tr))
        p[4 * prog] = max(0, min(99, int(kg.get("low", 0))))
        p[4 * prog + 1] = max(0, min(99, int(kg.get("high", 99))))
        p[4 * prog + 2] = (tr & 0x7F) | (0x80 if kg.get("fixedPitch") else 0)
    set_points(p, min(start, end - 1), end, min(ls, end - 1), mode if (mode not in (2, 4) or le - ls >= 2) else 1)
    return to_words(x[:end]), p, pressure


# -------------------------------------------------------------------------------------------- QuickDisk ---
def qd_block(path):
    """the S700 block of a .qd image: MFM cells, the sync bytes and marker, the first block whose CRC holds"""
    b = Path(path).read_bytes()
    if path.endswith(".gz"):
        b = gzip.decompress(b)
    if b[3:5] != b"QD":
        raise SystemExit(f"{path}: not a QuickDisk image")
    off, length = struct.unpack("<II", b[0x200:0x208])
    track = np.frombuffer(b[off:off + length], np.uint8)
    cells = np.unpackbits(track, bitorder="little")
    # the sync bytes and marker as cells, without the first clock cell (it depends on the bit before)
    pat, last = [], 0
    for byte in [0x16] * 7 + [0xA5]:
        for k in range(8):
            bit = byte >> k & 1
            pat += [1 if bit == 0 and last == 0 else 0, bit]
            last = bit
    cb, pb = cells.tobytes(), bytes(pat[1:])
    hits, at = [], cb.find(pb)
    while at >= 0:
        hits.append(at)
        at = cb.find(pb, at + 1)
    for at in hits:
        start = at - 1
        data = cells[start + 1:start + 1 + 16 * 0xC0C4:2]
        if len(data) < 8 * 0xC0C4:
            continue
        block = np.packbits(data[:8 * 0xC0C4], bitorder="little").tobytes()
        if block[8:24] != b"S700 FORMAT 0.00":
            continue
        crc = 0
        for byte in block[7:]:
            crc ^= byte
            for _ in range(8):
                crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
        if crc == 0:
            return block
    raise SystemExit(f"{path}: no S700 block with a good CRC")


def from_qd(path):
    blk = qd_block(path)
    disk = list(blk[0x28:0xC2])         # 154 values: SPRM's without its undefined one (k 128)
    p = disk[:128] + [2] + disk[128:]
    assert len(p) == NPARAM
    nib, hi = blk[0xC2:0xC2 + 0x4000], blk[0x40C2:0x40C2 + 0x8000]
    end = min(word(p, P_SEND), MAXWORDS)
    words = np.array([(hi[i] << 4) | (nib[i >> 1] >> 4 if i & 1 else nib[i >> 1] & 15) for i in range(end)])
    return words, p, 0


# ------------------------------------------------------------------------------------------------ output ---
def pack(words):
    """two words to three bytes (app/s700/s7.h), and the pad byte"""
    w = list(words) + ([0] if len(words) & 1 else [])
    out = bytearray()
    for i in range(0, len(w), 2):
        a, b = int(w[i]), int(w[i + 1])
        out += bytes([a & 0xFF, (a >> 8) | (b & 15) << 4, b >> 4])
    return bytes(out + b"\0")


def packed_size(words):
    """S7_PACKED (app/s700/s7.h): what pack() makes of so many words"""
    return (words + 1) // 2 * 3 + 1


def c_bytes(b):
    return ",\n".join(", ".join(f"0x{x:02X}" for x in b[i:i + 24]) for i in range(0, len(b), 24))


def c_name(s):
    return "".join(ch if 32 <= ord(ch) < 127 and ch not in '"\\' else "?" for ch in s)[:BANK_NAME - 1]


# ------------------------------------------------------------------------------------------- the bank ---
# app/s700/s7bank.h
BANK_MAGIC = 0x4B423753           # "S7BK"
BANK_VERSION = 2
BANK_NAME = 24
BANK_ENTRY = 188                  # <BBHI24s, the 155 values, a spare byte
BANK_SOUND = 16                   # a kit's sound: <HBBI8s
BANK_PAD = 16                     # a kit's pad: <8sBbBbBBBB
BANK_FOOT = 32
KIT_SOUNDS, KIT_PADS, KIT_NAME = 64, 16, 6
RATE_LO, RATE_HI = 36, 76
DESC_MAGIC = b"SM87 S7 BANK"      # s7_bank_desc_t, in the firmware's image
PAD_KEYS = ("sound", "tune", "level", "pan", "note", "group", "poly")
PAD_RANGE = {"tune": (-24, 24), "level": (0, 99), "pan": (-3, 3), "note": (0, 127), "group": (0, 9), "poly": (1, 2)}


def check_kit(kit):
    """a kit as bank() takes it: {"sounds": [{name, words, rate, vel}], "pads": [{name, sound, tune, level, pan,
    note, group, poly}]}; SystemExit for one the firmware wouldn't take"""
    sounds, pads = kit["sounds"], kit["pads"]
    if len(sounds) > KIT_SOUNDS or len(pads) > KIT_PADS:
        raise SystemExit(f"a kit of {len(sounds)} sounds and {len(pads)} pads: at most {KIT_SOUNDS} and {KIT_PADS}")
    for what in sounds + pads:
        if len(what["name"]) > KIT_NAME or not what["name"].isascii():
            raise SystemExit(f"the kit's {what['name']}: a name of up to {KIT_NAME} characters")
    for s in sounds:
        if not 1 <= len(s["words"]) <= MAXWORDS or not RATE_LO <= s["rate"] <= RATE_HI or not 0 <= s["vel"] <= 99:
            raise SystemExit(f"the kit's sound {s['name']}: its words, rate code or vel")
    for q in pads:
        if not (q["sound"] == 0xFF or 0 <= q["sound"] < len(sounds) + 16):
            raise SystemExit(f"the kit's pad {q['name']}: sound {q['sound']}")
        for k, (lo, hi) in PAD_RANGE.items():
            if not lo <= q[k] <= hi:
                raise SystemExit(f"the kit's pad {q['name']}: {k} {q[k]}: {lo}..{hi}")


def bank(items, kit=None):
    """the bank's bytes, its footer last: items (slot, name, words, params, pressure), sorted by slot; a kit (check_kit)
    or None"""
    sounds, pads = (kit["sounds"], kit["pads"]) if kit else ([], [])
    if kit:
        check_kit(kit)
    head, data = bytearray(), bytearray()
    at = len(items) * BANK_ENTRY + len(sounds) * BANK_SOUND + len(pads) * BANK_PAD

    def put(words):
        packed = pack(words)
        where = at + len(data)
        data.extend(packed + b"\0" * (-len(packed) % 4))
        return where

    for slot, name, words, p, pressure in items:
        if not 1 <= len(words) <= MAXWORDS:   # (as sm87bank.js checkWords: the firmware takes no bank with such a one)
            raise SystemExit(f"{name}: {len(words)} words: a sample has 1..{MAXWORDS}")
        head += struct.pack("<BBHI24s", slot, pressure, len(words), put(words), c_name(name).encode())
        head += bytes(p) + b"\0"
    for snd in sounds:
        head += struct.pack("<HBBI8s", len(snd["words"]), snd["rate"], snd["vel"], put(snd["words"]),
                            snd["name"].encode())
    for q in pads:
        head += struct.pack("<8sBbBbBBBB", q["name"].encode(), *(q[k] for k in PAD_KEYS), 0)
    body = bytes(head + data)
    foot = struct.pack("<IHHIIHH8x", BANK_MAGIC, BANK_VERSION, len(items), len(body), zlib.crc32(body), len(sounds),
                       len(pads))
    return body + foot + struct.pack("<I", zlib.crc32(foot))


def bank_size(top):
    """the bytes the bank ending at the end of top takes, footer and all, by its footer alone (0: none; SystemExit for
    a damaged footer): one whose contents don't check (bank_read) can still be taken out or replaced"""
    if len(top) < BANK_FOOT:
        return 0
    f = top[-BANK_FOOT:]
    magic, _, _, size, _, _, _ = struct.unpack_from("<IHHIIHH", f)
    if magic != BANK_MAGIC:
        return 0
    if zlib.crc32(f[:28]) != struct.unpack_from("<I", f, 28)[0] or size & 3 or size > len(top) - BANK_FOOT:
        raise SystemExit("the package's samples don't check (a damaged footer)")
    return size + BANK_FOOT


def bank_read(top):
    """the bank ending at the end of top (bytes), checked as the firmware checks it (s7bank.c): {"samples": [(slot,
    name, words, params, pressure)], "kit": {"sounds": [...], "pads": [...]} or None, "size": its bytes} with words a
    count; None without one; SystemExit for one that doesn't check"""
    if len(top) < BANK_FOOT:
        return None
    f = top[-BANK_FOOT:]
    magic, version, count, size, crc, nsnd, npad = struct.unpack_from("<IHHIIHH", f)
    if magic != BANK_MAGIC:
        return None
    head = count * BANK_ENTRY + nsnd * BANK_SOUND + npad * BANK_PAD
    if zlib.crc32(f[:28]) != struct.unpack_from("<I", f, 28)[0] or version != BANK_VERSION or count > 16 \
            or nsnd > KIT_SOUNDS or npad > KIT_PADS or size & 3 or size > len(top) - BANK_FOOT or head > size:
        raise SystemExit("the package's samples don't check (a damaged footer)")
    body = top[-BANK_FOOT - size:-BANK_FOOT]
    if zlib.crc32(body) != crc:
        raise SystemExit("the package's samples don't check (their CRC)")

    def bad(what):
        raise SystemExit(f"the package's samples don't check ({what})")

    def words_in(at, words):
        return 1 <= words <= MAXWORDS and at >= head and not at & 3 and at <= size and packed_size(words) <= size - at

    out = []
    for i in range(count):
        slot, pressure, words, at, name = struct.unpack_from("<BBHI24s", body, i * BANK_ENTRY)
        if slot >= 16 or not words_in(at, words) or name[-1]:
            bad(f"sample {i + 1}")
        p = list(body[i * BANK_ENTRY + 32:i * BANK_ENTRY + 32 + NPARAM])
        out.append((slot, name.rstrip(b"\0").decode("latin-1"), words, p, pressure))
    sounds, pads, o = [], [], count * BANK_ENTRY
    for i in range(nsnd):
        words, rate, vel, at, name = struct.unpack_from("<HBBI8s", body, o + i * BANK_SOUND)
        if not words_in(at, words) or not RATE_LO <= rate <= RATE_HI or vel > 99 or any(name[KIT_NAME:]):
            bad(f"the kit's sound {i + 1}")
        sounds.append({"name": name.rstrip(b"\0").decode("latin-1"), "words": words, "rate": rate, "vel": vel})
    o += nsnd * BANK_SOUND
    for i in range(npad):
        name, *v = struct.unpack_from("<8sBbBbBBBB", body, o + i * BANK_PAD)
        q = dict(zip(PAD_KEYS, v))
        if any(name[KIT_NAME:]) or not (q["sound"] == 0xFF or q["sound"] < nsnd + 16) or \
                any(not lo <= q[k] <= hi for k, (lo, hi) in PAD_RANGE.items()):
            bad(f"the kit's pad {i + 1}")
        pads.append({"name": name.rstrip(b"\0").decode("latin-1"), **q})
    return {"samples": out, "kit": {"sounds": sounds, "pads": pads} if nsnd or npad else None,
            "size": size + BANK_FOOT}


def load_kit(path):
    """a kit list (resources/kit/kit.txt's form: tools/mkkit.py) and its WAV files -> a kit for bank()"""
    if str(Path(__file__).resolve().parent) not in sys.path:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
    import mkkit                  # (it imports this: here, once both are loaded)
    sounds, pads = mkkit.parse(path)
    out = []
    for snd in sounds:
        f = Path(path).parent / snd["file"]
        if not f.exists():
            raise SystemExit(f"{snd['at']}: {f}: no such file")
        words, code, _ = mkkit.load(f)
        out.append({"name": snd["name"], "words": words, "rate": code, "vel": snd["vel"]})
    return {"sounds": out, "pads": [{"name": q["name"], **{k: q[k] for k in PAD_KEYS}} for q in pads]}


# ---------------------------------------------------------------------------------------- the package ---
MARKS, MARK_BLK, MARK_KEEP = 20, 0x30, 0x2F   # .fwsc: a marker byte after each of the first 20 blocks: its identity
APP_AREA = 0x4000                 # flash.bin: the app area from here (SFC-ciphered with the chip key)
APP_NAME = b"app.bin"


def fm1pkg():
    if str(FM1_TOOLS) not in sys.path:
        sys.path.insert(0, str(FM1_TOOLS))
    import fm1pkg_make
    return fm1pkg_make


class Package:
    """a firmware package (fm1-base tools/fm1pkg_make.py) opened: its app.bin plain (self.app), the firmware's image
    in it (self.image_end) and its slot (len(self.app)); close() makes the package again with the app changed"""

    def __init__(self, raw, name="the package"):
        P = fm1pkg()
        self.P, self.name, self.raw = P, name, bytes(raw)
        if len(raw) < MARKS * MARK_BLK + 0x40:
            self.fail("too short")
        self.identity = "".join(chr((raw[i * MARK_BLK + MARK_KEEP] - i - 1) & 0xFF) for i in range(MARKS)
                                if raw[i * MARK_BLK + MARK_KEEP] != 0x7D)
        lg = bytearray(b"".join(raw[i * MARK_BLK:i * MARK_BLK + MARK_KEEP] for i in range(MARKS))
                       + raw[MARKS * MARK_BLK:])
        hdr = self.dec(lg[0:0x40])
        n = struct.unpack_from("<H", hdr, 8)[0]
        if self.crc(hdr[2:0x40]) != struct.unpack_from("<H", hdr, 0)[0] or not n \
                or self.crc(lg[0x40:0x40 + n * 0x50]) != struct.unpack_from("<H", hdr, 2)[0]:
            self.fail("its header doesn't check")
        for i in range(n):           # the UFW parts: flash.bin is type 0
            e = self.dec(lg[0x40 + i * 0x50:0x90 + i * 0x50])
            if struct.unpack_from("<H", e, 0)[0] == 0:
                self.part = i
                off, size = struct.unpack_from("<II", e, 8)
                break
        else:
            self.fail("no flash.bin in it")
        flash = bytearray(lg[off:off + size])
        if self.crc(flash) != struct.unpack_from("<H", e, 4)[0]:
            self.fail("its flash.bin doesn't check")
        self.lg, self.off = lg, off
        self.key = self.chip_key(flash)
        # the app area: its head (a JLFS entry: the block's size), the entries after it (app.bin's), the app
        area = bytearray(flash[APP_AREA:APP_AREA + 32])
        P.sfc(area, 0, 32, 0, self.key)
        if self.crc(area[2:32]) != struct.unpack_from("<H", area, 0)[0]:
            self.fail(f"its app area doesn't open with the chip key {self.key:#06x}")
        blk = struct.unpack_from("<I", area, 8)[0]
        self.span = (blk + 31) & ~31              # whole cipher blocks: ciphered back the same
        if APP_AREA + self.span > len(flash):
            self.fail("its app area is bigger than its flash.bin")
        region = bytearray(flash[APP_AREA:APP_AREA + self.span])
        P.sfc(region, 0, self.span, 0, self.key)
        if self.crc(region[32:blk]) != struct.unpack_from("<H", area, 2)[0]:
            self.fail("its app area doesn't check")
        for k in range(32, 0x120, 32):
            if region[k + 16:k + 16 + len(APP_NAME) + 1] == APP_NAME + b"\0":
                self.entry = k
                break
        else:
            self.fail("no app.bin in its app area")
        a_off, a_size = struct.unpack_from("<II", region, self.entry + 4)
        self.flash, self.region, self.blk, self.a_off = flash, region, blk, a_off
        self.app = bytearray(region[a_off:a_off + a_size])
        self.desc()

    def fail(self, why):
        raise SystemExit(f"{self.name}: not a firmware package this can put samples into: {why}")

    def crc(self, b):
        return self.P.crc16(bytes(b))

    def dec(self, b):
        b = bytearray(b)
        self.P.enc(b, 0, len(b))
        return b

    def chip_key(self, flash):
        """from the flash's head: the isd_config.ini entry's blob (fm1pkg_make.flash_image)"""
        for o in range(32, 0x200, 32):
            e = self.dec(flash[o:o + 32])
            if e[16:32].rstrip(b"\0\xff") == b"isd_config.ini":
                at = struct.unpack_from("<I", e, 4)[0]
                return self.P.chipkey_decode(bytes(flash[at:at + 32]))
        self.fail("no chip key in its head")

    def desc(self):
        """the firmware's s7_bank_desc_t: where its image ends"""
        at = self.app.find(DESC_MAGIC)
        if at < 0:
            self.fail("its firmware doesn't take samples this way (an SM-87 from before the sample bank, or another "
                      "firmware)")
        version, me, load, start, end, slot_end = struct.unpack_from("<6I", self.app, at + 12)
        base = me - at
        self.image_end = load + (end - start) - base
        if slot_end - base != len(self.app) or not at < self.image_end <= len(self.app) or self.image_end & 3:
            self.fail("its firmware's description of itself doesn't fit its slot")
        if version != BANK_VERSION:
            raise SystemExit(f"{self.name}: its firmware reads samples of format {version}, this mkrom.py writes "
                             f"format {BANK_VERSION}: use the mkrom.py of the firmware's version")

    def room(self):
        return len(self.app) - self.image_end

    def contents(self):
        """what it holds (bank_read), None for none"""
        return bank_read(bytes(self.app[self.image_end:]))

    def put(self, b):
        """the bank b into the slot (b"" for none): above the image all 0xFF, b at the slot's top"""
        top = bytes(self.app[self.image_end:])
        size = bank_size(top)     # (not bank_read: samples that don't check are replaced as any others)
        if any(x != 0xFF for x in top[:len(top) - size]):
            self.fail("there's something above its firmware that isn't samples")
        if len(b) > self.room():
            raise SystemExit(f"{len(b) / 1024:.1f} KB of samples: more than the {self.room() / 1024:.1f} KB the "
                             f"firmware leaves in {self.name}")
        self.app[self.image_end:] = b"\xFF" * (self.room() - len(b)) + b

    def close(self):
        """the package's bytes, the app as it is now"""
        P, region, flash, lg = self.P, bytearray(self.region), bytearray(self.flash), bytearray(self.lg)
        region[self.a_off:self.a_off + len(self.app)] = self.app
        e = self.entry
        struct.pack_into("<H", region, e + 2, self.crc(self.app))
        struct.pack_into("<H", region, e, self.crc(region[e + 2:e + 32]))
        struct.pack_into("<H", region, 2, self.crc(region[32:self.blk]))
        struct.pack_into("<H", region, 0, self.crc(region[2:32]))
        P.sfc(region, 0, self.span, 0, self.key)
        flash[APP_AREA:APP_AREA + self.span] = region
        lg[self.off:self.off + len(flash)] = flash
        at = 0x40 + self.part * 0x50
        ent = self.dec(lg[at:at + 0x50])
        struct.pack_into("<H", ent, 4, self.crc(flash))
        lg[at:at + 0x50] = self.dec(ent)
        hdr = self.dec(lg[0:0x40])
        n = struct.unpack_from("<H", hdr, 8)[0]
        struct.pack_into("<H", hdr, 2, self.crc(lg[0x40:0x40 + n * 0x50]))
        struct.pack_into("<H", hdr, 0, self.crc(hdr[2:0x40]))
        lg[0:0x40] = self.dec(hdr)
        raw = bytearray(self.raw)
        for i in range(MARKS):
            raw[i * MARK_BLK:i * MARK_BLK + MARK_KEEP] = lg[i * MARK_KEEP:(i + 1) * MARK_KEEP]
        raw[MARKS * MARK_BLK:] = lg[MARKS * MARK_KEEP:]
        return bytes(raw)


# ----------------------------------------------------------------------------------------------- main ---
def load_items(files):
    """FILE[@SLOT] ... -> [(slot, name, words, params, pressure)] by slot"""
    fixed = {int(x.rsplit("@", 1)[1]) - 1 for x in files if "@" in x and x.rsplit("@", 1)[1].isdigit()}
    taken, items = set(), []
    for spec in files:
        path, _, slot = spec.rpartition("@") if "@" in spec and spec.rsplit("@", 1)[1].isdigit() else (spec, "", "")
        if slot:
            slot = int(slot) - 1
            if not 0 <= slot < 16 or slot in taken:
                raise SystemExit(f"{spec}: slot {slot + 1} is not free (1-16)")
        else:
            slot = next((s for s in range(16) if s not in taken and s not in fixed), None)
            if slot is None:
                raise SystemExit("more than 16 samples")
        taken.add(slot)
        words, p, pressure = from_qd(path) if path.lower().endswith((".qd", ".qd.gz")) else from_wav(path, slot)
        items.append((slot, Path(path).stem, words, p, pressure))
    return sorted(items, key=lambda it: it[0])


def show(items, kit=None):
    for s, n, w, p, _ in items:
        words = w if isinstance(w, int) else len(w)
        progs = [q + 1 for q in range(32) if p[4 * q] <= p[4 * q + 1]]
        print(f"slot {s + 1:2}  {n:23.23}  {words:5} words  {RATES[p[P_RECBW]]:7.0f} Hz  root {p[P_RECNP]}  "
              f"mode {p[P_RPMODE]}  programs {','.join(map(str, progs)) or '-'}")
    if not kit:
        return
    names = [snd["name"] for snd in kit["sounds"]]
    for i, snd in enumerate(kit["sounds"]):
        words = snd["words"] if isinstance(snd["words"], int) else len(snd["words"])
        print(f"kit sound {i + 1:2}  {snd['name']:6}  {words:5} words  {RATES[snd['rate']]:7.0f} Hz  vel {snd['vel']}")
    for i, q in enumerate(kit["pads"]):
        snd = q["sound"]
        what = names[snd] if snd < len(names) else f"SLOT{snd - len(names) + 1:02}" if snd != 0xFF else "-----"
        print(f"kit pad {i + 1:2}  {q['name'] or '-':6}  {what:6}  tune {q['tune']:+d}  level {q['level']}  "
              f"pan {q['pan']:+d}  note {q['note']}" + (f"  group {q['group']}" if q["group"] else "")
              + (f"  poly {q['poly']}" if q["poly"] > 1 else ""))


def kb(n):
    return f"{n / 1024:.1f} KB"


def held(items, kit):
    what = [f"{len(items)} samples"] if items or not kit else []
    if kit:
        what.append(f"a kit of {len(kit['sounds'])} sounds and {len(kit['pads'])} pads")
    return " and ".join(what)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="PACKAGE.fwsc, then FILE[@SLOT] ...: WAV or .qd")
    ap.add_argument("-o", "--out", type=Path, help="the package to write (default: PACKAGE itself)")
    ap.add_argument("--kit", type=Path, help="a drum kit to replace the firmware's: a list as resources/kit/kit.txt, "
                    "its WAV files beside it")
    ap.add_argument("--none", action="store_true", help="no samples: the package as built (--emu: no emu_bank.h)")
    ap.add_argument("--emu", action="store_true", help=f"{EMU_OUT.relative_to(ROOT)} for the host simulator and "
                    "the browser emulator, not a package")
    a = ap.parse_args()
    if a.none and (a.kit or (a.files[1:] if not a.emu else a.files)):
        ap.error("--none takes no samples or kit")
    if a.emu:
        if a.none:
            EMU_OUT.unlink(missing_ok=True)
            print(f"removed {EMU_OUT.relative_to(ROOT)}: the next host or emulator build has no samples built in")
            return
        if not a.files and not a.kit:
            ap.error("no files")
        items = load_items(a.files)
        kit = load_kit(a.kit) if a.kit else None
        b = bank(items, kit)
        lines = ["/* Samples built in for the host simulator and the browser emulator (the device's are in its",
                 " * package): made by tools/mkrom.py --emu, do not edit. From:"]
        lines += [f" *   slot {s + 1:2}: {c_name(n)} ({len(w)} words, rate code {p[P_RECBW]})"
                  for s, n, w, p, _ in items]
        lines += [f" *   kit: {a.kit}"] if a.kit else []
        lines += [" */", f"static const uint8_t S7_EMU_BANK[{len(b)}] __attribute__((aligned(4))) = {{", c_bytes(b),
                  "};", ""]
        EMU_OUT.parent.mkdir(parents=True, exist_ok=True)
        EMU_OUT.write_text("\n".join(lines))
        show(items, kit)
        print(f"{EMU_OUT.relative_to(ROOT)}: {held(items, kit)}, {kb(len(b))}")
        return
    if not a.files or not a.files[0].lower().endswith(".fwsc"):
        ap.error("the first is the package (.fwsc), or --emu")
    src, files = Path(a.files[0]), a.files[1:]
    pkg = Package(src.read_bytes(), src.name)
    slot = len(pkg.app)
    about = (f"{pkg.identity}: the firmware {pkg.image_end:,} of the slot's {slot:,} bytes, "
             f"{kb(pkg.room())} for samples")
    if not files and not a.kit and not a.none:
        c = pkg.contents()
        print(f"{src}: {about}")
        if c:
            show(c["samples"], c["kit"])
            print(f"{held(c['samples'], c['kit'])}, {kb(c['size'])}, {kb(pkg.room() - c['size'])} free"
                  + ("" if c["kit"] else "; the firmware's own kit"))
        else:
            print("no samples; the firmware's own kit")
        return
    items = [] if a.none else load_items(files)
    kit = load_kit(a.kit) if a.kit else None
    b = bank(items, kit) if items or kit else b""
    pkg.put(b)
    out = a.out or src
    out.write_bytes(pkg.close())
    show(items, kit)
    print(f"{out}: {about}" + (f"; {held(items, kit)} in, {kb(len(b))}, {kb(pkg.room() - len(b))} free"
                                if b else "; no samples") + ("" if kit else "; the firmware's own kit"))


if __name__ == "__main__":
    sys.exit(main())
