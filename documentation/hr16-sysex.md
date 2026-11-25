# The Alesis HR-16's SysEx dump

A summary of what SM-87 uses from the HR-16/HR-16B Service Manual 1.00 (1995), section 11.0, MIDI Implementation
(pages 23-30), checked against two HR-16 dumps (`HR16-factory.syx`, `drumatix-hr16.syx`) and the HR-16's and
HR-16B's firmware ROMs (V1.09, V2.00; `HR16_FORMAT.md` beside this file describes them). The manual's own pages are at
<https://www.manualsdir.com/manuals/46570/alesis-hr-16.html?page=29> (pages 29-36 of that scan).

## Messages

Every message is `F0 00 00 0E 01 cc ... F7`: Alesis' ID (`00 00 0E`), the HR-16's (`01`), a command `cc` (00-0A).
The HR-16 sends and takes them only with SYSEX ENABLE (MIDI/UTIL page 15) on.

| Command | What | Data |
|---|---|---|
| 00 | The whole memory | The memory, packed (below). An HR-16 sends it with TAPE held, the left arrow once, then RECORD. It takes one at any time, and it replaces its whole memory |
| 01 | Select a pattern or song | `0x` (0 pattern, 1 song), `nn` (0-99) |
| 02 | RECORD pressed or released | `0x` |
| 03 | Quantize and swing | quantize 0-9, swing 0-24 |
| 04 | A pattern's length | end or top, 3 BCD digits |
| 05 | Copy a pattern, drum or song | a length, a mode, packed data, `00` |
| 06 | Step edit | enter, exit, next beat, next drum |
| 07 | Request a dump | none: the HR-16 answers with 00, unless it's playing |
| 08 | A pattern's voice, tune and mix | 48 bytes packed (a pattern's bytes 04-33) |
| 09 | MIDI test | `00` asks; the HR-16 answers `01` (with MIDI ECHO off) |
| 0A | Erase the pattern or song | none |

SM-87 takes 00 in, sends 00 (MIDI DUMP on the drum page, or when asked with 07), and answers 09. It ignores the
others.

## Packing

Seven memory bytes go as eight MIDI bytes: the 56 bits, most significant first, in 7-bit pieces. It's the MMT-8's
packing, so SM-87 uses the same code for both.

## The memory

Offsets are from the dump's first byte. Addresses inside the memory (the pointers) are those offsets + 8200H; the
memory ends at FF00H, so a dump holds at most 7D00H (32,000) bytes.

| Offset | What |
|---|---|
| 000-0C7 | Don't care |
| 0C8 | MIDI channel |
| 0C9, 0CA | Drum triggers received, sent (0 off, 1 on) |
| 0CB-0DB | MIDI notes: the click, then the 16 drums in their order (below) |
| 0DC-0E6 | MIDI echo, program select, clock mode, clock out, auto start, click, click in play, manual voice/tune/mix (0E3: on, every pattern plays the kit at 103-132), pad dynamics (0-10), song loop, SysEx enable |
| 0E7-0EB | Software version: a space (HR-16) or `B` (HR-16B), then e.g. `2.00`. The HR-16B's V2.00 copies the `B` of its "ALESIS HR-16:B" and its "2.00" there at start-up; the HR-16's V1.09 writes nothing there (the two dumps at hand, both made by software, have other bytes) |
| 0F4 | 0 |
| 0FE, 0FF | 27H, B5H |
| 100-187 | Don't alter (don't care with manual voice/tune/mix off). 100-102 the click's sound, level and pan/pitch, 103-132 the 16 drums' as a pattern has them: the kit playing now, every pattern's with manual voice/tune/mix on (HR16_FORMAT.md) |
| 192-1A1 | 0 |
| 200-2C7 | Patterns 00-99: a pointer each, most significant byte first; 0 for none |
| 2CD-2CE | The first byte after song 99, least significant first |
| 2D1-2D2 | FF00H minus that: the free memory, least significant first |
| 2D5 | Swing, 0-24 (0 = 50%) |
| 2D8 | Tempo, 20-255 |
| 2D9, 2DA | Quantize: its clocks, then its value 0-9 (1/4, 1/6, 1/8, 1/12, 1/16, 1/24, 1/32, 1/48, 1/64, 1/384: 96, 64, 48, 32, 24, 16, 12, 8, 6, 1 clocks) |
| 302-3C9 | Songs 00-99: a pointer each, as the patterns' |
| 400- | The patterns, then the songs |

Other bytes in 000-3FF are marked don't care or don't alter. The patterns come first in order (00-99), then the songs
(00-99), each right after the last, with no gaps; a missing one is skipped. Song 99 must exist, empty if nothing else
(`03 00 FF`). The manual suggests leaving don't-care bytes as they came.

### A pattern

| Offset | What |
|---|---|
| 00-01 | Its length in bytes, header included, least significant first |
| 02-03 | Its beats + 1, BCD, least significant first: 8 beats 0009H, 16 beats 0017H, 32 beats 0033H (the manual says only BCD) |
| 04-33 | Each drum's sound (0-30H), output (bit 7) and level (0-99), pan (bits 5-7: 0 left .. 6 right) and pitch (bits 0-4: 0 = -16 .. 1FH = +15): three bytes a drum |
| 34- | Its events, then FFH |

The drums, in this order everywhere: KICK, SNARE, CLS HAT, MID HAT, OPN HAT, CLAPS, PERC 3, PERC 4, TOM 1, TOM 2,
TOM 3, TOM 4, RIDE, CRASH, PERC 1, PERC 2.

