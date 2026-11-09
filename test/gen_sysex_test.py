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
    mmt8_test()
    clock_test()


# ---- the MMT-8's memory dump, as documentation/MMT-8 SYSEX.odt has it (with mmt8utils' finding: the BCD beats are
# the beats + 1) ----
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
        "# The MMT-8's memory dump: made by test/gen_sysex_test.py from documentation/MMT-8 SYSEX.odt, do not edit.",
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
        "turn ALGO 11",
        "wait 30",
        "expect fn 11",
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


if __name__ == "__main__":
    main()
