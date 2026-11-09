# How it plays: measured on an X7000

What the sound settings (OUT and LFO) and the MIDI controllers do, measured on an X7000 in November 2026: test
samples (noise and a 989 Hz sine at 39555 Hz) sent by sample dump, their settings changed with SPRM between notes,
the output recorded and analysed (Axs7 `tools/x7000-lab`, results in Axs7's `doc/x7000-measurements.md`). Axs7's
engine follows these formulas; played through it, the same test notes measure within the figures given here.

Levels are relative: 0 dB is the loudest the sampler plays a sample (LEVEL 99, VEL>LEVEL 99, velocity 127).
Velocity v is the MIDI value, 1-127.

## Level

    level dB = -34.2 × (1 - LEVEL / 99)
             + (1 - a) × -7.7 + a × -40.3 × (1 - (v - 1) / 126)        a = VELLDN / 99

- **LEVEL** is linear in dB: 0.345 dB a step (Akai's spec says 0.375), **LEVEL 0 is -34.2 dB, not silent**.
- **VEL>LEVEL** mixes, in dB, a level that ignores velocity (7.7 dB under the top) with one that rises in a
  straight line with velocity, from -40.3 dB at velocity 1 to 0 at 127. At 0 every note is -7.7 dB; at 99 notes span
  40 dB. Within 1 dB of the measured at every amount and velocity tried (0, 25, 50, 75, 99 × 1-127).
- **No attack**: a note is at full level within the first millisecond, at any setting. (Axs7 ramps over 2 ms
  against clicks.)

## Filter

A **6th order Butterworth lowpass**: flat to the cutoff, -3 dB there, -36 dB an octave above, -72 two octaves above,
no resonance (no peak at any setting). It is a switched capacitor filter (like National's MF6-50): its clock is 50 ×
the cutoff, and at low settings that clock leaks into the output (a line at 15.7 kHz, -32 dBFS, at FILTER 0, with
the input's noise folded around it). It takes about 0.7 dB off below the cutoff.

    F  = clamp(FLTFRQ + VELFIL × (v - 64) / 64, 0, 99)
    fc = 306 Hz × 2^(F / 17.29)

- **FILTER**: 306 Hz at 0, 1.0 kHz at 30, 3.4 kHz at 60, 7.6 kHz at 80, 11.3 kHz at 90, **16.2 kHz at 99** (open).
  The formula is within ±0.9 semitone of every setting measured (0 to 95 in fives); the real steps zigzag a little.
- **The cutoff doesn't follow the key**: FILTER 60 gave 3.5 kHz played from C1 to B2.
- **VEL>FILTER** moves the cutoff from **velocity 64**, up above it and down below it, in FILTER steps, kept within
  0-99. At velocity 64 the cutoff is FILTER's; at VEL>FILTER 99 the velocities sweep the whole range.

To implement: three biquad (or state variable) lowpass sections in a row at fc, with Q = 0.5176, 0.7071 and 1.9319
(1 / (2 cos θ) for θ = 15°, 45°, 75°). Keep fc under about 0.45 × the output rate. The filter is per voice (its
cutoff follows each note's velocity).

## Release

After key off the level falls **exponentially** (a straight line in dB). The time to fall 60 dB, from RLEASE R:

    K   = 40 × 2^((50 - 10 × floor(R / 10)) / 10) × (1 - (R mod 10) / 20)
    T60 = 22.8 s / max(2, floor(K))

| RELEASE | 0 | 10 | 20 | 30 | 40 | 50 | 60 | 70 | 80 | 82-84 | 86-88 | 90-99 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Measured T60 (s) | 0.016 | 0.034 | 0.068 | 0.137 | 0.278 | 0.569 | 1.14 | 2.28 | 4.54 | 5.72 | 7.6 | 11.1-11.4 |
| Formula (s) | 0.018 | 0.036 | 0.071 | 0.143 | 0.285 | 0.570 | 1.14 | 2.28 | 4.56 | 5.70 | 7.60 | 11.4 |

It doubles every 10 steps and comes in steps at the top, the longest from 90 on: it looks like 22.8 s over a whole
number that halves every 10 steps and falls in a straight line between.

Per output sample the envelope is multiplied by `g = 0.001^(1 / (T60 × output rate))`, from the level at key off.

- **VEL>RELEASE follows the note off's velocity** (how fast the key comes up), not the note on's:

      R = clamp(RLEASE + VELRLS × (64 - off velocity) / 64, 0, 99)

  A note off at 64 leaves RELEASE as it is; 0 (a slow release of the key) makes it longer, 127 shorter. A **note on
  with velocity 0** counts as a note off at 64. The note on's velocity does nothing here (measured at RELEASE 80 and
  50, VEL>RELEASE 50 and 99, note on velocities 1-127 and note off velocities 0-127). Akai's "99 gives slowest
  release for low velocity" fits, with velocity meaning the note off's.
- The release applies to **one shots** too (the manual has it for looping sounds): a one shot let go early fades over
  the release; held past its END it stops there.
- A **drum trigger** (scan mode 9) plays to its END whatever the key and RELEASE do.
- Alternating samples release like looping ones.

## Vibrato (LFO)

The LFO is a **sine**, restarted at every key on from the middle going up. Its depth comes in **8 steps**:

    step 0: none     step n (1-7): ±(2.7 + 6.4 n) cents

Measured: ±9.1, 15.5, 21.9, 28.4, 34.2, 40.6 and 47.1 cents (the formula gives 9.1 to 47.5).

- **SPEED**: `rate = 1.65 + 0.0634 × LFSP` Hz: 1.7 Hz at 1, 4.8 at 50, 7.9 at 99, within 0.01 Hz. **0 stops the
  LFO** (no vibrato at all). A sample played two octaves under its rate got a 4 % faster LFO (the processor less busy,
  probably).
- **DEPTH**: `step = min(7, floor(8 × LFDP / 101))`: none up to 12, ±9 cents from 13, **±47 cents from 89** (Akai's
  spec says a semitone).
- **DELAY**: the vibrato comes in **a step at a time** from key on, a step every `2.35 / (100 - LFDL)` seconds:
  24 ms at 0, 47 ms at 50, 0.12 s at 80, 0.47 s at 95, 2.35 s at 99. So the full depth comes after DEPTH's step ×
  that. (Measured from 45 to 99; within a few percent above 70.)
- **Channel aftertouch** adds vibrato at once (no delay), at the sample's speed:
  `steps = floor(floor(pressure / 16) × PRESSURE / 99)`: a step for each sixteenth of the pressure at PRESSURE 99,
  half as many at 50. PRESSURE is the X7000's pressure sensitivity, set **per sample** in its LFO menu, and not sent
  over MIDI (see [02-sample-parameters.md](02-sample-parameters.md)).
- **Polyphonic aftertouch does nothing.**
- **The modulation wheel** (controller 1) adds `floor(value / 16)` steps, ±47 cents at full, **whatever PRESSURE
  is**, at once, at the sample's speed.
- Together, the depths about add up in cents (DEPTH's step's + aftertouch's + the wheel's). At the top it is off by
  up to ±5 cents: DEPTH 30 (step 2) with aftertouch 96 (step 6) gave ±51 cents, with 127 (step 7) ±68.

To implement: per voice, `pitch × 2^(cents × sin(2π × rate × t) / 1200)` with t from key on, cents the sum of the
steps' depths, the LFO's own step count rising with DELAY. The X7000's pitch moves in small steps of a few cents,
so its vibrato is slightly stepped; that isn't needed for a convincing sound.

## Pitch

    semitones = (constant pitch ? 0 : key - RECNP) + TRANSP + SFNTUN / 16 + bend + vibrato
    rate = rate(RECBW) × 2^(semitones / 12)

(See [03-samples-and-programs.md](03-samples-and-programs.md).) The sampler changes its playing rate per voice; a
player with a fixed output rate interpolates (Axs7 uses 4-point Hermite). The pitch wheel's range is PWRNGE in the
overall settings (Akai's spec, 0-12 semitones; not measured; Axs7 uses ±2).

## Voices

- **6 voices.** A note takes the first free voice (seen on the measured unit, one of whose voices, the 5th, is
  faulty and is only heard when 4 notes before it still sound). What happens with 7 notes wasn't measured.
- Voices differ a little: two voices' longest release measured 11.1 and 11.4 s (2.5 %).
- Each voice has its own filter, release envelope and LFO.

## MIDI, in short

| Message | Effect |
|---|---|
| Note on | Velocity sets the level (VEL>LEVEL) and the cutoff (VEL>FILTER) |
| Note off | Its velocity sets the release (VEL>RELEASE) |
| Note on, velocity 0 | A note off at velocity 64 |
| Channel aftertouch | Vibrato, scaled by the sample's PRESSURE |
| Polyphonic aftertouch | Nothing |
| Controller 1 (wheel) | Vibrato, not scaled |
| Program change n | Program n + 1 |
| Pitch bend | ± PWRNGE semitones (not measured) |

The unit measured received on channel 1. Sustain pedal, other controllers and mono modes weren't tried.

## Not covered

The filter's clock leak and aliasing at low cutoffs, the 0.7 dB the filter takes off, the output stage's own
response (with FILTER 99 at the top rate the output was -7 dB at 16 kHz, mostly the filter at 16.2 kHz), how the LFO
speed shifts with the playing rate, the stepped pitch within a vibrato cycle, voice stealing, and differences
between units.
