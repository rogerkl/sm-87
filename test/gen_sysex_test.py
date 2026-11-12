#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Roger Klaveness
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
    mmt8_test()
    clock_test()
    powercut_test()
    hr16_test()


# ---- the MMT-8's memory dump, as its SysEx document has it (documentation/manuals.md), with mmt8utils' finding: the
# BCD beats are the beats + 1 ----
def mmt8_body(ev):
    """an event's five bytes: number, amount (bit 7: not a note), channel, two more"""
    kind = ev[1]
    if kind == "note":                 # key, velocity, channel, duration (most significant byte first)
        _, _, key, vel, ch, dur = ev
        return [key, vel, ch, dur >> 8, dur & 0xFF]
    if kind == "cc":
        _, _, num, val, ch = ev
        return [num, 0x80 | val, ch, 0, 0]
    if kind == "prog":
        _, _, prog, ch = ev
        return [122, 0x80 | prog, ch, 0, 0]
    if kind == "touch":
        _, _, val, ch = ev
        return [123, 0x80 | val, ch, 0, 0]
    if kind == "bend":
        _, _, lsb, msb, ch = ev
        return [124, 0x80, ch, lsb, msb]
    raise ValueError(kind)


def mmt8_memory(parts, songs, keep):
    """parts {no: (beats, name, channels of tracks 1-8, {track: [events, in time order]})}, songs {no: (tempo, name,
    [(part, tracks)])}, keep {address: byte}: the header's bytes not to alter"""
    mem = bytearray(0x200)
    for a, v in keep.items():
        mem[a] = v
    for p in sorted(parts):
        beats, name, chans, tracks = parts[p]
        mem[2 * p:2 * p + 2] = (len(mem) + 0x400).to_bytes(2, "big")
        part = bytearray(0x2A)
        part[0x12:0x14] = int(str(beats + 1), 16).to_bytes(2, "little")
        for j in range(8):             # tracks 8 .. 1
            part[0x14 + j] = chans[7 - j]
        part[0x1C:0x2A] = name.ljust(14).encode()
        for j in range(8):
            part[2 + 2 * j:4 + 2 * j] = len(part).to_bytes(2, "little")
            last = None
            for ev in tracks.get(8 - j, []):
                b = mmt8_body(ev)
                if ev[0] != last:      # a new clock: the 7-byte packet with it
                    part += bytes([0x80 | b[0]]) + ev[0].to_bytes(2, "little") + bytes(b[1:])
                    last = ev[0]
                else:
                    part += bytes(b)
            part += bytes([0x80]) + (beats * 96).to_bytes(2, "little") + bytes([0, 0x80, 0, 0])
        part[0:2] = len(part).to_bytes(2, "little")
        mem += part
    for s in sorted(songs):
        tempo, name, steps = songs[s]
        mem[0x102 + 2 * s:0x104 + 2 * s] = (len(mem) + 0x400).to_bytes(2, "big")
        song = bytearray([0, 0, tempo]) + name.ljust(14).encode()
        for part, tracks in steps:
            song += bytes([part, tracks])
        song += b"\xff"
        song[0:2] = len(song).to_bytes(2, "little")
        mem += song
    free = len(mem) + 0x400
    mem[0xCF:0xD1] = free.to_bytes(2, "little")
    mem[0xD3:0xD5] = (0xFF00 - free).to_bytes(2, "little")
    return mem


def mmt8_sysex(mem):
    """F0 00 00 0E 00, seven bytes in eight (a 7-bit stream, the most significant bit first), F7"""
    bits = "".join(f"{x:08b}" for x in mem)
    bits += "0" * (-len(bits) % 7)
    return [0xF0, 0x00, 0x00, 0x0E, 0x00] + [int(bits[i:i + 7], 2) for i in range(0, len(bits), 7)] + [0xF7]


