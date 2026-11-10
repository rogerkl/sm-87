# The sample bank: samples put into a firmware package

SM-87's built-in samples aren't compiled in. They sit at the top of the firmware's slot, above the image, in what
the package's padding was: the sample bank. A tool puts them into a finished package (`.fwsc`) with no compiler or
JieLi SDK. `tools/mkrom.py` does it in Python. Its format and the package's are below, for a tool in another language
(a page in the browser) to do the same.

The code: `app/s700/s7bank.h` (the format), `s7bank.c` (the firmware's reader), `s7mem.c` (finding it at boot),
`tools/mkrom.py` (`bank()`, `Package`). The tests: `test/s7bank_test.c` (the reader), `test/bank_test.py` (a package
with samples put in must be the very package fm1-base's `tools/fm1pkg_make.py` makes of the firmware with them).

## Where it is

The firmware's slot is app.bin, 581,564 bytes (`APP_SLOT`, 0x8DFBC), mapped from 0x02000120. The image comes first
(about 200 KB), then 0xFF to the slot's end. The bank ends at the slot's end, its footer last.

The firmware describes itself so a tool knows where its image ends. Once in the image, 4-byte aligned:

| Offset | Size | What |
|---|---|---|
| 0 | 12 | `SM87 S7 BANK` (ASCII, no NUL) |
| 12 | 4 | The bank format this firmware reads: 1 |
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

Little-endian, every part 4-byte aligned. From the bank's first byte:

1. `count` entries of 188 bytes:

   | Offset | Size | What |
   |---|---|---|
   | 0 | 1 | Slot, 0-15 |
   | 1 | 1 | Pressure (aftertouch) amount, 0-99 |
   | 2 | 2 | Words, 1-32,752 |
   | 4 | 4 | Where its packed words are, from the bank's first byte (4-aligned) |
   | 8 | 24 | Its name: up to 23 printable ASCII characters, then NULs |
   | 32 | 155 | Its SPRM values (`documentation/akai-s700-x7000/02-sample-parameters.md`; k 128 included) |
   | 187 | 1 | 0 |

2. Each sample's words, packed two to three bytes (`app/s700/s7.h`: word `a` then `b` as `a & 0xFF`,
   `a >> 8 | (b & 15) << 4`, `b >> 4`; an odd count padded with a 0 word), then one 0 byte, then 0 bytes to a multiple
   of 4. A sample of `w` words takes `(w + 1) / 2 * 3 + 1` bytes before the padding.

3. The footer, 32 bytes:

   | Offset | Size | What |
   |---|---|---|
   | 0 | 4 | 0x4B423753 (`S7BK`) |
   | 4 | 2 | Format: 1 |
   | 6 | 2 | `count`, 0-16 |
   | 8 | 4 | `size`: the bank's bytes before the footer |
   | 12 | 4 | CRC-32 (zlib's) of those bytes |
   | 16 | 12 | 0 |
   | 28 | 4 | CRC-32 of the footer's first 28 bytes |

The bank starts at `slot_end - 32 - size`. Everything between the image's end and the bank stays 0xFF. With no
samples, the whole room is 0xFF: the package as built.

At boot the firmware takes the bank only if both CRCs hold, the format is 1, `count` ≤ 16, the bank fits the room,
and each entry's slot, words, words' place and name's NUL check. Otherwise it plays none (the slots get the test
tone, as an X7000 without samples). The bank's CRC-32 names it in the saved sample directory: a different bank
empties the slots the last one filled, then fills the empty ones.

## The package

fm1-base's `tools/fm1pkg_make.py` builds the package. Its `crc16` (polynomial 0x1021, initial 0), `enc` (JieLi's ENC
stream cipher) and `sfc` (ENC in 32-byte blocks, each keyed with `key ^ (offset >> 2)`) are the ones to use. Putting a
bank in, as `mkrom.py`'s `Package` does:

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
