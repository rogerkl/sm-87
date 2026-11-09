# QuickDisk images (.qd)

The S700 and X7000 save one sample a disk side on 2.8" QuickDisks. A Gotek (FlashFloppy) or an HxC floppy emulator
uses image files (.qd) in their place, and sample libraries come as such images. This is how to read one, worked out
from 99 S700 library images and verified by writing images the same way cell for cell (Axs7, 2026-11; images Axs7
writes weren't yet tried on hardware).

## The image file

| Offset | |
|---|---|
| 0 | Header, 512 bytes: "HXCQDDRV..." or "\0\0\0QD". FlashFloppy only checks for "QD" at bytes 3-4. |
| 200h | Track table: u32 LE offset of the track (400h), u32 its length in bytes, u32 start and u32 end of the read/write window (in bytes from the track's start) |
| 400h | The track: one track holds the whole spiral of a disk side |

The track is **cells**, 8 to a byte, the **first cell in the lowest bit**, 4.917 µs each (about 203.4 kHz). Outside
data it is filled with 11h (a 1 cell in every four).

## Blocks on the track

A block starts with a run of 1 bits, then **sync bytes 16h** and a **marker A5h**, then the data. Bits are **MFM**:
each bit is a clock cell then a data cell; the data cell is the bit; the clock cell is 1 only between two 0 bits.
Bytes go **least significant bit first**.

To find a block: look for the cells of seven 16h bytes and A5h (skip the very first clock cell, which depends on the
bit before). Read bytes from the first sync byte on: take every second cell (the data cell), 8 to a byte, LSB first.

**CRC-16/ARC** (polynomial 8005h reflected, i.e. A001h, starting at 0) over the bytes from the marker to the block's
end, the CRC included, gives 0. A disk may hold an older, damaged copy too: use the first block whose CRC is right.

## The S700 block: C0C4h bytes from the first sync byte

| Offset | |
|---|---|
| 0-6 | Sync, 16h |
| 7 | Marker, A5h |
| 8-17h | "S700 FORMAT 0.00" |
| 28h-A7h | 32 programs × 4 bytes: KEYL, KEYH, TRANSP (+ constant pitch in the top bit), ATPTCH |
| A8h | RPMODE |
| A9h | SSTRT (u16 LE) |
| ABh | END - START - 1 (u16 LE) |
| ADh | SEND, END + 1 (u16 LE) |
| AFh | SLOOP (u16 LE) |
| B1h | END - LOOP - 1 (u16 LE) |
| B3h-BFh | LFSP, LFDP, LFDL, FLTFRQ, RLEASE, LDNSS, SFNTUN, RECNP, RECBW, RVRSE, VELFIL, VELLDN, VELRLS, a byte each |
| C0h-C1h | Undefined |
| C2h | Audio: 4000h bytes of **low nibbles**, two words a byte (the even word in the low nibble) |
| 40C2h | Then 8000h bytes of the **high 8 bits** of each word |
| C0C2h | CRC, low byte first |

That is 32768 words of 12 bits, offset binary as in a sample dump: `word[i] = (high[i] << 4) | nibble(i)`. (Read as
two's complement they come out as full-scale noise.)

The parameter bytes from 28h to C1h are the sample parameter message's values ([02-sample-parameters.md](02-sample-parameters.md))
one byte each and words little-endian, **without** SPRM's undefined value between the programs and RPMODE. The rate
comes from RECBW; the disks seen all have START 0.

## Writing one as the S700 does

What Axs7 does to make an image the X7000's disks look like (an image of 204800 bytes, a track of 203417 bytes,
window 12713 to 177989): fill with 11h, then MFM 0 bits (a clock cell each) from cell 134273, then at cell 144453 a
**marked F0h byte** (a clock cell before its first 1 bit, where MFM has none, so no data can look like it), ten sync
bytes in all, the marker and the block, eight sync bytes, MFM 0 bits again, and the fill from cell 1177776.

## The S612

The S612's disks (same drive) hold 7E00h words of 12 bits at 31250 Hz in 16-bit words (the 12 bits at the top) from
offset 28h, after 32 bytes of settings that aren't decoded. Mentioned only to tell them apart: no "S700 FORMAT" text.