def mmt8_test():
    parts = {
        0: (2, "VERSE", [0, 5, 0, 0, 0, 0, 0, 0], {
            1: [(0, "note", 60, 100, 0, 48), (0, "note", 64, 90, 0, 48), (96, "note", 67, 80, 0, 90)],
            2: [(0, "prog", 3, 1), (0, "cc", 7, 100, 1), (48, "bend", 0, 0x50, 1), (60, "touch", 40, 1)],
        }),
        3: (4, "CHORUS", [0] * 8, {
            8: [(0, "note", 36, 127, 9, 383)],
            4: [(200, "note", 72, 64, 0, 24)],
        }),
    }
    songs = {0: (96, "SONG A", [(0, 0xFF), (3, 0x0F), (0, 0x01)]), 7: (140, "SONG B", [(3, 0xFF)])}
    keep = {0xC8: 0x11, 0xC9: 0x22, 0xCE: 0x33, 0xD1: 0x44, 0xD2: 0x55, 0xD5: 0x66, 0x101: 0x77, 0x1CA: 0x88,
            0x1FF: 0x99}
    dump = mmt8_sysex(mmt8_memory(parts, songs, keep))
    lines = [
        "# The MMT-8's memory dump: made by test/gen_sysex_test.py from the MMT-8's SysEx document, do not edit.",
        "# A dump from an MMT-8 replaces the sequencer's memory; MIDI DUMP (ALGO) sends it back the same, its header",
        "# as the MMT-8's was.",
        "wait 600",
        "tap SEQ",
        "wait 30",
        "expect mmt8hdr 0",
        "sysex " + hx(dump),
        "wait 100",
        "expect beats0 2",
        "expect used0 3",
        "expect beats3 4",
        "expect used3 136",
        "expect events1 3",
        "expect events2 4",
        "expect chan2 5",
        "expect mmt8hdr 1",
        "expect unsaved 1",
        "shot mmt8_in.png",
        "# its songs: song 00 three steps, the second part 03",
        "tap SEQ",
        "wait 30",
        "expect songmode 1",
        "expect steps 3",
        "expect step2 3",
        "tap SEQ",
        "wait 30",
        "expect songmode 0",
        "# MIDI DUMP (the last function): REC sends the memory back, byte for byte the dump that came",
        "usb plug",
        "turn ALGO 13",
        "wait 30",
        "expect fn 13",
        "shot mmt8_dump.png",
        "sysex_out",
        "tap REC",
        "wait 300",
        "expect_sysex " + hx(dump),
        "# kept with SAVE: after a power cycle the parts and the MMT-8's header are there",
        "tap SAVE",
        "wait 500",
        "expect unsaved 0",
        "reboot",
        "wait 600",
        "expect beats3 4",
        "expect used0 3",
        "expect mmt8hdr 1",
    ]
    (ROOT / "app" / "tests" / "mmt8.txt").write_text("\n".join(lines) + "\n")


def clock_test():
    """MIDI clock in: 125 BPM (24 clocks a beat, 20 ms apart: a "midi" line takes 2 ms)"""
    def clocks(n):
        return ["midi F8", "wait 18"] * n
    lines = [
        "# MIDI clock in: made by test/gen_sysex_test.py, do not edit. CLOCK (ALGO) to MIDI; the clocks' tempo; FA starts",
        "# at the next clock, FC stops, F2 sets the place, FB goes on from it; PLAY waits for the clocks; kept with SAVE.",
        "wait 600",
        "tap SEQ",
        "wait 30",
        "# part 00 four beats long",
        "turn ALGO 1",
        "turn K1 4 20",
        "tap REC",
        "wait 30",
        "expect beats0 4",
        "turn ALGO 9",
        "wait 30",
        "expect fn 10",
        "expect clockin 0",
        "turn K1 1",
        "wait 30",
        "expect clockin 1",
        "shot clock.png",
        "turn ALGO -10",
        "wait 30",
        "expect fn 0",
        *clocks(48),
        "expect exttempo >123",
        "expect exttempo <127",
        "expect seq 0",
        "midi FA",
        "wait 5",
        "expect seq 2",
        "expect beat 1",
        *clocks(30),
        "expect beat 2",
        "midi FC",
        "wait 5",
        "expect seq 0",
        "# the song position: eight 16ths, beat 3; FB goes on from there",
        "midi F2 08 00",
        "wait 5",
        "expect beat 3",
        "midi FB",
        "wait 5",
        "expect seq 2",
        *clocks(26),
        "expect beat 4",
        "midi FC",
        "wait 5",
        "# PLAY with MIDI's clock: playing, at beat 1 until clocks come",
        "tap PLAY",
        "wait 300",
        "expect seq 2",
        "expect beat 1",
        *clocks(25),
        "expect beat 2",
        "tap PLAY",
        "wait 30",
        "tap SAVE",
        "wait 300",
        "reboot",
        "wait 600",
        "expect clockin 1",
    ]
    (ROOT / "app" / "tests" / "clockin.txt").write_text("\n".join(lines) + "\n")


# ---- the HR-16's memory dump, as its service manual has it (documentation/hr16-sysex.md) ----
HR16_PAD = [0, 1, 2, 6, 3, 7, 14, 15, 8, 9, 10, 11, 5, 4, 12, 13]   # its drums on SM-87's pads (the README's table)


def hr16_waits(n):
    """clocks to wait: 126 at most a byte"""
    out = []
    while n > 0:
        c = min(n, 126)
        out.append(0x80 | c)
        n -= c
    return bytes(out)


