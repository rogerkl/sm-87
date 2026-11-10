#!/usr/bin/env python3
"""Samples built into the firmware: for a test build, or an FM-1 with no sampler or computer to send samples from.

Writes app/rom/rom_samples.h (not in the repository: the sounds are yours, or a library's). While it exists, the
build has those samples in its slots as the factory's: a slot nothing was sent to plays them, a dump sent to it
replaces them. Delete it (or run with --none) for a build without.

  .venv/bin/python tools/mkrom.py FILE[@SLOT] ...        (slots 1-16; without @, the next free one)
  .venv/bin/python tools/mkrom.py --none

FILE is
  - a WAV file (PCM 8/16/24/32 bit or float, mono or mixed down to mono). Axs7's WAV files carry their settings in an
    "axs7" chunk (START, END, the loop, scan mode, the sound, the keygroups in the 32 programs): they are taken as
    they are. Otherwise the smpl chunk's loop and root key; the sample alone in program SLOT, keys C0-D#6 (24-99).
    A rate that isn't one of the sampler's 41 is resampled to the nearest one (windowed sinc).
  - an S700/X7000 QuickDisk image (.qd, as a Gotek or HxC uses them): the sample and all its parameters and programs
    (documentation/akai-s700-x7000/05-quickdisk.md).

Up to 16 samples, of up to 32,752 words each, packed two words to three bytes, in --max-kb (KiB) of the firmware's
581,564-byte slot: by default what the firmware itself (about 150 KiB) and the drum kit built in (app/rom/hr_kit.h,
tools/mkkit.py) leave.
"""
import argparse
import gzip
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "app" / "rom" / "rom_samples.h"
KIT = ROOT / "app" / "rom" / "hr_kit.h"
APP_SLOT = 0x8DFBC                # fm1-base tools/fm1pkg_make.py
FIRMWARE = 150 * 1024             # the firmware without samples or kit, with room to grow
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


def c_bytes(b):
    return ",\n".join(", ".join(f"0x{x:02X}" for x in b[i:i + 24]) for i in range(0, len(b), 24))


def c_name(s):
    return "".join(ch if 32 <= ord(ch) < 127 and ch not in '"\\' else "?" for ch in s)[:24]


def kit_bytes():
    """the drum kit's sounds built in (tools/mkkit.py), bytes"""
    if not KIT.exists():
        return 0
    return sum(int(n) for n in re.findall(r"HR_KIT_D\d+\[(\d+)\]", KIT.read_text()))


def main():
    room = (APP_SLOT - FIRMWARE - kit_bytes()) // 1024
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="FILE[@SLOT]: WAV or .qd")
    ap.add_argument("-o", "--out", type=Path, default=OUT)
    ap.add_argument("--max-kb", type=int, default=room,
                    help=f"room for the samples (now {room}: the slot less the firmware and the drum kit)")
    ap.add_argument("--none", action="store_true", help="remove the generated file: a build without samples")
    a = ap.parse_args()
    if a.none:
        a.out.unlink(missing_ok=True)
        print(f"removed {a.out}: the next build has no samples built in")
        return
    if not a.files:
        ap.error("no files")
    taken, items = set(), []
    for spec in a.files:
        path, _, slot = spec.rpartition("@") if "@" in spec and spec.rsplit("@", 1)[1].isdigit() else (spec, "", "")
        if slot:
            slot = int(slot) - 1
            if not 0 <= slot < 16 or slot in taken:
                raise SystemExit(f"{spec}: slot {slot + 1} is not free (1-16)")
        else:
            slot = next((s for s in range(16) if s not in taken and s not in
                         {int(x.rsplit('@', 1)[1]) - 1 for x in a.files if '@' in x and x.rsplit('@', 1)[1].isdigit()}),
                        None)
            if slot is None:
                raise SystemExit("more than 16 samples")
        taken.add(slot)
        lower = path.lower()
        words, p, pressure = from_qd(path) if lower.endswith((".qd", ".qd.gz")) else from_wav(path, slot)
        items.append((slot, Path(path).stem, words, p, pressure))
    items.sort()
    total = sum(len(pack(w)) for _, _, w, _, _ in items)
    if total > a.max_kb * 1024:
        raise SystemExit(f"{total // 1024} KB of samples: more than --max-kb {a.max_kb}"
                         + (f" (the drum kit takes {kit_bytes() // 1024} KB)" if kit_bytes() else ""))
    blob = b"".join(bytes([s]) + bytes(p) + pack(w) for s, _, w, p, _ in items)
    rom_id = zlib.crc32(blob) or 1
    lines = ["/* Samples built into the firmware: made by tools/mkrom.py, do not edit. From:"]
    lines += [f" *   slot {s + 1:2}: {c_name(n)} ({len(w)} words, rate code {p[P_RECBW]})" for s, n, w, p, _ in items]
    lines += [" */", f"#define S7_ROM_N {len(items)}u", f"#define S7_ROM_ID 0x{rom_id:08X}u", ""]
    for i, (s, n, w, p, pr) in enumerate(items):
        lines += [f"static const uint8_t S7_ROM_D{i}[{len(pack(w))}] = {{", c_bytes(pack(w)), "};"]
    lines += ["", "static const s7_rom_t S7_ROM[S7_ROM_N] = {"]
    for i, (s, n, w, p, pr) in enumerate(items):
        lines += [f'    {{{s}u, {pr}u, {len(w)}u, S7_ROM_D{i}, "{c_name(n)}",', "     {" + c_bytes(bytes(p)) + "}},"]
    lines += ["};", ""]
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text("\n".join(lines))
    for s, n, w, p, _ in items:
        progs = [q + 1 for q in range(32) if p[4 * q] <= p[4 * q + 1]]
        print(f"slot {s + 1:2}  {n:32.32}  {len(w):5} words  {RATES[p[P_RECBW]]:7.0f} Hz  root {p[P_RECNP]}  "
              f"mode {p[P_RPMODE]}  programs {','.join(map(str, progs)) or '-'}")
    print(f"{a.out}: {len(items)} samples, {total // 1024} KB")


if __name__ == "__main__":
    sys.exit(main())
