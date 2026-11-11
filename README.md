# SM-87

A firmware for the M-VAVE FM-1 that recreates a 1986/87 studio: an **Alesis MMT-8** style part/song sequencer
driving an **Akai S700 / X7000** style sampler and an **Alesis HR-16** style drum machine. It is built on
[fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0, a git submodule here) and felucca-bsp.

**Status: works in fm1-base's host simulator, and builds for the FM-1 (`app/build/sm87.fwsc`). It hasn't run
on an FM-1 yet** (see *On the device*).

## What it does

- **The sampler** plays samples the way an X7000 does. Every formula comes from the measurements in
  [documentation/akai-s700-x7000/04-playback.md](documentation/akai-s700-x7000/04-playback.md), ported from Axs7's
  engine (`../Axs7SdsW/engine`):
  - 6 voices.
  - 12-bit words, packed two to three bytes.
  - 4-point Hermite interpolation.
  - A 6-pole Butterworth lowpass whose cutoff follows velocity.
  - LEVEL and VEL>LEVEL as measured.
  - An exponential release whose time follows the note off's velocity.
  - Vibrato: a stepped sine, plus the mod wheel and aftertouch.
  - One shot, loop, alternating and drum trigger.
  - 32 programs of key ranges.
  - The S700's MIDI modes: OMNI OFF (its channel), OMNI ON (every channel), and MULTI PROGRAM, where channel 1-16
    plays program 1-16 (or 17-32), so each MMT-8 track can play its own program.
- **Sample memory** is 299 KB of flash: 6 full-length samples, as many as a stock S700 holds, or all 16 slots with
  shorter ones. Samples play in place from the flash. An empty slot holds the X7000's test tone.
- **Samples over MIDI.** The FM-1 answers as an X7000 with 16 slots: the sample dump in and out, SPRM/RSPRM and
  OVS/ROVS on the sampler's channel. So [Axs7](https://rogerkl.github.io/Axs7/) sends and fetches samples as it does
  with an X7000, and it can bring them from WAV files, `.qd` disk images or a real X7000.
