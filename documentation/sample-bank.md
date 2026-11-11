# The sample bank: samples put into a firmware package

SM-87's built-in samples aren't compiled in. They sit at the top of the firmware's slot, above the image, in what
the package's padding was: the sample bank. A drum kit can go there too, replacing the one the firmware was built
with. A tool puts them into a finished package (`.fwsc`) with no compiler or JieLi SDK. `tools/mkrom.py` does it in
Python, `web/sm87bank.js` in a browser. The formats are below.

The code: `app/s700/s7bank.h` (the format), `s7bank.c` (the firmware's reader), `s7mem.c` (finding it at boot),
`tools/mkrom.py` (`bank()`, `Package`), `web/sm87bank.js` (`makeBank`, `Sm87Package`). The tests:
`test/s7bank_test.c` (the reader), `test/bank_test.py` (a package with samples put in must be the very package
fm1-base's `tools/fm1pkg_make.py` makes of the firmware with them), `test/bank_js_test.mjs` (the JavaScript's bytes
must be `mkrom.py`'s).

## Where it is

The firmware's slot is app.bin, 581,564 bytes (`APP_SLOT`, 0x8DFBC), mapped from 0x02000120. The image comes first
(about 200 KB), then 0xFF to the slot's end. The bank ends at the slot's end, its footer last.

The firmware describes itself so a tool knows where its image ends. Once in the image, 4-byte aligned:

| Offset | Size | What |
|---|---|---|
| 0 | 12 | `SM87 S7 BANK` (ASCII, no NUL) |
| 12 | 4 | The bank format this firmware reads: 2 |
| 16 | 4 | Its own address (`self`) |
| 20 | 4 | `_data_load`: where .data's load image starts |
| 24 | 4 | `_data_start` |
| 28 | 4 | `_data_end` |
| 32 | 4 | `_app_xip_end`: the slot's end |

With the description found at offset `at` in app.bin, the addresses' base is `self - at`. The image ends at
`data_load + (data_end - data_start) - base` (fm1-base `device/app.ld` puts .data's load image last), and the slot
at `slot_end - base`, which must be app.bin's length. The room for a bank is the slot's end less the image's.

A firmware without the description (SM-87 before the bank, or another firmware) takes no bank. A different format
number means another version of the tool.

## The bank

A bank holds up to 16 samples for the sampler's slots, a drum kit for the HR-16, or both. Little-endian, every part
4-byte aligned. From the bank's first byte:

1. `count` sample entries of 188 bytes:

   | Offset | Size | What |
   |---|---|---|
   | 0 | 1 | Slot, 0-15 |
   | 1 | 1 | Pressure (aftertouch) amount, 0-99 |
   | 2 | 2 | Words, 1-32,752 |
   | 4 | 4 | Where its packed words are, from the bank's first byte (4-aligned) |
   | 8 | 24 | Its name: up to 23 printable ASCII characters, then NULs |
   | 32 | 155 | Its SPRM values (`documentation/akai-s700-x7000/02-sample-parameters.md`; k 128 included) |
   | 187 | 1 | 0 |

2. `kit_sounds` kit sound entries of 16 bytes. A kit's sound plays whole, once, at its rate. It has no loop or
   parameters of its own: the pads tune it and set its level and pan.

   | Offset | Size | What |
   |---|---|---|
   | 0 | 2 | Words, 1-32,752 |
   | 2 | 1 | Its rate code, 36-76 (the sampler's 41 rates, RECBW) |
   | 3 | 1 | Velocity's say in its level, 0-99 (VELLDN) |
   | 4 | 4 | Where its packed words are, from the bank's first byte (4-aligned) |
   | 8 | 8 | Its name: up to 6 ASCII characters, then NULs |

3. `kit_pads` pad entries of 16 bytes, the pads in order (the white keys from the lowest):

   | Offset | Size | What |
   |---|---|---|
   | 0 | 8 | Its name: up to 6 ASCII characters, then NULs. Empty: the pad shows as `PAD nn` |
   | 8 | 1 | Its sound: the kit's 0 to `kit_sounds - 1`, then the sampler's slots (`kit_sounds` + 0-15), or 0xFF for none |
   | 9 | 1 | Tuning, semitones, -24..24 (signed) |
   | 10 | 1 | Level, 0-99 |
   | 11 | 1 | Pan, -3 (left) .. 3 (signed) |
   | 12 | 1 | Its MIDI note, 0-127, in and out on the drum channel |
   | 13 | 1 | Group, 0-9: a hit cuts the others of its group (the hats) |
   | 14 | 1 | Voices at once: 1 (a hit cuts the last) or 2 (the cymbals) |
   | 15 | 1 | 0 |

4. The packed words: the samples' in their entries' order, then the kit's sounds'. Each is packed two words to three
   bytes (`app/s700/s7.h`: word `a` then `b` as `a & 0xFF`, `a >> 8 | (b & 15) << 4`, `b >> 4`; an odd count padded
   with a 0 word), then one 0 byte, then 0 bytes to a multiple of 4. `w` words take `(w + 1) / 2 * 3 + 1` bytes
   before the padding.

5. The footer, 32 bytes:

   | Offset | Size | What |
   |---|---|---|
   | 0 | 4 | 0x4B423753 (`S7BK`) |
   | 4 | 2 | Format: 2 |
   | 6 | 2 | `count`, 0-16 |
   | 8 | 4 | `size`: the bank's bytes before the footer |
   | 12 | 4 | CRC-32 (zlib's) of those bytes |
   | 16 | 2 | `kit_sounds`, 0-64 |
   | 18 | 2 | `kit_pads`, 0-16 |
   | 20 | 8 | 0 |
   | 28 | 4 | CRC-32 of the footer's first 28 bytes |

The bank starts at `slot_end - 32 - size`. Everything between the image's end and the bank stays 0xFF. With no
samples, the whole room is 0xFF: the package as built.

At boot the firmware takes the bank only if:
- both CRCs hold, and the format is 2;
- `count` ≤ 16, `kit_sounds` ≤ 64, `kit_pads` ≤ 16, and the bank fits the room;
- each entry checks: its words and their place, its name's NULs, a sample's slot, a sound's rate code and velocity,
  a pad's sound and the ranges above.

Otherwise it plays none of it: the slots get the test tone, as an X7000 without samples, and the drums the
firmware's own kit. Format 1 had no kit, and had its 12 bytes at 16 as 0.

The bank's CRC-32 names it in the saved sample directory. A different bank empties the slots the last one filled,
then fills the empty ones.

### The kit

A bank with a kit (`kit_sounds` or `kit_pads` not 0) replaces the kit the firmware was built with
(`resources/kit`), its sounds and its pads together. A pad's sound can also be one of the sampler's slots, as on the
drum page's PAD SOUND. Pads after `kit_pads` have no sound, each on the first General MIDI drum note no pad before it
has.

The settings keep the pads' set-up (PAD SOUND, PAD MIDI) and which kit it was made with: a 16-bit tag, the kit
entries' CRC-32 folded. When the kit changes, the pads take the new kit's set-up. A pattern's own sounds (VOICE,
TUNE, MIX) keep the kit's sounds by number, so after a change they play whatever the new kit has at those numbers.

An HR-16 dump coming in gives each drum a sound of its kind from the kit, found by its name's start: `KICK`,
`SNARE`, `TOM`, `CLHAT`, `OPHAT`, `RIDE`, `CRASH`, `CLAP` (`CYMBL` for either cymbal). A kit's sounds named so get
the HR-16's patterns played as they were meant.

The HR-16's 16 drums go on these pads (an HR-16 dump's hits do too: `app/hr16/hrdump.h` `HD_PAD`). For a full kit,
`web/sm87bank.js` has them as `HR16_DRUMS`, with a name, General MIDI note, group and voices for each, and
`hr16Pads()` makes a kit's 16 pads from them:

| HR-16 drum | Pad | Name | Note | |
|---|---|---|---|---|
| KICK | 1 | KICK | 36 | |
| SNARE | 2 | SNARE | 38 | |
| CLS HAT | 3 | CLHAT | 42 | group 1 |
| MID HAT | 7 | MDHAT | 44 | group 1 |
| OPN HAT | 4 | OPHAT | 46 | group 1 |
| CLAPS | 8 | CLAPS | 39 | |
| PERC3 | 15 | PERC3 | 62 | |
| PERC4 | 16 | PERC4 | 64 | |
| TOM1 | 9 | TOM1 | 50 | |
| TOM2 | 10 | TOM2 | 48 | |
| TOM3 | 11 | TOM3 | 45 | |
| TOM4 | 12 | TOM4 | 41 | |
| RIDE | 6 | RIDE | 51 | 2 voices |
| CRASH | 5 | CRASH | 49 | 2 voices |
| PERC1 | 13 | PERC1 | 56 | |
| PERC2 | 14 | PERC2 | 54 | |

### A kit from a sampler's programs (Axs7's SM-87 drums)

A tool that edits samples as an X7000 does can make a kit of them. Axs7 does (its DRUMS bank, `model/sm87.ts`): it
edits them as SM-87's drums, a program for each HR-16 drum, one keygroup each, tuned in whole semitones. Each drum's
first keygroup becomes its pad and a sound:
- **The sound:** the sample's words from START to END (reversed with REVERSE), its rate code, and VEL>LV as its
  velocity's say. It plays once, start to end: its loop, filter, release and LFO don't apply. Drums with the same
  sample, START, END and REVERSE share one sound.
- **Its name:** the drum's kind (`KICK`, `SNARE`, `CLHAT`, `OPHAT`, `CLAPS`, `TOM1`, `RIDE`, `CRASH`, `PERC1`, ...),
  so an HR-16 dump finds it.
- **The pad's tuning:** C3 (60) less the keygroup's root key, plus its transpose (with CONSTANT PITCH, the transpose
  alone), in semitones: -24..24. The pad plays the sample as the tool plays it on C3, so a new keygroup (root C3)
  is the sample at its own pitch, and the root or the transpose tunes it. A pad has no cents (fine tuning).
- **The pad's level:** the drum's own LEVEL (Axs7 keeps one with each drum's program), else its sample's LOUDNESS,
  0-99. A pad's level and the sampler's LOUDNESS take the same curve (`s7_level_db`), so the drum is as loud as the
  sampler would play the sample at that LOUDNESS, and two drums playing one sound can each have a level.
- **The pad's pan:** -3..3 (the X7000 has none).
- **The rest:** the drum's pad, name, note, group and voices from `HR16_DRUMS`. A drum without a keygroup has no
  sound.

## The package

fm1-base's `tools/fm1pkg_make.py` builds the package. Its `crc16` (polynomial 0x1021, initial 0), `enc` (JieLi's ENC
stream cipher) and `sfc` (ENC in 32-byte blocks, each keyed with `key ^ (offset >> 2)`) are the ones to use. Putting a
bank in, as `mkrom.py`'s `Package` and `sm87bank.js`'s `Sm87Package` do:

1. **The identity.** In the `.fwsc`, a marker byte follows each of the first 20 blocks of 0x2F bytes. Taken out,
   they leave the logical image (`web/fm1pkg.js` `logicalImage`). Keep them, to put back at the end.
2. **The UFW header and parts.** The logical image's first 0x40 bytes are the header, ENC'd with key 0xFFFF. Bytes
   8-9 are the number of parts, and a list of 0x50-byte entries follows, each ENC'd on its own. An entry has its
   type at 0 (0 is flash.bin), its CRC-16 at 4, its offset at 8 and its length at 12.
3. **The chip key.** In flash.bin's head, the 32-byte entries from 32 on are ENC'd with 0xFFFF. The one named
   `isd_config.ini` (its name at 16-31) gives at 4 where its 32-byte blob is; `chipkey_decode` turns the blob into
   the key. fm1-base's packages use 0x980F.
4. **The app area.** From flash.bin's 0x4000, SFC'd with the chip key from offset 0. Its first 32 bytes are the
   `app_area_head` entry: CRC-16 of its other 30 bytes at 0, CRC-16 of the area's bytes 32 to its size at 2, its
   size at 8. Decipher the area, rounded up to whole 32-byte blocks: they cipher back the same.
5. **app.bin.** Among the area's entries from 32, the one named `app.bin`: its offset in the area at 4 (0x120), its
   length at 8 (the slot). Put the bank in as above.
6. **The checksums again.** The app.bin entry's data CRC (at 2) is CRC-16 of app.bin, and its own (at 0) CRC-16 of
   its bytes 2-31. Then the `app_area_head` entry's, the same way. Cipher the area back.
7. **flash.bin's part.** Its entry's CRC-16 (at 4) is of flash.bin as stored. ENC the entry again. The header's
   CRC-16 at 2 is of the entry list as stored, and the one at 0 of the header's bytes 2-0x3F. ENC it again.
8. **The identity back:** the marker bytes where they were.

`test/bank_test.py` checks that the result is fm1pkg_make's package byte for byte, also with another chip key and
identity. It also checks that taking the bank out again gives the package as built. fm1-base's `web/fm1pkg.js`
`checkUfw`, which the web installer runs, takes the result.
