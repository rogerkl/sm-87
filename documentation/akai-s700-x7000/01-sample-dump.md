# The sample dump

The S700/X7000 moves audio with the **Prophet 2000 sample dump** (Akai's spec calls it "system exclusive common":
F0 7E with no manufacturer and no channel). It is close to, but not the same as, the later MIDI Sample Dump Standard:
the header is shorter, a block has 60 words and no packet number high byte, and there is no WAIT message.

Everything a sampler must know beyond the audio, rate and loop (START, scan mode, root key, programs, sound) comes
in a separate Akai exclusive message, see [02-sample-parameters.md](02-sample-parameters.md).

## Messages

| Message | Bytes | Direction |
|---|---|---|
| RSD, request sample dump | `F0 7E 00 nn 00 F7` | to the sampler; nn = slot from 0 |
| SD, sample dump | `F0 7E 01 nn 00 bb pp pp pp tt tt tt ss ss ss ee ee ee mm` + blocks + `F7` | both ways |
| ACKS, acknowledge | `F0 7E 7F F7` | the receiver, after the header and after each block |
| NAKS, not acknowledged | `F0 7E 7E F7` | the receiver: send that block again |
| ASD, abort | `F0 7E 7D F7` | either: stop the dump |

All numbers below are hex unless they say otherwise.

### The header: 19 bytes

| Byte | Value | |
|---|---|---|
| 0 | F0 | System exclusive |
| 1 | 7E | Common non-real-time |
| 2 | 01 | Sample dump |
| 3 | nn | Slot, from 0. The sample replaces slot nn + 1 on the display. |
| 4 | 00 | Reserved |
| 5 | bb | Bits per word: the S700 sends 12 (0C), accepts 8 to 14 |
| 6-8 | TB | Sampling period in **ns**. The S700 accepts 15259 to 500000. |
| 9-11 | TB | Total words in the sample. The S700 accepts 200 to 32752 and keeps the first 32752. |
| 12-14 | TB | Loop start. If >= total - 5, the sample doesn't loop. |
| 15-17 | TB | Loop end, after the last word of the loop. **The sampler takes it as the sample's end.** |
| 18 | mm | 0 looping, 1 alternating. One shot when the loop is shorter than 5 words. |

TB is a 21-bit number in three bytes, low 7 bits first: `v = b0 + (b1 << 7) + (b2 << 14)`.

The header carries no START point (the sampler takes 0 until an SPRM raises it) and no root key: the sampler takes
C3 (MIDI 60) as the root until an SPRM says otherwise.

### Data blocks: 122 bytes each, straight after the header

| Byte | |
|---|---|
| 0 | Block number, low 7 bits only (0, 1, ... 127, 0, 1 ...) |
| 1-120 | 60 words, two bytes each (SW) |
| 121 | Checksum: XOR of bytes 1-120 |

SW is a 12-bit word in two bytes, **offset binary** (800h is zero, 0 is the most negative, FFFh the most positive):

    first byte:  0 d11 d10 d9 d8 d7 d6 d5
    second byte: 0 d4 d3 d2 d1 d0 0 0

    word = (first << 5) | (second >> 2)
    signed = word - 2048

The last block is padded to 60 words (with zero bytes, Akai's example; Axs7 pads with 800h, silence). After the last
block comes `F7`, so **the whole dump is one system exclusive message**, F0 at the header to F7 after the last
block, even though it is acknowledged block by block.

A full sample (32752 words) is 546 blocks: 19 + 546 × 122 + 1 = 66632 bytes, **21.3 s** at MIDI's 3125 bytes a
second.

## Handshake

Akai's spec: "S700 is always a slave. It will never send unless it is told to." Sending from the sampler needs MIDI
both ways; it can receive over a single cable.

**Receiving from the sampler (closed loop):**

1. Send RSD with the slot.
2. The sampler sends the header and waits for ACKS.
3. ACKS; it sends a block and waits; ACKS; ... ; after the ACKS for the last block it sends F7.

Akai's extension: in closed loop the gap before an ACK may be up to **10 seconds** (the Prophet's standard has
20 ms), so a slow receiver can take its time. NAKS asks for the block again; ASD stops the dump.

**Sending to the sampler:**

- Open loop (one cable): send everything at full speed. The X7000 takes a whole 546-block dump sent in one go without
  any pause or waiting for ACKs (verified on an X7000). It still sends ACKS back if it is connected.