def hr16_memory(patterns, keep):
    """patterns {no: (beats, [(sound, level, pan << 5 | pitch)] for its 16 drums, [(clock, drum, dynamics)])}, keep
    {address: byte}: the header's other bytes. Its events in time order; a clock's drums in the order of SM-87's pads,
    as SM-87 sends them"""
    mem = bytearray(0x400)
    for a, v in keep.items():
        mem[a] = v
    mem[0xFE], mem[0xFF] = 0x27, 0xB5
    for p in sorted(patterns):
        beats, voices, hits = patterns[p]
        mem[0x200 + 2 * p:0x202 + 2 * p] = (len(mem) + 0x8200).to_bytes(2, "big")
        pat = bytearray(0x34)
        pat[2:4] = int(str(beats), 16).to_bytes(2, "little")
        for d, v in enumerate(voices):
            pat[4 + 3 * d:7 + 3 * d] = bytes(v)
        t = 0
        for clock, drum, dyn in sorted(hits, key=lambda h: (h[0], HR16_PAD[h[1]])):
            pat += hr16_waits(clock - t)
            t = clock
            pat.append(dyn << 4 | drum)
        pat += hr16_waits(beats * 96 - t) + bytes([0xFF])
        pat[0:2] = len(pat).to_bytes(2, "little")
        mem += pat
    mem[0x302 + 2 * 99:0x304 + 2 * 99] = (len(mem) + 0x8200).to_bytes(2, "big")   # song 99, empty: it must be
    mem += bytes([3, 0, 0xFF])
    end = len(mem) + 0x8200
    mem[0x2CD:0x2CF] = end.to_bytes(2, "little")
    mem[0x2D1:0x2D3] = (0xFF00 - end).to_bytes(2, "little")
    return mem


def hr16_test():
    # each drum's sound, level, pan and pitch (the pitch's steps ones SM-87's semitones give back)
    sounds = [7, 13, 24, 27, 26, 46, 32, 33, 18, 19, 20, 21, 29, 30, 42, 45]
    levels = [90, 80, 70, 60, 75, 85, 50, 55, 65, 66, 67, 68, 72, 99, 40, 0]
    pans = [3, 2, 4, 3, 1, 5, 0, 6, 3, 3, 3, 3, 4, 2, 3, 3]
    pitch = [16, 18, 14, 16, 20, 16, 11, 16, 18, 16, 14, 16, 16, 16, 16, 16]
    voices = [(sounds[d], levels[d], pans[d] << 5 | pitch[d]) for d in range(16)]
    patterns = {
        0: (2, voices, [(0, 0, 7), (0, 13, 5), (0, 3, 2), (48, 1, 6), (96, 2, 3), (100, 8, 4), (191, 15, 0)]),
        3: (1, voices, [(0, 0, 7), (24, 4, 1), (72, 12, 2)]),
        12: (4, voices, []),
        40: (3, voices, [(130, 9, 4), (287, 10, 7)]),
    }
    notes = [35, 38, 42, 44, 46, 39, 60, 61, 50, 48, 45, 41, 51, 49, 62, 63]
    keep = {0x10: 0x11, 0x50: 0x22, 0xC8: 9, 0xC9: 1, 0xCA: 1, 0xCB: 37, 0xDD: 1, 0xE6: 1, 0x120: 0x33, 0x2C8: 0x44,
            0x2D5: 6, 0x2D8: 112, 0x2D9: 24, 0x2DA: 4, 0x3D3: 0x55}
    keep.update({0xCC + d: n for d, n in enumerate(notes)})
    keep.update({0xE7 + i: c for i, c in enumerate(b" 1.07")})
    dump = [0xF0, 0x00, 0x00, 0x0E, 0x01, 0x00] + mmt8_sysex(hr16_memory(patterns, keep))[5:]
    lines = [
        "# The HR-16's memory dump: made by test/gen_sysex_test.py from its service manual's format, do not edit. A",
        "# dump from an HR-16 replaces the drum patterns (hits, lengths, each one's VOICE, TUNE and MIX); MIDI DUMP",
        "# (the drum page's last function) and its request send them back the same, its header as it was.",
        "wait 600",
        "usb plug",
        "tap ARP",
        "wait 30",
        "expect hr16hdr 0",
        "sysex " + hx(dump),
        "wait 200",
        "expect hr16hdr 1",
        "expect drprog 1",
        "expect drbeats0 2",
        "expect drhits0 7",
        "expect drbeats3 1",
        "expect drhits3 3",
        "expect drbeats12 4",
        "expect drhits12 0",
        "expect drbeats40 3",
        "expect drhits40 2",
        "expect patown0 1",
        "expect patown12 1",
        "expect patown5 0",
        "expect drquant 3",
        "expect drswing 6",
        "expect drch 10",
        "expect unsaved 1",
        "# pattern 00's first hits: the kick, the crash (pad 5), the mid hat (pad 7), each dynamics as a velocity",
        "expect drhpad1 1",
        "expect drhvel1 127",
        "expect drhpad2 5",
        "expect drhvel2 95",
        "expect drhpad3 7",
        "expect drhvel3 47",
        "# its sounds on the pads: kinds the kit has (a gated kick the KICK, a crash the cymbal, the foot hat the",
        "# closed one), a tom none (the pad's own); levels, pans, tunings",
        "expect padsnd1 0",
        "expect padlevel1 90",
        "expect padpan1 0",
        "expect padsnd2 1",
        "expect padtune2 1",
        "expect padpan2 -1",
        "expect padsnd5 4",
        "expect padlevel5 99",
        "expect padsnd7 2",
        "expect padsnd9 -1",
        "expect padtune9 1",
        "expect padlevel9 65",
        "wait 1300",
        "shot hr16_in.png",
        "# MIDI DUMP (the drum page's last function): REC sends them back, byte for byte the dump that came",
        "turn ALGO 9",
        "wait 30",
        "expect drfn 9",
        "wait 1300",
        "shot hr16_dump.png",
        "sysex_out",
        "tap REC",
        "wait 300",
        "expect_sysex " + hx(dump),
        "# an HR-16's request for its dump: the same",
        "sysex_out",
        "sysex F0 00 00 0E 01 07 F7",
        "wait 300",
        "expect_sysex " + hx(dump),
        "# its MIDI test: answered",
        "sysex_out",
        "sysex F0 00 00 0E 01 09 00 F7",
        "wait 30",
        "expect_sysex F0 00 00 0E 01 09 01 F7",
        "# kept with SAVE: after a power cycle the patterns and the HR-16's header are there",
        "tap SAVE",
        "wait 500",
        "expect unsaved 0",
        "reboot",
        "wait 600",
        "expect hr16hdr 1",
        "expect drbeats0 2",
        "expect drhits0 7",
        "expect patown3 1",
        "expect drswing 6",
        "expect drprog 1",
    ]
    (ROOT / "app" / "tests" / "hr16.txt").write_text("\n".join(lines) + "\n")

