# Akai S700 / X7000: a reference for playing their samples

What a device needs to receive, store and play samples the way an Akai S700 or X7000 does: the MIDI messages
(sample dump and Akai's parameter messages), how a sample and its settings are laid out, and how the sampler
actually sounds, measured on an X7000. Written from the work on Axs7 (github.com/rogerkl/Axs7), a browser
re-creation of the sampler that sends and receives samples to and from a real X7000.

The S700 (a rack) and the X7000 (a keyboard) are the same sampler: the X7000's MIDI implementation is the S700's,
and both save the same QuickDisk format.

## Files

| File | |
|---|---|
| [01-sample-dump.md](01-sample-dump.md) | The sample dump: request, header, data blocks, handshake, both directions, and what the X7000 does in practice |
| [02-sample-parameters.md](02-sample-parameters.md) | Akai exclusive messages: requesting and setting a sample's parameters (RSPRM / SPRM), overall settings, byte by byte |
| [03-samples-and-programs.md](03-samples-and-programs.md) | A sample in memory: points, scan modes, rates, pitch, programs and key ranges, the defaults, storage sizes |
| [04-playback.md](04-playback.md) | How it sounds, measured: filter, level, release, velocity, vibrato, aftertouch, voices, with formulas to implement |
| [05-quickdisk.md](05-quickdisk.md) | The .qd disk image (Gotek/HxC) of an S700/X7000 disk: one sample and its parameters |

Reference code, in the Axs7 repo: `web/src/protocol/akai.ts` (sample dump), `sprm.ts` (parameter messages),
`s700disk.ts` and `qd.ts` (disk images), `web/src/model/params.ts` (the 0-99 tables), `engine/src/voice.rs`,
`filter.rs` and `envelope.rs` (playback, Rust).

## How sure each part is

Each statement comes from one of these, and the files say which where it matters:

- **Akai's spec**: "MIDI system exclusive data format for S700" (Akai, 1986), the only official description. It survives
  as a mailing list post (Synth-diy, 1998). It isn't in this repository, being Akai's ([../manuals.md](../manuals.md)).
  Some of its numbers are decimal without saying so (the S700 identifier "65" is 41h), and it has a few slips, noted
  where they matter. The S700's owner's manual is listed there too.
- **Verified on an X7000**: tried against the user's X7000 (with the 16 slot memory board) from Axs7, 2026-11-01 to
  2026-11-08: receiving and sending samples, bit for bit round trips, reading and setting parameters.
- **Measured**: recorded from the X7000's output with test samples, 2026-11-08 (Axs7 `tools/x7000-lab`,
  `doc/x7000-measurements.md`). One unit, so small differences between units are possible.
- **Axs7's choice**: how the app does something the sampler doesn't define.

## The short version

- A sample is up to **32752 12-bit words**, at one of 41 rates a semitone apart (3.9 to 39.6 kHz), with START, END
  and LOOP points and a scan mode (one shot, looping, alternating, drum trigger).
- A standard unit has **6 sample slots**, 16 with the memory board. Slots are numbered from 0 on MIDI (display - 1).
- **32 programs**, each a key range per sample (low key, high key, transpose, constant pitch). A sample is in a
  program when its range there is valid (low <= high).
- The sound settings (LFO, filter, release, loudness and their velocity amounts) belong to the **sample**, not the
  program.
- On MIDI: the **sample dump** (F0 7E ...) carries the audio, the loop and the rate; **Akai exclusive SPRM**
  (F0 47 ...) carries everything else. A sample sent to a sampler is a dump followed by an SPRM.
- **6 voices**, a 6-pole lowpass per voice (switched capacitor, no resonance), exponential release, a sine LFO for
  vibrato in 8 depth steps.

## Sizes for a player with little memory

A full sample is 32752 words of 12 bits: **49128 bytes** packed (two words in three bytes), **65504 bytes** as 16-bit
words. 500 KB holds 10 full samples packed, 7 as 16-bit. A disk image's block is 49348 bytes (the sample in
32768 words as low nibbles plus high bytes, and the parameters).