An event is one byte:
- bit 7 clear: a drum played, bits 4-6 its dynamics (0-7), bits 0-3 the drum;
- bit 7 set: wait that many clocks (bits 0-6, 0-126; 96 a beat);
- FFH: the end. The pattern then loops, so its waits add up to its length. The manual's empty 8-beat pattern is
  `FE FE FE FE FE FE 8C FF` (126 × 6 + 12 = 768 clocks).

### A song

Its length (two bytes, least significant first), then its steps, then FFH. A step is a pattern (0-99) or a tempo
change: 100-254, the tempo in BPM + 54 (46-200 BPM). The manual has 100-250 as 50% to 200%, but the factory songs'
tempo steps, 154, 164, 134, 174 and 124, read as 100, 110, 80, 120 and 70 BPM that way, and as odd percentages the
other. At most 255 steps.

## The sounds

The manual numbers the sounds 00-30H but doesn't list them. The list below was first taken from Strellis' replacement
sound ROMs for the HR-16 ([matrixsynth](https://www.matrixsynth.com/2011/04/alesis-hr-16-custom-sounds-eproms-linn.html),
from strellis.com), which give each original voice by its number, 1-49, and a 1988 review lists the same sounds in the
same groups ([Sound On Sound via muzines](https://www.muzines.co.uk/articles/alesis-hr-16-drum-machine/3449)). The
HR-16's own firmware confirms it: its table of the 49 sounds' names (V1.09, 4395H once its address lines are put
right) has them in this order, from `24" POWER KICK` to `DRUM STICKS`. SM-87 takes the data's 0 as voice 1: the
KICK pad's sound shows as 01, and that's the 24" power kick.

The HR-16B has other sounds in another order (`HR16_FORMAT.md`, 3.5: kicks 00-06, snares 07-18, hats 19-23, toms
24-26, crash 27, ride 28, the rest percussion, gated claps 35). SM-87 reads a dump whose 0E7 is `B` with that list.

SM-87 plays a dump's sound as the pad of its drum has it in the kit: the drum whose sound it is as an HR-16 sets its
drums up (a new pattern's, V1.09 at 153DH: KICK 00, SNARE 11, CLS HAT 23, MID HAT 25, OPN HAT 26, CLAPS 46, PERC 3
45, PERC 4 44, TOM 1-4 20, 21, 19, 22, RIDE 28, CRASH 30, PERC 1 31, PERC 2 33; the HR-16B's at 6BC5H: 00, 08, 19,
20, 21, 35, 39, 45, 24, 25, 25, 26, 28, 27, 41, 43), else the drum of its kind (the last column). A sound of no
drum's is silent. Each drum's sound number is kept as it came and goes back in SM-87's dump.

| Data | Voice | Name | SM-87 plays it as |
|---|---|---|---|
| 00-09 | 1-10 | 24" Power Kick, 22" Deep Kick, 22" Power Kick, 20" Swift Kick, 22" Dbl Hd Kick A, 22" Dbl Hd Kick B, 22" 60's Kick, 22" Gated Kick, Electronic Kick 1, Electronic Kick 2 | KICK |
| 10-17 | 11-18 | 8x14" Wood Snare, Ambient Snare, 13" Brass Piccolo, Gated Snare, Electronic Snare, Rimshot Snare, Sidestick, Brush Hit Snare | SNARE |
| 18-22 | 19-23 | 10" Power Tom, 16" Power Tom, 10" Dbl Head Tom, 14" Dbl Head Tom, Electronic Tom | TOM 1, TOM 3, TOM 1, TOM 2, TOM 4 |
| 23, 24, 27 | 24, 25, 28 | Closed HiHat A, Closed HiHat B, Foot Closed HiHat | CLS HAT |
| 25, 26 | 26, 27 | Half Open HiHat, Open HiHat | MID HAT, OPN HAT |
| 28, 29 | 29, 30 | Ride Cymbal, Ride Cymbal Bell | RIDE |
| 30 | 31 | Crash Cymbal | CRASH |
| 31-33 | 32-34 | Timbale, High Conga Slap, Low Conga Slap | PERC 1, PERC 2, PERC 2 |
| 34-43 | 35-44 | Small Wood Block, Large Wood Block, Rosewood Claves, Cabasa, Maracas A, Maracas B, Shaker, Agogo Bell, Medium Cowbell, Large Cowbell | none (silent) |
| 44, 45 | 45, 46 | Triangle, Tambourine | PERC 4, PERC 3 |
| 46 | 47 | Hand Claps | CLAPS |
| 47, 48 | 48, 49 | Finger Snap, Drum Sticks | none (silent) |

## What the manual leaves open

- **The tuning's steps.** The user manual says 32 steps over an octave and a half, so 9/16 of a semitone each.
  A 1988 review says semitones. The firmware doesn't settle it: it hands the 5 bits to the sound chip as they are,
  with no table. SM-87 follows the manual, and keeps a step exactly: a pad's TUNE and FINE (sixteenths), 9 a step.
- **The beats in BCD.** Settled: like the MMT-8's (found by mmt8utils), the HR-16's dump holds the beats + 1, in BCD.
  The factory dump's 16- and 32-beat patterns have 0017H and 0033H, and both firmwares' template for a new pattern
  has 0009H for its 8 beats. SM-87 writes them so; coming in, it reads a pattern's length from its waits, and from
  these two bytes only when it has none.
- **The pointers' bytes.** Patterns and songs are most significant first, the free memory least significant first, as
  the manual has it.
- **2D1-2D2** is "FF00H minus data in 0CDH & 0CEH" in the manual. 0CD-0CE are MIDI notes, so it must mean 2CD-2CE.