# ---- the power cut before a dump is saved: the slots as the store had them, none pointing at the new sample ----
def powercut_test():
    a = [2048 + int(1500 * math.sin(2 * math.pi * 3 * i / 60)) for i in range(60)]
    b = [2048 + int(900 * math.sin(2 * math.pi * 7 * i / 120)) for i in range(120)]
    lines = [
        "# A power cut after a dump, before it is saved (3 s after): made by test/gen_sysex_test.py, do not",
        "# edit. The store's directory then comes back; a dump into sectors it points at (a sample cleared or",
        "# replaced since) saves the directory without them first, so no slot comes back playing the new",
        "# sample's data",
        "wait 600",
        "usb plug",
        "# slot 3: 60 words, in the first sector; saved once quiet",
        "sysex " + hx(dump(2, a, 0, len(a))),
        "wait 100",
        "expect words3 60",
        "expect free 72",
        "wait 3500",
        "expect unsaved 0",
        "# CLEAR (EDIT's last page) slot 3, not saved; slot 4's dump takes its sector; the power cut",
        "tap EDIT",
        "tap EDIT",
        "tap EDIT",
        "tap EDIT",
        "tap EDIT",
        "wait 30",
        "expect page 8",
        "turn SELECT 2",
        "wait 30",
        "expect slot 3",
        "tap REC",
        "wait 30",
        "expect words3 0",
        "expect free 73",
        "expect unsaved 1",
        "sysex " + hx(dump(3, b, 0, len(b))),
        "wait 100",
        "expect words4 120",
        "expect free 72",
        "reboot",
        "wait 600",
        "# slot 3 cleared (saved before the dump), slot 4 empty as before it",
        "expect words3 0",
        "expect words4 0",
        "expect free 73",
        "expect unsaved 0",
        "# slot 3 again, saved; then a dump replacing it, into its own sector; the power cut: the slot empty",
        "usb plug",
        "sysex " + hx(dump(2, a, 0, len(a))),
        "wait 100",
        "expect words3 60",
        "wait 3500",
        "expect unsaved 0",
        "sysex " + hx(dump(2, b, 0, len(b))),
        "wait 100",
        "expect words3 120",
        "expect free 72",
        "reboot",
        "wait 600",
        "expect words3 0",
        "expect free 73",
        "# a dump into a sector the store doesn't point at: saved as before, 3 s after",
        "usb plug",
        "sysex " + hx(dump(3, b, 0, len(b))),
        "wait 100",
        "expect words4 120",
        "expect unsaved 1",
        "wait 3500",
        "expect unsaved 0",
        "reboot",
        "wait 600",
        "expect words4 120",
    ]
    (ROOT / "app" / "tests" / "powercut.txt").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

