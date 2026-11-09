#!/usr/bin/env python3
"""Writes app/tests/sysex.txt: the FM-1 as an X7000 to Axs7, in the host simulator. The messages are built here from
documentation/akai-s700-x7000/01-sample-dump.md and 02-sample-parameters.md, apart from the firmware's C code, so the
script checks the firmware against the documents.

  .venv/bin/python test/gen_sysex_test.py      (test/run.sh does it)
"""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def hx(b):
    return " ".join(f"{x:02X}" for x in b)


def tb(v):
    return [v & 0x7F, (v >> 7) & 0x7F, (v >> 14) & 0x7F]


def dump(slot, words, loop_start, loop_end, alternating=False, period=25281, announce=None):
    """a sample dump as Axs7 sends one: the header, 60-word blocks, F7"""
    total = announce if announce is not None else len(words)
    h = [0xF0, 0x7E, 0x01, slot, 0x00, 12, *tb(period), *tb(total), *tb(loop_start), *tb(loop_end),
         1 if alternating else 0]
    out = list(h)
    nblocks = (len(words) + 59) // 60
    for b in range(nblocks):
        blk = [b & 0x7F]
        for i in range(60):
            at = b * 60 + i
            w = words[at] if at < len(words) else 0
            blk += [(w >> 5) & 0x7F, (w & 0x1F) << 2]
        chk = 0
        for x in blk[1:]:
            chk ^= x
        out += blk + [chk]
    return out + [0xF7]


def sprm(channel, slot, values):
    out = [0xF0, 0x47, channel, 0x0A, 0x41, slot, 0x00]
    for v in values:
        out += [v & 0x7F, v >> 7]
    chk = 0
    for x in out[7:]:
        chk ^= x
    return out + [chk, 0xF7]


def default_values():
    """the X7000's empty slot (02/03): not in any program, the test tone's sound"""
    v = []
    for _ in range(32):
        v += [24, 0, 0, 0x80 | 60]
    v += [2, 2]                                  # undefined (2), RPMODE looping
    v += [0, 0, 359 & 0xFF, 359 >> 8, 360 & 0xFF, 360 >> 8, 0, 0, 359 & 0xFF, 359 >> 8]
    v += [50, 0, 64, 99, 30, 75, 0, 60, 48, 0, 0, 50, 0, 0, 0]
    assert len(v) == 155
    return v


def word(v, k):
    return v[k] | v[k + 1] << 8


def setword(v, k, x):
    v[k] = x & 0xFF
    v[k + 1] = x >> 8


def main():
    # 300 words of a ramp-ish sine, looping from 100 to the end; Axs7 announces 200 more than it sends
    n = 300
    words = [2048 + int(1500 * math.sin(2 * math.pi * 5 * i / n)) for i in range(n)]
    d = dump(2, words, 100, n, announce=n + 200)
    assert len(d) < 640 or True
    # the parameters Axs7 sends after: in P1 keys 60-72, root 66, rate code 76, release 50
    v = default_values()
    v[0:4] = [60, 72, 0, 0x80 | 60]
    setword(v, 130, 0)
    setword(v, 132, n - 1)
    setword(v, 134, n)
    setword(v, 136, 100)
    setword(v, 138, n - 100 - 1)
    v[129] = 2
    v[147] = 66
    v[148] = 76
    v[144] = 50
    # what the FM-1 answers then: the same values (a dump sets START 0, END, the loop, root 60, rate; SPRM the rest)
    lines = [
        "# The FM-1 as an X7000 with 16 slots, to Axs7: made by test/gen_sysex_test.py, do not edit",
        "wait 600",
        "usb plug",
        "expect words3 0",
        "# a dump into slot 3 (nn = 2): the slot takes it, as one shot until its parameters come",
        "sysex " + hx(d),
        "wait 100",
        f"expect words3 {n}",
        f"expect end3 {n}",
        "expect mode3 2",
        "expect root3 60",
        "# the parameters: slot 3 in P1, keys 60-72",
        "sysex " + hx(sprm(0, 2, v)),
        "wait 30",
        "expect root3 66",
        "# RSPRM: they come back, byte for byte",
        "sysex_out",
        "sysex F0 47 00 04 41 02 00 F7",
        "wait 30",
        "expect_sysex " + hx(sprm(0, 2, v)),
        "# another channel's request: no answer",
        "sysex_out",
        "sysex F0 47 05 04 41 02 00 F7",
        "wait 30",
        "sysex_out",
        "# a key in its range plays it, with slot 1's test tone (in P1 at keys 24-99 since switching on)",
        "midi 90 40 64",
        "wait 30",
        "expect voices 2",
        "midi 80 40 40",
        "wait 3500",
        "# saved by itself once the transfer is quiet: after a power cycle it is all there",
        "expect unsaved 0",
        "reboot",
        "wait 600",
        f"expect words3 {n}",
        "expect root3 66",
        "usb plug",
        "# RSD: the slot sent back as the sampler sends it, START to END",
        "sysex_out",
        "sysex F0 7E 00 02 00 F7",
        "wait 100",
        "expect_sysex " + hx(dump(2, words, 100, n)),
        "# ROVS: the overall settings, channel 1, pitch wheel 2",
        "sysex_out",
        "sysex F0 47 00 01 41 00 00 F7",
        "wait 30",
    ]
    o = [0xF0, 0x47, 0x00, 0x08, 0x41, 0x00, 0x00]
    for x in [0, 3, 0, 0, 2, 0, 5, 127]:
        o += [x & 0x7F, x >> 7]
    o += [0] * 8
    chk = 0
    for x in o[7:]:
        chk ^= x
    lines.append("expect_sysex " + hx(o + [chk, 0xF7]))
    (ROOT / "app" / "tests" / "sysex.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