- **The sequencer** works like the MMT-8:
  - 100 parts of 8 tracks and 100 songs of up to 255 steps, at 96 clocks a beat, in 64 KB of event memory.
  - Real-time recording replaces a track like tape. The first take on an empty part sets its length. Recording
    starts after a bar's count-in.
  - Track mutes, saved per song step.
  - A part chosen while playing starts at the end of the current one.
  - Per-track MIDI channel, or UNCHANGED.
  - MIDI echo to the record track's channel.
  - MIDI clock and song position out, and MIDI clock in: the sequencer can follow another's clock and transport.
  - The MMT-8's memory dump over USB, both ways: an MMT-8's dump replaces the sequencer's memory, and ours goes out
    in the same format.
  - The MMT-8's functions: LENGTH (at the end or the top), ERASE and COPY with its 22 filters, MERGE, QUANTIZE (10
    grids, 4 modes), TRANSPOSE, MIDI CHANNEL, NAME, and EDIT, the event list editor; in a song ERASE, COPY,
    QUANTIZE (each track's clock offset, ±48), NAME and EDIT, its steps put in and taken out.
- **The drum machine** is a cut-down HR-16:
  - 16 pads on the white keys and on MIDI channel 10, each with its own sound, tuning (±24 semitones), level, pan
    (the HR-16's seven places) and note. The hats cut each other (a group); the crash and ride ring over themselves
    (two voices).
  - The sounds are the sampler's format (12 bits, at its rates) and play on its voices, without the filter: a kit
    built into the firmware (`resources/kit`), or any of the sampler's 16 slots.
  - 100 patterns of up to 682 beats, in the sequencer's memory. Recorded as they loop, each hit quantized (1/4 ..
    1/64, or off) and swung (50-75%) as it goes in, the HR-16's way. Its STEP EDIT, OFFSET and FILL too.
  - A pattern can keep its own sounds, as the HR-16 stores VOICE, TUNE and MIX with a pattern; one without them
    plays the pads' global set-up.
  - The HR-16's memory dump over USB, both ways: an HR-16's dump replaces the drum patterns, and ours goes out in the
    same format, also when an HR-16's request for one comes.
  - With the MMT-8: one clock and tempo. A song step chooses the drum pattern that plays with it (or none). Playing
    a part, the drums loop their own pattern beside it. On their own page they play alone.
- **The screen** is a 2 × 16 character LCD, drawn dot by dot, with a status strip under it.

## Controls

| | |
|---|---|
| Keys | Play the sampler on its channel (and out over USB); OCT- / OCT+ shift them |
| SEQ | The sequencer's page; again (stopped): PART ↔ SONG |
| PLAY | Play from the top / stop; SEL + PLAY continues from the beat |
| REC | Arm recording on the part's lowest empty track (or the one chosen with REC + its key); again: disarm; while a take runs: punch out |
| SEL + first 8 white keys | Track mutes (on the SEQ page; in a song: the step's mutes); the 9th: the drums on or muted |
| REC + first 8 white keys | The record track |
| PRESET | The part or song (SEQ page), else the sampler's program |
| K1 / K2 (SEQ) | Tempo / the record track's channel (UNCHANGED, 1-16) |
| K4 (SEQ) | The drum pattern that plays with the parts; in a song (stopped): the step's drum pattern, or none (the step's row shows it: `P05 12345678 D07`, `D--` none) |
| ALGO (SEQ) | The MMT-8's functions, below; SEQ goes back to the part or song |
| SELECT / K3 (SONG, stopped) | The step / its part (at the END: appends the last part; past 99: the song ends there) |
| ARP | The drum machine's page (below); from one of its functions: back to the pattern |
| EDIT | The sampler's pages round: POINTS (START, END), LOOP (LOOP, MODE), TUNE (ROOT, FINE, RATE), KEYS (LO, HI, TRANSPOSE, CONSTANT), CLEAR (REC empties the slot) |
| ENV | OUT (FILTER, RELEASE, LEVEL), then the velocity amounts |
| LFO | SPEED, DEPTH, DELAY, PRESSURE |
| SELECT / K1-K4 (sampler pages) | The slot / the page's values. The strip under the LCD shows the slot, its sample and the program |
| HOME | The sampler's overview |
| GLO | MIDI: K1 the sampler's channel, K2 its mode (OMNI OFF, ON, MULTI), K3 the pitch bend's range, K4 the drums' channel. GLO again: the MIX (below) |
| SAVE | Lit when something isn't saved: settings, the sample parameters, the sequencer (stopped) |

### MIDI modes (GLO)

The S700's MIDI modes, from its manual (33-37), set with K2 on the GLO page:
- **OMNI OFF** (as before): the sampler hears its channel (K1) and plays the program shown (PRESET).
- **OMNI ON:** every channel plays the program shown.
- **MULTI** (the S700's MULTI PROGRAM / POLYPHONIC): channel 1 plays program 1, channel 2 program 2, up to 16. The
  6 voices go to whichever channel needs one. While a program of 17-32 is shown, channels 1-16 play programs 17-32
  instead (the manual says "or P17-32" without saying how; this is a guess). A program change on the sampler's
  own channel chooses the program shown; one on another channel is ignored, since that channel's program is fixed.
  Pitch bend, the mod wheel and aftertouch are each channel's own.

In MULTI the keys play the program shown on its own channel: with P5 shown they play channel 5. A track you record
from the keys then plays P5 when its channel is UNCHANGED. To give an existing track a program, set its channel
(K2 on the SEQ page, or MIDI CHANNEL) to that program's number.

Channel 10 is the drums' too. In MULTI it also plays program 10.

OVS (Akai's overall settings) carries the mode both ways.

### The mix (GLO, then GLO again)

`LEV PAN SLOT DRM` / `99  L20  01  99`:
- K1 sets a sampler slot's level (0-99, a fader's curve), and K2 its pan (L50 .. C .. R50).
- K3 or SELECT chooses the slot.
- K4 sets the drums' bus level; the pads are panned on their own (PAD SOUND).

The S700 is mono, so by default every slot is at full level in the middle, and sounds as before. The level and pan
belong to the slot, like a mixer's channel strip, so a sample sent to the slot keeps them. They act at once, on notes
already sounding too, and are kept in the settings (SAVE).

### The drum machine (ARP)

The LCD shows the pattern and its beat (`PATT 05  BT 003`, `PATT 05>07` with 07 queued), then its length and hits,
or while recording the quantize and swing. The strip shows the 16 pads, lit as they sound.

| | |
|---|---|
| White keys | The 16 pads, by velocity (the black keys do nothing here) |
| PLAY | The drums alone, from the top / stop; SEL + PLAY: on from the beat |
| PRESET | The pattern; while playing, queued for the end of the one playing |
| K1 / K2 / K3 | Tempo / quantize (1/4, 1/8, 1/12, 1/16, 1/24, 1/32, 1/48, 1/64, off) / swing, 50-75% |
| REC | Recording on or off: the hits played (pads, or MIDI in on the drums' channel) go into the pattern as it loops |
| SEL + a pad, recording | Erases that pad's hits as they come by, while held (the HR-16's ERASE + a pad) |
| OCT+ held | FILL: a pad held repeats at the quantize rate (off: 1/32), at the velocity it was hit at, while the drums play; recorded when recording. (The pads have no octave: OCT- and OCT+ don't change it here) |
| ALGO | The functions below; ARP goes back to the pattern |

A new pattern is 8 beats long, as on the HR-16. A hit is placed on the nearest step of the quantize grid. It's heard
as it's played, and played back from the next time round. Recording counts the beats with the click. It records
when the drums play alone, or beside a part; it doesn't record in a song.

| Function | K1 | K2 | K3 | K4 | REC |
|---|---|---|---|---|---|
| STEP EDIT | The hit's place, a clock (SEL: a step) | Its velocity | Its pad | The grid | Erases the hit shown |
| LENGTH | Beats, 1-682 (SEL: tens) | | | | Sets the pattern's length (stopped): hits past the end go |
| ERASE | A pad, or all (a pad's key chooses it) | | | | Erases that pad's hits, or the whole pattern |
| COPY | The pattern to | | | | Appends the pattern to the other (an empty one gets a copy; itself: twice as long) |
| OFFSET | Clocks, -99..99 (384ths; SEL: tens) | A pad, or all (a pad's key chooses it) | | | Moves the hits that much later (+) or earlier; past the end round to the start, and back. Again: as much again |
| PAD SOUND | The sound: the kit's, then SLOT01-16 | Tuning, ±24 | Level | Pan, <3 .. 3> | Keeps the pads' sounds with the pattern; SEL + REC: lets them go (the global set-up again) |
| PAD MIDI | Its note | Its group (pads that cut each other) | Voices, 1 or 2 | The drums' channel | The pad as the kit sets it (sound and MIDI); SEL + REC: all of them |
| MIDI DUMP | | | | | Sends the drum patterns as an HR-16's SysEx dump, over USB (below) |

On a pad's function, a pad's key chooses it (and plays it); so does SELECT.

**A pattern's own sounds** are the HR-16's VOICE, TUNE and MIX stored with a pattern: each pad's sound, tuning,
level and pan. PAD SOUND's top line says whose the pads have:
- **`01 KICK   GLOBAL`**: the pattern has none of its own and plays the global set-up. An edit changes the set-up,
  for every pattern without its own, and is kept with the settings (as before patterns had sounds).
- **`01 KICK   PAT 05`**: pattern 05's own. An edit shows `PAT 05*` and lasts until REC stores it, or until another
  pattern is chosen or comes in, which loses it, as on the HR-16.
- **REC keeps the pads' sounds with the pattern**, all 16 pads at once. **SEL + REC lets them go**: the pattern plays
  the global set-up again.
- **The pads change with the pattern** as it starts: one queued, or a song step's. When stopped, they have the
  chosen pattern's sounds.
- **COPY** to an empty pattern copies its sounds too; appending to a pattern with hits keeps that pattern's. ERASE
  keeps them.
- **A pad's MIDI** (note, group, voices) is the same in every pattern.
- Each pattern's sounds take 64 bytes of the sequencer's memory. They keep a kit sound by its number and a slot as
  the slot, so a kit grown later leaves them as they were. A memory saved before this loads with none.

**STEP EDIT** is the HR-16's step mode. `STEP 001 + 24/96` is beat 1, clock 24 of 96, and below it is the hit there
(`CLHAT  V 40 2/2`: the closed hat at velocity 40, the second of two hits), or EMPTY.
- **SELECT** goes from place to place, forwards or back. It stops at each step of the quantize grid (swung, as
  recording places hits) and at every hit, on the grid or off it. Where there are several hits, it shows one at a
  time. With SEL held it goes by beats. At the pattern's ends it goes round.
- **Each place reached sounds** its hit. The strip and the white keys light the pads with a hit there.
- **A pad played adds a hit** at the place, from a white key or MIDI in. A pad that already has a hit there gets the
  new velocity, as on the HR-16.
- **K1 moves the hit shown**, a clock at a time, or a step with SEL held. It stops before another hit of the same
  pad. K2 changes the hit's velocity, K3 its pad (skipping pads with a hit there), and K4 the grid.
- **REC erases the hit shown.**
- **PLAY leaves STEP EDIT** and plays from the top.

**The HR-16's memory dump**, as its service manual has it (`documentation/hr16-sysex.md`):
- **In:** send an HR-16's dump to the FM-1 over USB at any time (on an HR-16, with SYSEX ENABLE on: TAPE held, the
  left arrow once, RECORD). The LCD counts the bytes. The dump replaces all 100 drum patterns, as it would on an
  HR-16; the MMT-8's parts and songs stay. SAVE keeps it.
- **What comes across:** each pattern's hits, its length, and its VOICE, TUNE and MIX as the pattern's own sounds;
  the swing and quantize, as the recording's (its 1/6 becomes 1/8).
- **What doesn't:**
  - Its songs: SM-87's drums follow the MMT-8's songs. When the dump has any, the LCD says `SONGS NOT TAKEN`.
  - Its MIDI settings and tempo. The drums keep SM-87's notes and channel, so the MMT-8's tracks still play them.
    Those bytes are kept, though, and go back out in ours.
- **Its drums on the pads**, by role:

  | Pads 1-6 | Pads 7-8 | Pads 9-12 | Pads 13-16 |
  |---|---|---|---|
  | KICK, SNARE, CLS HAT, OPN HAT, CRASH, RIDE | MID HAT, CLAPS | TOM 1-4 | PERC 1-4 |

- **Its sounds** become the kit's sound of the same kind: kicks the KICK; snares, the rimshot, sidestick and brush
  the SNARE; the closed and foot hats CLHAT; the half-open and open hats OPHAT; the rides and the crash the CYMBL.
  The kit has no toms, claps or percussion, so a pad with one of those keeps the sound it has in the global set-up.
  To hear them, give pads 7-16 sounds (sampler slots, say) on PAD SOUND before the dump comes in.
- **Dynamics and tuning:** the HR-16's 8 dynamics become velocities 15, 31 .. 127, and go back the same. Its 32
  tuning steps span an octave and a half, so each becomes the nearest semitone (-9 to +8). Going out, a tuning
  beyond that range goes as the HR-16's highest or lowest step.
- **Out:** MIDI DUMP (the drum page's last function), REC. An HR-16's request for a dump (`F0 00 00 0E 01 07 F7`)
  sends it too, unless the FM-1 is playing, as an HR-16 does. Its MIDI test is answered.
  - Each pad's sound goes as one of the HR-16's 49 of its kind: on each drum, the one most patterns of the last HR-16
    dump had there, if it is that kind. A pad playing a sampler slot, or nothing, goes as that drum's sound in the
    last HR-16 dump.
  - As with the MMT-8, the header comes from the last HR-16 dump taken in, so **send the FM-1 one of your HR-16's
    dumps first** (MIDI DUMP's strip says `HEADER: AN HR-16'S` then). Without one, the header is made up: the notes
    the pads', the channel the drums', the tempo the sequencer's, and zeros where the manual says not to alter. An
    HR-16 may not take that.
  - The FM-1 takes its own dump back the same, byte for byte. A real HR-16 taking one is untested, and so is the
    sound list's numbering (the manual gives none; `documentation/hr16-sysex.md` says where it's from).

**With the sequencer**, the two share one clock, one tempo and one transport. Which pattern plays depends on how
PLAY was started:

- **From the SEQ page, in PART mode:** the sequencer's PLAY plays the part, and the drums loop their pattern beside
  it, each on its own length. K4 chooses the drum pattern, queued while playing. SEL + the 9th white key mutes the
  drums.
- **From the SEQ page, in SONG mode:** each step's drum pattern (K4 on the step, or in the song's EDIT) starts with
  the step, and is cut at the step's end. A step without one is quiet. A new step takes the drums of the step
  before. A step lasts as long as its part: a part without a length (an empty one) is passed over, as the MMT-8
  does, and its drums with it.
- **From the drum page:** PLAY plays the drums alone, as pressing PLAY on an HR-16 would.

The drums' hits go out over USB on the drums' channel (10 unless set), and notes coming in on that channel play the
pads. So an MMT-8 track on channel 10 plays them too.

### Recording a part

1. SEQ, and PRESET to the part. The LCD's top left says PART (SEQ again switches to SONG, which plays parts but
   doesn't record).
2. REC: it lights, and the LCD shows `REC TRK 1`, the part's lowest empty track. REC + a track key chooses
   another; it stays chosen until you change part.
3. PLAY: four clicks of count-in (`COUNT IN 4` .. `1`), then it records from beat 1. Notes in the count-in aren't
   recorded, except one played in its last sixteenth, which lands on beat 1.
4. PLAY (or REC) to stop. On an empty part this sets its length, to the nearest beat. Later takes stop at the
   part's end. The LCD says `RECORDED TRACK 1`, or `NOTHING RECORDED` when nothing was played after the count-in.
5. PLAY plays it back. Another REC arms the next empty track, so takes don't record over each other unless you
   choose a recorded track yourself (like tape, that replaces it).
6. SAVE (lit while something isn't saved) keeps it in the flash. Without it, the part is gone after a power cycle
   (or a reload of the emulator page).

### The MMT-8's functions (ALGO on the SEQ page)

As on the MMT-8, where you hold a function's button: the first eight white keys are the track buttons, K1-K4 set
the function's values, and **REC does it** (RECORD). Nothing changes before REC. The functions that change the
part's events wait until the sequencer stops ("STOP FIRST"). The tracks: a function starts with all eight lit; a
track key chooses only its own, then the keys add and take away. COPY and MERGE take a track from (lit) and one to
(flashing; red on the screen). The strip under the LCD shows the tracks and the function.

| Function | K1 | K2 | REC |
|---|---|---|---|
| LENGTH | Beats, 1-682 (SEL: tens) | At the end / at the top | Sets the part's length: events past a new end go, a note held over it is cut; at the top, the beats are added or taken off at the start |
| ERASE | What: all, notes, bend, controllers, programs, aftertouch, or channel 1-16 | | Erases it from the tracks lit; all of everything: EMPTY PART. In a song: the song |
| COPY | The part (song) to | What, as ERASE | All tracks: the part goes after the other's end (an empty one gets a copy; itself: twice as long). One track (and one to): over that track, cut to the other part's length. In a song: after the other song's steps |
| MERGE | | | The track lit onto the flashing one, both channels baked in, the merged track UNCHANGED |
| QUANTIZE | 1/2 ... 1/64 | Note start / start & end / note end / keep duration | The notes of the tracks lit |
| QUANTIZE (SONG) | The track's offset, ±48 clocks (384ths), at once | | |
| TRANSPOSE | Semitones, ±99 (SEL: octaves) | | The tracks lit; out of range folds back by octaves |
| MIDI CHANNEL | The track's channel, UNCHANGED or 1-16, at once | | |
| NAME | The cursor | The character | Stores the 14-character name (a part once used) |
| EDIT | The event's place (SEL: by beats) | Key, controller, program, bend (SEL: the channel) | Adds a C3 note where the event shown is; SEL + REC erases the event |
| CLOCK | In: INTERNAL or MIDI | Out: ON or OFF | (at once; SAVE keeps it) |
| MIDI DUMP | | | Sends the whole memory as an MMT-8's SysEx dump, over USB |

**EDIT** shows one track's events (the track key chooses which), as the MMT-8: `1 001.05 E3  100` is track 1, beat
1, clock 5, E3 at velocity 100, and `DUR 001.00 CH01` its duration (a beat) and channel (in brackets when the track
plays on a channel of its own). SELECT goes from event to event; K3 is the velocity (a controller's value, the bend
fine), K4 the duration (SEL: by beats; not a note: the channel). Past the last is END OF TRACK: K1 is where REC adds,
K2 what (a note, a program change, a controller, bend, aftertouch). In a **song**, EDIT shows its steps: SELECT the
step, K1 its part, the track keys its tracks, REC puts a step in before it (at the end: the part K1 chose), SEL + REC
takes it out. While playing, EDIT follows: the last event played on the track (in a song, the step playing); stopped,
it stays on it, ready to edit.

**QUANTIZE in a song** sets each track's clock offset, as the MMT-8's: the track key chooses the track (track 1
first), K1 moves it up to 48 clocks (half a beat) late (+) or early (-), to make up for a slow attack or MIDI delay.
The offsets are every song's, kept with the sequencer's memory, and take effect from the next PLAY. An early track
reads into the next step's part; at the song's start its first clocks play at once (the MMT-8's first beat can't be
advanced), and a late track's last clocks are cut when the song ends.

**MIDI clock in** (CLOCK, K1 to MIDI): the sequencer follows MIDI clock from USB or the TRS jack. F8 moves it on
(each plays 4 of its 96 clocks a beat, spaced by the last beat's clocks); FA starts from the top at the next clock,
FC stops, F2 (song position) sets the place while stopped and FB goes on from there. PLAY still plays, but waits
for clocks. The strip shows the tempo the clocks come at (`MIDI 120`). MIDI clock out (K2) goes on as it was, so
the FM-1 passes the clock on. Clocks are timed in rendered frames, to a block (2.9 ms); the notes land on them.

**The MMT-8's memory dump**, as `documentation/MMT-8 SYSEX.odt` has it:
- **In:** send a dump to the FM-1 over USB at any time (from a SysEx librarian; an MMT-8 sends one with TAPE held,
  PAGE DOWN, RECORD, through a computer's MIDI interface). MIDI DUMP's strip says so: `IN: ANY TIME`. The LCD
  counts the bytes; the dump replaces the sequencer's memory (parts, songs, names, channels; the song offsets and
  the drum patterns stay ours) and SAVE keeps it. Its SysEx events aren't kept. The sequencer's 64 KB holds a little
  less than an MMT-8's: a note sharing a clock takes 7 bytes here, 5 there, so a full memory of chords may not fit,
  and the LCD says from which part on it didn't.
- **Out:** MIDI DUMP (the last function), REC: the whole memory goes out over USB, as an MMT-8 sends it. The FM-1
  takes it back the same, byte for byte.
- **To a real MMT-8:** two things the document leaves out come from
  [polluxsynth's mmt8utils](https://github.com/polluxsynth/mmt8utils), a librarian that writes dumps MMT-8s take:
  the BCD beats are the beats + 1, and a dump made from nothing confuses an MMT-8, because the header's other
  bytes have to come from one of its own dumps. So the FM-1 keeps the header of the last dump it took in and sends
  it back in its own: **send the FM-1 one of your MMT-8's dumps first** (MIDI DUMP's strip says `HEADER: AN MMT-8'S`
  then). mmt8utils' own librarian reads the FM-1's dumps right (each part's name, beats and length, each song's name,
  tempo and length); a real MMT-8 taking one is untested.

**A slot into a program:** PRESET to the program, EDIT to the KEYS page, SELECT to the slot. LO and HI show `--`
while the slot isn't in the program (the strip says OUT). One detent of K1 (LO) or K2 (HI) puts it in with the whole
keyboard, C0-D#6; then K1 raises the low key and K2 lowers the high one. Turning LO past HI (or HI under LO) takes the
slot out again, and one detent back puts it in on the same keys. SAVE keeps it.

**Clearing a slot:** EDIT to the CLEAR page (the last of the round), SELECT to the slot, then REC.
- The page shows what the slot holds: `BUILT-IN SAMPLE`, `SAMPLE 48K` (the flash it takes), or `TEST TONE`, where
  there's nothing to clear. The strip under the LCD gives the sample's name.
- REC takes the sample out, whether it was built in or received over MIDI. The slot is then as an X7000's empty one:
  the test tone, its parameters as the sampler starts them, in no program. A received sample's flash is free for the
  next.
- SAVE keeps it, and so does a sample received after it (that saves the slots, see below). Until then, a power cycle
  brings the sample back. A built-in sample stays cleared until a firmware with other built-in samples is installed,
  which fills the empty slots with its own.
- The drum kit's sounds aren't in the slots, so CLEAR doesn't touch them. A pad set to play a slot plays the test tone
  once that slot is cleared.

Samples received over MIDI are written to the flash at once. The slots (the sample each one plays, and every slot's
parameters, panel edits too) are saved 3 s after the transfer goes quiet. If the power goes before that, the slots
come back as they were last saved. A slot cleared or replaced since then comes back empty (the test tone) when the new
sample took its flash, never playing the new sample's data: a dump saves the slots first before writing over a sample
they still name.

## Layout

| | |
|---|---|
| `app/` | The firmware: `app.c` (glue, routing, render, pages), `fm1.toml`, `app.h` (contract, flash map) |
| `app/s700/` | The sampler: `s7engine.c` (voices), `s7mem.c` (flash pool, directory), `s7bank.c` (the samples built in), `sds.c` (sample dump, SPRM, OVS) |
| `app/mmt8/` | The sequencer: `seq.c` (memory, takes, edits), `player.c` (clock, MIDI clock in, playback, recording, songs), `mmtdump.c` (the MMT-8's SysEx dump) |
| `app/hr16/` | The drum machine: `hrkit.c` (the kit, the pads, a pattern's own sounds, their voices), `hrpat.c` (patterns, their player, recording), `hrdump.c` (the HR-16's SysEx dump) |
| `app/ui/` | The LCD (`lcd.c`, a 5 × 7 font) and the pages; `seq_edit.c` the MMT-8's functions, `pages_drum.c` the HR-16's page |
| `app/tests/` | Scripts for the host simulator (`sysex.txt` is generated by `test/gen_sysex_test.py`) |
| `test/` | C unit tests against the X7000 measurements, Axs7's fixtures and the MMT-8's behaviour; `run.sh` runs all |
| `tools/mkrom.py` | Samples built in, from WAV files and `.qd` images: put into a firmware package, or `app/rom/emu_bank.h` for the host simulator and emulator (not in the repository) |
| `web/sm87bank.js` | Samples put into a firmware package in the browser: `mkrom.py`'s packaging in JavaScript, with no imports, for a page to copy (`sm87bank.d.ts` its types) |
| `resources/kit/` | The drum kit: its WAV files and `kit.txt`, the list of its sounds and its pads' set-up |
| `tools/mkkit.py` | The drum kit made a header as the firmware is built (`fm1.toml` `gen`), or a report of it |
| `documentation/` | The manuals and the S700/X7000 reference |

### Flash

| Range | Use |
|---|---|
| Below 0x93000 | The firmware's slot (581,564 bytes): the firmware with the drum kit, then the samples built in at its top |
| 0x93000-0xDC000 | Sample pool (73 sectors of 4 KB), packed 12-bit |
| 0xDC000, 0xDE000 | Store objects: settings; sample directory and parameters (A/B pairs) |
| 0xE0000-0xE5000 | The update loader's staging (not the app's) |
| 0xE5000-0xFC000 | Sequencer memory, the drum patterns too (3,840 bytes a sector: the update loader's bytes at 0xF00 kept erased) |
| 0xFC000 | Store object: the panel calibration (fm1-base's) |

## Building and testing

You need fm1-base's Python tools: Pillow, numpy and mido. There's a venv in `.venv`; Debian's Python has no ensurepip,
so pip was put into it with get-pip.py.

    git submodule update --init --recursive
    test/run.sh                                   # unit tests, then the host simulator's scripts
    cd fm1-base && ../.venv/bin/python tools/build.py ../app --host
    ../app/build/host/sm87_host ../app/tests/seq.txt ../app/build/host    # screenshots land there too

### Samples built in

`tools/mkrom.py` puts samples into a firmware package (`.fwsc`), as its slots' factory contents. That gives you
something to play without a sampler or computer sending any, and more sample memory than the pool alone. It needs no
compiler or JieLi SDK: it opens the package as it is, a release's or your own build's, writes the samples above the
firmware in its slot, and makes the package's checksums again (`documentation/sample-bank.md`).
- It takes WAV files. Axs7's carry their settings and keygroups in an `axs7` chunk, which are taken as they are;
  other WAVs give their `smpl` loop and root key, and are resampled to the nearest of the sampler's rates.
- It takes S700/X7000 `.qd` disk images, with all their parameters and programs.
- It changes the package in place, or writes another with `-o`. Run again, it replaces the samples; `--none` takes
  them out and gives the package back as it was built, byte for byte. With the package alone, it says what's in it
  and how much room there is.
- A dump sent to a slot replaces its built-in sample, and CLEAR (EDIT) empties the slot.

    W=../Axs7SdsW/sounds/wav
    .venv/bin/python tools/mkrom.py sm87.fwsc $W/85_grand_piano_e1.wav $W/86_grand_piano_c2.wav \
        $W/87_grand_piano_a2.wav $W/88_grand_piano_f3.wav $W/89_grand_piano_d4.wav $W/90_double_bass.wav \
        "some disk.qd@16"
    python3 fm1-base/tools/fm1_install.py sm87.fwsc

That example is a piano split over 5 samples, the double bass on keys 36-52 in program 1, and a disk in slot 16.
Don't take the disk images in Axs7's `web/test/fixtures` for one: their audio is a test ramp through every 12-bit
value, a buzz that follows the pitch. `mkrom.py` refuses more than the slot has room for.

A drum kit can go in too, replacing the one the firmware was built with: `--kit` takes a list in
`resources/kit/kit.txt`'s form (*The drum kit*), its WAV files beside it. Up to 64 sounds and 16 pads. Its sounds
are the drum machine's own and take no sampler slot. Without `--kit` the firmware's own kit plays: each run puts in
the whole, samples and kit as it is told.

    .venv/bin/python tools/mkrom.py sm87.fwsc piano.wav@1 --kit ../my-kit/kit.txt

The samples and kit are in that package only: a new firmware comes without them, so put them into its package too.
The firmware checks them at boot (their CRC), and plays none from a damaged package. When the kit changes, the pads
take its set-up (the settings keep which kit they were saved with); a pattern's own sounds keep the kit's sounds by
number.

Axs7 does it in the browser: INIT SM-87 BANKS (its BANK page) makes a SAMPLES bank and a DRUMS bank of the HR-16's
16 drums, which PROGRAM then edits as drums (one keygroup each, tuned in semitones, a pan); BUILD IN puts both into a
package you open, with a meter of the room they take. It uses `web/sm87bank.js`, which does the same as `mkrom.py`
for a page that makes the samples itself: `makeBank` makes the bytes of the samples and a kit, `Sm87Package` opens a
package, says what it holds and its room, takes them (`put`) and gives the package back (`bytes`). For a kit of the
HR-16's 16 drums, `HR16_DRUMS` says which pad each goes on (where an HR-16 dump puts it) with its name and note, and
`hr16Pads` makes the pads. It gives `mkrom.py`'s bytes (`test/bank_js_test.mjs`); Axs7 keeps a copy, and its tests
check the copy is this one.

The host simulator and the browser emulator have no firmware slot. For them, `mkrom.py --emu FILE ... [--kit KIT]`
writes `app/rom/emu_bank.h` (not in the repository: the sounds aren't ours to publish), built in while it exists;
`--emu --none` removes it. `test/run.sh` sets it aside while it runs and puts it back.

#### Getting the most samples in

The FM-1 keeps samples in two places, and a full set uses both:

| | Room | Filled by | Kept |
|---|---|---|---|
| Built in, in the firmware's slot | 372 KB, beside the firmware (159 KB) and the drum kit (37 KB) | `mkrom.py`, then installing the package | Each to the byte, plus 188 bytes for its settings. Until another firmware is installed |
| The sample pool | 292 KB (FREE on the sampler's strip) | Sample dumps over USB at any time: Axs7, or an X7000 through it | In whole 4 KB sectors. Saved by themselves 3 s after the transfer |

That's about 664 KB in all, 13 full-length samples of 48 KB, in the sampler's 16 slots, which the two share. A dump
to a slot with a built-in sample takes pool space and leaves the built-in one unused until the next install.

- **Short sounds built in.** The pool rounds each sample up to whole sectors, so a 1 KB hit takes 4 KB there.
  Built in, it takes 1 KB.
- **The rate to suit the sound.** A sample takes 1.5 bytes a word at any rate, and has at most 32,752 words. So the
  lower the rate, the longer it plays: 0.83 s at the top rate (39.6 kHz), 1.5 s at 22.2 kHz, 2.1 s at 15.7 kHz,
  4.2 s at 7.8 kHz. A bass or a pad loses little at a low rate, as on the S700. `mkrom.py` uses the nearest of the
  sampler's 41 rates to the WAV's own, so resample a WAV first (in Axs7, or any editor) to make it smaller.
- **Trimmed and looped.** `mkrom.py` keeps a WAV from its first word to END, and a looped one to its loop end. Cut
  silence off the start first, and loop a sound that holds instead of keeping all of it.
- **One sample over many keys.** With short sounds the 16 slots run out before the memory does. One sample can play
  over a whole key range, and in several programs with a transpose each, as the S700's keygroups do. Axs7 sets them
  and saves them in its WAV files, which `mkrom.py` takes as they are.
- **The kit shares the slot.** The drum kit (`resources/kit`) takes 37 KB of the same room. Fewer, shorter or
  lower-rate kit sounds leave more for samples (that one takes a build), and a drum pad can play a sampler slot too
  (PAD SOUND).
- **The room is the package's.** `mkrom.py PACKAGE.fwsc` says how much the firmware in it leaves. A later firmware
  may leave a little less.

The steps:

    .venv/bin/python tools/mkrom.py sm87.fwsc                                     # the room
    .venv/bin/python tools/mkrom.py sm87.fwsc bass.wav@1 strings.wav@2 "a disk.qd@3"
    python3 fm1-base/tools/fm1_install.py sm87.fwsc

Then send the rest to the pool from Axs7 over USB, as to an X7000 (*In the browser* says how it finds SM-87).

### The drum kit

The drum machine's sounds are in `resources/kit`: the WAV files, and `kit.txt`, the list of them.
- `sound` lines name a WAV file in the folder.
- `pad` lines set up the pads in order: a sound, tuning, level, pan, note, group and voices.

Every build makes the kit into a header of its own (`fm1.toml`'s `gen` runs `tools/mkkit.py`), so a change to the
folder is in the next build. Run by hand, `mkkit.py` reports what the kit holds and takes:

    .venv/bin/python tools/mkkit.py

The kit's sounds (37 KB now) are part of the firmware's image, so they take from the room `tools/mkrom.py` has for
samples in the slot (568 KB in all). It is the minimal kit every build has. Another kit, of up to 64 sounds and all
16 pads, goes into a package without a build: `tools/mkrom.py --kit` takes a list of this form (*Samples built in*).

The kit is designed to grow:
- A pad keeps its sound by number, so add new sounds at the end of the list.
- A pad saved with a sampler slot keeps it when the kit grows.

### In the browser (fm1-base's emulator, with SysEx both ways)

The same app compiled to WebAssembly. Its MIDI part gives it Web MIDI in and out, SysEx included; its data part
keeps the flash, so the samples, in the browser. It needs Emscripten:

    git clone https://github.com/emscripten-core/emsdk ~/emsdk && ~/emsdk/emsdk install 6.0.10 && ~/emsdk/emsdk activate 6.0.10
    source ~/emsdk/emsdk_env.sh
    cd fm1-base && ../.venv/bin/python tools/build.py ../app --web     # app/build/emu/
    python3 -m http.server -d ../app/build/emu 8087                     # open http://localhost:8087 in Chrome or Edge

To talk to Axs7, make two virtual MIDI cables, e.g. "to SM87" and "from SM87" with loopMIDI on Windows. Then:
- **Emulator:** MIDI ports, input "to SM87", output "from SM87".
- **Axs7:** output "to SM87", input "from SM87".

Axs7 finds it as it finds an X7000, on channel 1, and sends and fetches samples. It answers a dump coming in with
ACKS, one for the header and one for each block, as an X7000 does, so Axs7 says `SENT TO SLOT n`. Its own dumps go
out open loop: it doesn't wait for ACKS and ignores them.

Axs7 sends a sample's parameters (START, loop, scan mode, sound, key ranges) only after the sampler has answered its
request for the slot's. It asks once, on the channel and identifier it remembers from the last answer, so after
using a real X7000 on another channel it may not find SM-87. Its MIDI log then says `No parameters of slot n on
channel c`, and the sample arrives without them. Either set the sampler's channel on SM-87 (GLO), or press REQUEST
PARAMETERS in Axs7's system view once: it asks on every channel and remembers the answer.

When MIDI goes wrong in the emulator, it says so in red under its bar:
- a port that won't open (another program has it, say);
- a message the browser won't send;
- a SysEx the FM-1 sent with no MIDI out chosen, or with the chosen one gone;
- a SysEx coming in that was lost (too long, or cut by a status byte).

If Axs7 still hears nothing back and the page shows nothing, the replies did leave the browser, so look at the cable:
the port names on both sides, and loopMIDI's activity for it.

`node test/emu_test.mjs` drives the built `app.wasm` as a browser runs it (fm1-base's harness):
- ROVS, a whole 32,752-word dump in and its 547 ACKS, RSPRM, RSD back word for word, then the slot played.
- An MMT-8 memory dump in, and back out.
- A drum pattern recorded from MIDI and played out.
- An HR-16 dump asked for, taken back in and sent again the same; its MIDI test answered.

`test/run.sh` runs it when there is a build.

Three small changes to the fm1-base submodule make whole dumps work in the emulator and show what goes wrong.
They're in `patches/fm1-base-emu-sysex.patch` (the submodule stays at upstream's commit); apply them after cloning,
before building the emulator:

    git -C fm1-base apply ../patches/fm1-base-emu-sysex.patch

All are worth sending upstream:
- `web/emu/emu_web.c`: the frame size comes from `APP_WEB_SX_IN` in `app.h` (8 KB before), so a 66 KB dump arrives
  in one frame.
- `web/emu/parts/midi_worklet.js`: a SysEx frame streamed over several frames of the main loop (plat_sysex_begin ..
  end, as the device allows) reaches the page whole. Before, only frames that were complete within one render
  quantum did.
- `web/emu/parts/midi.js` and `midi_worklet.js`: MIDI that fails shows on the page's alert line (the list above).
  Before, a failed send, a port that wouldn't open and a frame lost on the way all went unnoticed.

### On the device

The device build needs JieLi's toolchain and three files from JieLi's AC79 SDK. JieLi's GitHub repository is gone;
its gitee repository has the revision fm1-base pins, and `get_sdk.sh` checks the files' SHA-256:

    sh fm1-base/tools/get_toolchain.sh            # ~/.jieli/toolchain
    SDK_URL=https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK sh fm1-base/bsp/tools/get_sdk.sh   # ~/.jieli/ac79-sdk
    cd fm1-base && JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk JIELI_DOCKER=0 \
        ../.venv/bin/python tools/build.py ../app --no-ble
    python3 tools/fm1_install.py ../app/build/sm87.fwsc     # over USB MIDI (or fm1-base's web installer)

The build's checks pass with the drum kit and no samples built in:
- No 64-bit multiply beside a load (the chip's erratum), and no soft doubles.
- Image: 200,916 of the slot's 581,564 bytes, the kit's 37,952 among them.
- RAM: 53,448 of 98,304 bytes.
- Pool: 276,116 of 344,064 bytes.

For recovery if an install goes wrong:
- Felucca's boot-loop guard: hold OCT- + OCT+ for 5 s.
- m-bootloader.
- MvaveFM1Unbricker.

The first things to measure on a device, before more is built on these assumptions:
- `plat_cpu_pct()` with 6 voices playing from the flash, and the 8 drum voices (the status strip shows CPU).
- `plat_xruns()`.
- A full 66 KB dump from Axs7 over USB, checked bit for bit with RSD.

## Next

- A dump sent to a real HR-16, and one of its dumps taken in: its sound numbers, the BCD beats.
- A dump sent to a real MMT-8, and MIDI clock in with `midi_stamp` (fm1.toml) for timing finer than a block.
- Installing a package from Axs7 over Web MIDI (it saves one now, for fm1-base's installer).
- On a device: samples built in at the very top of the slot played (nothing has read that high there yet).