- Closed loop: wait for the sampler's ACKS after the header and each block, or for NAKS to resend.

Akai's spec also has the S700 timeout for its own exclusive messages (2 s between bytes); the sample dump has the
10 s closed loop gap.

### Example, from Akai's spec

The computer asks for the first sample; the sampler answers with a header for 32752 words (`70 7F 01`) at 26400 Hz
(period 37879 ns: `77 27 02`), loop start 32692 (`34 7F 01`), loop end 32752, looping. (Akai's text repeats
`77 27 02` for the loop end, a slip: the loop end is the last word + 1, here the total, `70 7F 01`.)

    computer: F0 7E 00 00 00 F7
    sampler:  F0 7E 01 00 00 0C 77 27 02 70 7F 01 34 7F 01 70 7F 01 00
    computer: F0 7E 7F F7
    sampler:  00 40 00 40 00 ... (block 0: 60 words of silence) cs
    computer: F0 7E 7F F7
    ... 546 blocks ...
    sampler:  F7

A small dump as Axs7 builds it, 100 words looping from 20 to 100 at 39555 Hz into slot 2 (nn = 1):

    F0 7E 01 01 00 0C 41 45 01 2C 02 00 14 00 00 64 00 00 00   header: period 25281 ns, 300 words (see below),
                                                                loop 20 to 100, looping
    00 40 00 46 1C 4C 14 51 54 56 34 5A ...                     block 0
    ... 5 blocks ... F7

## What the X7000 does in practice

Verified on an X7000 (2026-11), unless marked:

- **Slots above 5**: with the 16 slot memory board, slot numbers 6-15 address slots 7-16, both receiving and sending
  (verified with slot 7). Akai's note that a number above 5 replaces "the sample being edited" is for a standard
  unit.
- **It sends START to END only.** The sampler dumps the part of its memory from START to END; the header has no
  START. With START after the loop point (valid on the sampler: it plays from START and loops back), the loop can't
  be expressed and a dump is tiny. To get all of a slot: set START to 0 and END to 32752 with SPRM first, dump, then
  set the parameters back (Axs7 does this, verified).
- **The sampling period in its headers can be garbage**: an X7000 was seen sending 1815780 ns (outside 15259-500000).
  Take the rate from the parameters' rate code (RECBW) instead, see
  [03-samples-and-programs.md](03-samples-and-programs.md). Later dumps had valid periods (63704 ns for code 60).
- **The rates are a fixed set**: the sampler rounds a received period to one of its 41 rates (a semitone apart), so a
  sample at another rate plays out of tune. Senders resample to one of them first.
- **Blind ACKS work**: a receiver that can't see the blocks as they come (a browser delivers the dump only at F7)
  can send ACKS at a steady pace instead, one every 50 ms (a block is 39 ms on the wire). The sampler sends the next
  block when it gets an ACKS.
- **Senders announce 200 more words than they send** (Axs7, and the Java tool before it): total words = samples +
  200 (at most 32752), and blocks = floor(total / 60), so often one block fewer than the total announces. The X7000
  accepts that; without the extra words the end of a loop got a click on the sampler (the "loop fix"). The loop end
  is the real end. **A receiver should accept fewer blocks than announced, F7 early, and take the loop end as the
  end.**
- After a dump the sampler has START 0, END at the loop end, root C3, and the rest of the slot's previous settings
  (unverified in detail): send the SPRM after the dump, not before.

## A receiver's checklist

For a device that takes samples from an X7000, an S700 or Axs7:

- Parse the header; take bits per word (12 from Akai machines), the total, the loop and the mode.
- Read 122-byte blocks until F7. Check each block's number (mod 128) and XOR; in closed loop answer ACKS, or NAKS for
  a bad block. Ignore real-time bytes (F8-FF) if they turn up inside the message: senders may put F8 between blocks
  as filler.
- Stop at F7 even if fewer blocks came than the total says.
- The sample ends at the loop end. A loop shorter than 5 words is a one shot.
- Expect an SPRM for the same slot next with START, the root key, the rate code and the sound; until then use
  START 0, root 60 and the period from the header (or the default rate if it is invalid).
- To send to a sampler: one message, header and blocks; announce total words + 200; then the SPRM.
