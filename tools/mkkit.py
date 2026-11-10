#!/usr/bin/env python3
"""The HR-16's drum kit built into the firmware, from resources/kit/kit.txt and the WAV files beside it.

The build makes it (app/fm1.toml: gen = mkkit.py --out {gen}/hr_kit.h): a header of the kit's sounds and its pads'
set-up, for app/hr16/hrkit.c. Run by hand, it tells what the kit holds and what it takes of the firmware's slot:

  .venv/bin/python tools/mkkit.py                       (resources/kit/kit.txt)
  .venv/bin/python tools/mkkit.py KIT [--dir DIR] [--out FILE]

The sounds are the sampler's format: mono, 12 bits, packed two words to three bytes, at one of its 41 rates (a WAV at
another is resampled to the nearest). They share the firmware's slot with the samples tools/mkrom.py builds in.
"""
import argparse
import re
import sys
import zlib
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mkrom  # noqa: E402  (the WAV reading, the rates, the packing)

ROOT = Path(__file__).resolve().parent.parent
KIT = ROOT / "resources" / "kit" / "kit.txt"
NAME = 6
PADS = 16


def parse(path):
    sounds, pads = [], []
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        kind, args = words[0], words[1:]
        opts = dict(a.split("=", 1) for a in args if "=" in a)
        plain = [a for a in args if "=" not in a]
        where = f"{path}:{n}"
        if kind == "sound" and len(plain) == 2:
            sounds.append({"name": plain[0], "file": plain[1], "vel": int(opts.get("vel", 60)), "at": where})
        elif kind == "pad" and len(plain) == 2:
            pads.append({"name": plain[0], "sound": plain[1], "tune": int(opts.get("tune", 0)),
                         "level": int(opts.get("level", 85)), "pan": int(opts.get("pan", 0)),
                         "note": int(opts.get("note", 0)), "group": int(opts.get("group", 0)),
                         "poly": int(opts.get("poly", 1)), "at": where})
        else:
            raise SystemExit(f"{where}: not a sound or a pad: {line.strip()}")
    names = [s["name"] for s in sounds]
    for s in sounds:
        if len(s["name"]) > NAME or not s["name"].isascii():
            raise SystemExit(f"{s['at']}: {s['name']}: a name of up to {NAME} characters")
        if names.count(s["name"]) > 1:
            raise SystemExit(f"{s['at']}: {s['name']} twice")
        if not 0 <= s["vel"] <= 99:
            raise SystemExit(f"{s['at']}: vel 0..99")
    if len(pads) > PADS:
        raise SystemExit(f"{path}: more than {PADS} pads")
    for p in pads:
        if p["sound"] not in names:
            raise SystemExit(f"{p['at']}: no sound {p['sound']}")
        if len(p["name"]) > NAME:
            raise SystemExit(f"{p['at']}: {p['name']}: a name of up to {NAME} characters")
        for k, lo, hi in (("tune", -24, 24), ("level", 0, 99), ("pan", -3, 3), ("note", 0, 127), ("group", 0, 9),
                          ("poly", 1, 2)):
            if not lo <= p[k] <= hi:
                raise SystemExit(f"{p['at']}: {k} {p[k]}: {lo}..{hi}")
        p["sound"] = names.index(p["sound"])
    return sounds, pads


def load(path):
    """the WAV as the sampler's words, at the nearest of its rates"""
    x, rate, _, _ = mkrom.read_wav(path)
    code = mkrom.nearest_code(rate)
    if abs(1200 * np.log2(rate / mkrom.RATES[code])) > 1:
        x = mkrom.resample(x, rate, mkrom.RATES[code])
    x = x[:mkrom.MAXWORDS]
    return mkrom.to_words(x), code, float(np.max(np.abs(x))) if len(x) else 0.0


