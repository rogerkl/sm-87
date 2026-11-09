# Akai exclusive: a sample's parameters

The sample dump carries audio, rate and loop. The rest of a sample (START, END, scan mode, root key, fine tune, the
sound settings, and its key range in each of the 32 programs) goes in Akai's own exclusive messages. They have a
manufacturer (47h) and a MIDI channel, unlike the dump.

Verified on an X7000 (2026-11): RSPRM and SPRM, on channel 1 with identifier 41h, in both directions, many times
(Axs7 reads and sets parameters with them, and its measurement tools set a test sample's sound before every note).
The other functions here are Akai's spec only, untried.

## Messages

All start `F0 47 cc ff 41`: Akai, MIDI channel cc (0-15), function ff, the S700's identifier. Akai's spec says
"65": that is decimal, **41h**. (Axs7 also tries 65h when 41h gets no answer; the X7000 answers 41h.)

| Function | ff | Message |
|---|---|---|
| ROVS, request overall settings | 01 | `F0 47 cc 01 41 00 00 F7` |
| RSPRM, request sample parameters | 04 | `F0 47 cc 04 41 nn 00 F7`, nn = slot from 0 (0-5, 0-15 with the memory board) |
| SECRE, enable sample dump reception | 05 | `F0 47 cc 05 41 00 00 F7` |
| SECRD, disable it | 06 | `F0 47 cc 06 41 00 00 F7` |
| OVS, overall settings | 08 | 33 bytes, below |
| SPRM, sample parameters | 0A | 319 bytes, below |

SECRE/SECRD exist because the sample dump has no channel: on a line with several S700s, all but one can be told to
ignore dumps.

The sampler answers RSPRM with SPRM on the same channel and identifier. It **doesn't answer an SPRM it receives**:
to know it took one, ask with RSPRM and compare (Axs7 waits 30 ms, then asks; the X7000 has the new values by then).
Akai's timeout: once the first six bytes of an exclusive message have arrived, the rest must not pause more than
2 s between bytes.

## Coding

- **DB**: an 8-bit value as two MIDI bytes, low 7 bits first, then the top bit: `v = b0 | (b1 << 7)`.
  Akai: if the second byte is 2 or more, the sampler keeps its value (to change only some). Not tried; Axs7 sends
  every value, after reading the slot's current ones.
- **DW**: a 16-bit value as two DBs, low byte then high byte (four MIDI bytes). Akai: if "the second and third bytes"
  are 2 or more the sampler keeps its value; the third is data, so that rule is unclear. Not tried.
- Signed values are two's complement in their byte (-1 is FFh).
- **Checksum**: XOR of all the data's MIDI bytes (7 to 316 in SPRM).

## SPRM: 319 bytes

    F0 47 cc 0A 41 nn 00  <155 DB pairs: 310 bytes>  checksum  F7

The 155 values, numbered k from 0 (MIDI bytes 7 + 2k and 8 + 2k):

| k | Bytes | Name | Range | |
|---|---|---|---|---|
| 0-127 | 7-262 | | | 32 programs × 4 values, below |
| 128 | 263-264 | | | Undefined (an X7000 replied 2 here, the scan mode's value) |
| 129 | 265-266 | RPMODE | 1, 2, 4, 9 | Scan mode: 1 one shot, 2 looping, 4 alternating, 9 drum trigger (a one shot the key off doesn't stop) |
| 130-131 | 267-270 | SSTRT | 0-32552 | START: where playing starts (DW) |
| 132-133 | 271-274 | | | Undefined in the spec. The X7000 keeps **END - START - 1** here (DW). |
| 134-135 | 275-278 | SEND | 200-32752 | END + 1: after the last word played. At least START + 200. (DW) |
| 136-137 | 279-282 | SLOOP | 0-32752 | The loop point, < SEND: a loop runs from here to END (DW) |
| 138-139 | 283-286 | | | Undefined in the spec; the sampler keeps **END - LOOP - 1** here (DW) |
| 140 | 287-288 | LFSP | 0-99 | LFO speed |
| 141 | 289-290 | LFDP | 0-99 | LFO depth |
| 142 | 291-292 | LFDL | 0-99 | LFO delay |
| 143 | 293-294 | FLTFRQ | 0-99 | Filter |
| 144 | 295-296 | RLEASE | 0-99 | Release |
| 145 | 297-298 | LDNSS | 0-99 | Loudness (LEVEL) |
| 146 | 299-300 | SFNTUN | -16..16 | Fine tune, signed, 1/16 semitone |
| 147 | 301-302 | RECNP | 24-96 | Root key: the MIDI note the sample plays at its own rate (60 = C3, 69 = A440) |
| 148 | 303-304 | RECBW | 36-77 | Rate code: rate = 26400 × 2^((RECBW - 69) / 12) Hz |
| 149 | 305-306 | RVRSE | 0, 255 | 0 forward, 255 reversed. Display only: the audio is stored as it plays. |
| 150 | 307-308 | VELFIL | 0-99 | Velocity to filter |
| 151 | 309-310 | VELLDN | 0-99 | Velocity to loudness |
| 152 | 311-312 | VELRLS | 0-99 | Velocity to release (it follows the **note off** velocity, see [04-playback.md](04-playback.md)) |
| 153-154 | 313-316 | | | Undefined |
| | 317 | | | Checksum |
| | 318 | | | F7 |

What the values mean in sound, measured, is in [04-playback.md](04-playback.md). Akai's own notes, for comparison:
"LFSP 0 = 2 Hz, 99 = 8 Hz", "LFDP 99 = ± semitone" (measured: ±47 cents), "LFDL 99 means slow buildup",
"FLTFRQ 99 means maximum", "LDNSS LSB is -0.375 dB" (measured 0.345), "VELRLS 99 gives slowest release for low
velocity".

Keep the two undefined words right when setting START, END or the loop (Axs7 does; whether the sampler needs them
is unknown).

### A program's four values (k = 4p ... 4p + 3, program p from 0)

| | Name | |
|---|---|---|
| 4p | KEYL | Lowest key of the sample's range in this program (MIDI note, 0-99) |
| 4p + 1 | KEYH | Highest key |
| 4p + 2 | TRANSP | Low 7 bits: transpose, signed, ±60 semitones. Top bit: constant pitch (every key plays at the root's pitch). |
| 4p + 3 | ATPTCH | Audio trigger: top bit set = off; low 7 bits a MIDI note |

**Not in a program**: KEYL > KEYH. The S700 leaves 24 and 0 (`18 00 00 00 00 00 3C 01` on MIDI: KEYL 24, KEYH 0,
transpose 0, audio trigger off with note 60).

### Example

The X7000's answer to RSPRM for slot 3 (nn = 2), a sine test sample of Axs7's lab: in program 32 only, keys 60-96,
looping from 0 to 32000, LFO speed 50, filter 99, release 80, loudness 99, root 72, rate code 76 (39555 Hz),
velocity to release 99. Request: `F0 47 00 04 41 02 00 F7`.

    f0 47 00 0a 41 02 00                                        header: channel 1, SPRM, slot 3
    18 00 00 00 00 00 3c 01   (× 31: programs 1-31)             KEYL 24, KEYH 0, not in the program
    3c 00 60 00 00 00 3c 01                                     program 32: keys 60 (3Ch) to 96 (60h)
    02 00                                                       undefined (2)
    02 00                                                       RPMODE 2, looping
    00 00 00 00                                                 SSTRT 0
    7f 01 7c 00                                                 END - START - 1 = 31999 (FFh, 7Ch)
    00 00 7d 00                                                 SEND 32000 (00h, 7Dh)
    00 00 00 00                                                 SLOOP 0
    7f 01 7c 00                                                 END - LOOP - 1 = 31999
    32 00 00 00 00 00                                           LFO speed 50, depth 0, delay 0
    63 00 50 00 63 00                                           filter 99, release 80, loudness 99
    00 00 48 00 4c 00 00 00                                     fine 0, root 72, rate code 76, forward
    00 00 00 00 63 00                                           velocity to filter 0, loudness 0, release 99
    00 00 00 00                                                 undefined
    3c f7                                                       checksum, F7

## OVS: overall settings (Akai's spec, untried)

    F0 47 cc 08 41 00 00  <data>  checksum  F7      (33 bytes)

| Bytes | Type | Name | |
|---|---|---|---|
| 7-8 | DB | MDCHO | Basic MIDI reception channel, 0-15 |
| 9-10 | DB | MDMPM | MIDI mode: 3 poly, 8 mono, 16 special mono |
| 11-12 | DB | MDXTCH | Channel the sampler sends Akai exclusive on |
| 13-14 | DB | OMNION | 128 omni on, 0 omni off, 64 multi-program |
| 15-16 | DB | PWRNGE | Pitch wheel range, ± semitones, 0-12 |
| 17-18 | DB | PRCHOF | Program change reception: 0 on, 255 off |
| 19-20 | DB | CHOMAX | Channels - 1 used in omni off mono mode, 0-5 |
| 21-22 | DB | AFSENS | Sensitivity to channel pressure, 0-127 |
| 23-30 | DW, DW | | Reserved, 0 |
| 31 | | | Checksum: XOR of bytes 7-30 |
| 32 | | | F7 (Akai's text says F0, a slip) |

On the X7000 the **pressure sensitivity is per sample** (set in its LFO menu, measured to scale only that sample's
aftertouch vibrato), and it is **not** in SPRM: changing it on the panel changes none of SPRM's bytes (verified).
Whether the X7000 still has AFSENS in OVS is unknown.

## Sending a sample to the sampler

What Axs7 does, verified on the X7000:

1. Optionally RSPRM first, to know the sampler answers (and on which channel; Axs7 tries channels and both
   identifiers once and keeps what answers).
2. The sample dump (see [01-sample-dump.md](01-sample-dump.md)).
3. Wait until it has gone out (21 s for a full sample), then SPRM with all 155 values: START, END and the loop as
   the sample's, scan mode, root, rate code, sound, and the key ranges.
4. RSPRM, and compare: resend if a value differs.

A sample that is in no program on the sampler isn't played by any key; a slot nothing was loaded into is like that
(see [03-samples-and-programs.md](03-samples-and-programs.md)).
