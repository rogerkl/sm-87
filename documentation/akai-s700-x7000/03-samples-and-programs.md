# Samples and programs

How the S700/X7000 organises its sounds: what a sample is, how it is played back in pitch, and how programs map keys
to samples. The values are the ones in the parameter message ([02-sample-parameters.md](02-sample-parameters.md))
and on disk ([05-quickdisk.md](05-quickdisk.md)).

## Slots

- 6 sample slots on a standard unit, **16** with the memory expansion board (the X7000 measured has it). The display
  numbers them from 1; MIDI messages from 0.
- Each holds one sample of up to **32752 words** (a sample dump's limit; a disk block holds 32768).
- A word is **12 bits, offset binary**: 0 to 4095, 2048 (800h) is zero. `signed = word - 2048`.

## A sample's points and scan mode

| | Value | |
|---|---|---|
| START | SSTRT, 0-32552 | Where playing starts |
| END | SEND - 1 | The last word played; SEND (END + 1) is what is stored, at least START + 200 |
| LOOP | SLOOP, < SEND | Where a loop starts; it runs to END |
| Scan mode | RPMODE | 1 one shot, 2 looping, 4 alternating, 9 drum trigger |

- **One shot** (1): plays from START to END, or until the key is let go and the release has faded (the release
  applies to one shots too, measured).
- **Looping** (2): plays from START to END, then from LOOP to END over and over while the key is held, and through
  the release after it.
- **Alternating** (4): as looping, but back and forth: LOOP to END, END back to LOOP, ... (Axs7 turns at the last
  word and at the loop's first word, playing neither twice.)
- **Drum trigger** (9): a one shot the key off doesn't stop: it always plays to END, whatever RELEASE says.

START may lie after LOOP: the sample plays from START and loops back to LOOP. That is valid on the sampler (but such
a sample can't be dumped with its loop, see [01-sample-dump.md](01-sample-dump.md)).

A sample dump's header says looping or alternating, and a loop shorter than 5 words means one shot; drum trigger
and START only come with the parameter message.

**Reverse**: RVRSE (0 or 255) is only for the display. A reversed sample is stored as it plays, so a player ignores
the flag and plays memory forward. (A sample dumped from the sampler arrives as it plays, too.)

## Rate and pitch

The rate a sample was recorded at, its **rate code** (RECBW): `rate = 26400 × 2^((code - 69) / 12)` Hz. The sampler
has 41 rates, a semitone apart; a sample dump's period (ns) is one of them:

| Code | Rate (Hz) | Period (ns) | | Code | Rate (Hz) | Period (ns) |
|---|---|---|---|---|---|---|
| 76 | 39555.4 | 25281 | | 55 | 11759.86 | 85035 |
| 75 | 37335.72 | 26784 | | 54 | 11099.89 | 90091 |
| 74 | 35239.81 | 28377 | | 53 | 10476.8 | 95449 |
| 73 | 33262.37 | 30064 | | 52 | 9888.85 | 101124 |
| 72 | 31395.2 | 31852 | | 51 | 9333.84 | 107137 |
| 71 | 29633.14 | 33746 | | 50 | 8809.95 | 113508 |
| 70 | 27969.68 | 35753 | | 49 | 8315.46 | 120258 |
| 69 | 26399.85 | 37879 | | 48 | 7848.74 | 127409 |
| 68 | 24918.39 | 40131 | | 47 | 7408.23 | 134985 |
| 67 | 23519.45 | 42518 | | 46 | 6992.47 | 143011 |
| 66 | 22199.53 | 45046 | | 45 | 6600.01 | 151515 |
| 65 | 20953.82 | 47724 | | 44 | 6229.56 | 160525 |
| 64 | 19777.7 | 50562 | | 43 | 5879.93 | 170070 |
| 63 | 18667.51 | 53569 | | 42 | 5549.91 | 180183 |
| 62 | 17619.9 | 56754 | | 41 | 5238.43 | 190897 |
| 61 | 16630.91 | 60129 | | 40 | 4944.42 | 202248 |
| 60 | 15697.6 | 63704 | | 39 | 4666.9 | 214275 |
| 59 | 14816.57 | 67492 | | 38 | 4404.98 | 227016 |
| 58 | 13984.84 | 71506 | | 37 | 4157.74 | 240515 |
| 57 | 13199.93 | 75758 | | 36 | 3924.38 | 254817 |
| 56 | 12459.2 | 80262 | | | | |

(The periods are what the X7000 and the earlier Java tool use; the rates are 10^9 / period. Akai's spec allows codes
up to 77.) The sampler also calls them bandwidths, about 0.4 × the rate: code 76 is "16 kHz", 60 is "6.3 kHz".

A **key** plays the sample at:

    semitones = (constant pitch ? 0 : key - RECNP) + TRANSP + SFNTUN / 16
    rate      = rate(RECBW) × 2^(semitones / 12)        (+ pitch bend and vibrato)

- **RECNP**, the root, 24-96: the key that plays the sample at its own rate. A sample dump alone gives C3 (60).
- **SFNTUN**, fine tune, ±16 sixteenths of a semitone, the sample's.
- **TRANSP**, ±60 semitones, and **constant pitch**: per program (below).
- Akai: with RECNP = RECBW, no fine tune and no transpose, a waveform of 60 words a cycle plays at concert pitch.

## Programs

There are **32 programs**. Each sample has, in each program, a key range (or none) and two settings:

| | |
|---|---|
| KEYL, KEYH | The keys (MIDI notes, 0-99) that play the sample in this program. KEYL > KEYH: not in the program (the sampler leaves 24 and 0). |
| TRANSP | Semitones, ±60, added to the pitch in this program |
| Constant pitch | Every key in the range plays at the root's pitch (for drums laid out over keys) |
| ATPTCH | Audio trigger: a MIDI note the sample plays when the audio input triggers it; top bit set = off. Not tried. |

A program is the set of samples with a range in it. Ranges may overlap (Axs7 then plays all of them; how the
sampler handles overlaps wasn't checked). The sound settings (LFO, filter, release, loudness) belong to the sample:
every program playing it sounds the same.

**MIDI program change n** selects program n + 1 (verified: 31 selects P32).

## What an empty slot holds

A slot nothing was loaded into holds the X7000's **test tone**: START 0, END 360, LOOP 0, looping, rate code 48,
root 60, LFO speed 50 depth 0 delay 64, filter 99, release 30, loudness 75, velocity to loudness 50, the others 0,
**in no program** (except slot 1 in P1, keys 24-99, on the unit measured after switching on). Seen once after
switching on: an empty slot with END 32752. So "in no program" is the better sign that a slot is unused.

## Storage

| | Bytes |
|---|---|
| A full sample, 12-bit words packed two to three bytes | 49128 |
| The same as 16-bit words | 65504 |
| Its parameters (155 values, or 154 as on disk) | 155 |
| A disk block (sample as 32768 words in nibbles and bytes, parameters, header, CRC) | 49348 |

Packed 12-bit words: `byte0 = w0 & FFh, byte1 = (w0 >> 8) | ((w1 & 0Fh) << 4), byte2 = w1 >> 4`, or the disk's own
layout (low nibbles of all words, then high bytes), which is easy to address by word.