def kit_bytes(kit=KIT, wav_dir=None):
    """the bytes the kit's sounds take in the firmware (tools/mkrom.py's budget)"""
    sounds, _ = parse(kit)
    d = Path(wav_dir) if wav_dir else Path(kit).parent
    return sum(len(mkrom.pack(load(d / s["file"])[0])) for s in sounds)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("kit", nargs="?", default=str(KIT))
    ap.add_argument("--dir", help="where the kit's WAV files are (the kit list's folder unless told)")
    ap.add_argument("-o", "--out", type=Path, help="the header to write (the build's: {gen}/hr_kit.h)")
    ap.add_argument("-q", "--quiet", action="store_true", help="one line, not the kit's every sound and pad")
    a = ap.parse_args()
    wav_dir = Path(a.dir) if a.dir else Path(a.kit).parent
    sounds, pads = parse(a.kit)
    if not sounds:
        raise SystemExit(f"{a.kit}: no sounds")
    lines = [f"/* The HR-16's drum kit, built into the firmware: made by tools/mkkit.py from {Path(a.kit).name}, do not",
             " * edit. The sounds:"]
    body, total, blob = [], 0, b""
    for i, s in enumerate(sounds):
        f = wav_dir / s["file"]
        if not f.exists():
            raise SystemExit(f"{s['at']}: {f}: no such file")
        words, code, peak = load(f)
        packed = mkrom.pack(words)
        s.update(words=len(words), code=code, peak=peak)
        total += len(packed)
        blob += packed
        lines.append(f" *   {i}: {s['name']:6} {s['file']} ({len(words)} words, {mkrom.RATES[code]:.0f} Hz)")
        body += [f"static const uint8_t HR_KIT_D{i}[{len(packed)}] = {{", mkrom.c_bytes(packed), "};"]
    lines += [" */", f"#define HR_KIT_N {len(sounds)}u", f"#define HR_KIT_PADS {len(pads)}u",
              f"#define HR_KIT_ID 0x{zlib.crc32(blob) or 1:08X}u", ""]
    lines += body
    lines += ["", "static const hr_kit_sound_t HR_KIT[HR_KIT_N] = {"]
    for i, s in enumerate(sounds):
        lines.append(f'    {{HR_KIT_D{i}, {s["words"]}u, {s["code"]}u, {s["vel"]}u, "{s["name"]}"}},')
    lines += ["};", ""]
    if pads:
        lines.append("static const hr_kit_pad_t HR_KIT_PAD[HR_KIT_PADS] = {")
        for p in pads:
            lines.append(f'    {{"{p["name"]}", {{{p["sound"]}u, {p["tune"]}, {p["level"]}u, {p["pan"]}, {p["note"]}u, '
                         f'{p["group"]}u, {p["poly"]}u, 0u}}}},')
        lines += ["};", ""]
    else:
        lines += ["static const hr_kit_pad_t HR_KIT_PAD[1];", ""]
    if a.out:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        text = "\n".join(lines)
        if not a.out.exists() or a.out.read_text() != text:   # (unchanged: left as it is, nothing rebuilt)
            a.out.write_text(text)
    if a.quiet:
        print(f"kit      {len(sounds)} sounds, {len(pads)} pads, {total / 1024:.1f} KB ({Path(a.kit).name})")
        return
    for i, s in enumerate(sounds):
        db = 20 * np.log10(s["peak"]) if s["peak"] > 0 else -99
        print(f"sound {i}  {s['name']:6}  {s['words']:6} words  {mkrom.RATES[s['code']]:7.0f} Hz  "
              f"{s['words'] / mkrom.RATES[s['code']]:5.2f} s  peak {db:5.1f} dBFS")
    for i, p in enumerate(pads):
        print(f"pad {i + 1:2}  {p['name']:6}  {sounds[p['sound']]['name']:6}  tune {p['tune']:+d}  note {p['note']}"
              + (f"  group {p['group']}" if p["group"] else "") + (f"  poly {p['poly']}" if p["poly"] > 1 else ""))
    print(f"{len(sounds)} sounds, {total / 1024:.1f} KB of the firmware's slot" + (f": {a.out}" if a.out else ""))
    rom = mkrom.OUT
    if rom.exists():                  # the samples built in beside it: do they still fit?
        rom_kb = sum(int(n) for n in re.findall(r"S7_ROM_D\d+\[(\d+)\]", rom.read_text())) / 1024
        room = (mkrom.APP_SLOT - mkrom.FIRMWARE - total) / 1024
        print(f"the samples built in ({rom.name}): {rom_kb:.0f} KB of the {room:.0f} KB left"
              + ("" if rom_kb <= room else ": too many, make it again with fewer (tools/mkrom.py)"))


if __name__ == "__main__":
    sys.exit(main())
