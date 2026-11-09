# SM-87

A firmware for the M-VAVE FM-1 that recreates a 1986/87 studio: an **Alesis MMT-8** style part/song sequencer
driving an **Akai S700 / X7000** style sampler, with an **Alesis HR-16** style drum machine planned. It is built on
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
  - MIDI clock and song position out.
  - The MMT-8's functions: LENGTH (at the end or the top), ERASE and COPY with its 22 filters, MERGE, QUANTIZE (10
    grids, 4 modes), TRANSPOSE, MIDI CHANNEL, NAME, and EDIT, the event list editor; in a song ERASE, COPY, NAME and
    EDIT, its steps put in and taken out.
- **The screen** is a 2 × 16 character LCD, drawn dot by dot, with a status strip under it.

## Controls

| | |
|---|---|
| Keys | Play the sampler on its channel (and out over USB); OCT- / OCT+ shift them |
| SEQ | The sequencer's page; again (stopped): PART ↔ SONG |
| PLAY | Play from the top / stop; SEL + PLAY continues from the beat |
| REC | Arm recording on the part's lowest empty track (or the one chosen with REC + its key); again: disarm; while a take runs: punch out |
| SEL + first 8 white keys | Track mutes (on the SEQ page; in a song: the step's mutes) |
| REC + first 8 white keys | The record track |
| PRESET | The part or song (SEQ page), else the sampler's program |
| K1 / K2 (SEQ) | Tempo / the record track's channel (UNCHANGED, 1-16) |
| ALGO (SEQ) | The MMT-8's functions, below; SEQ goes back to the part or song |
| SELECT / K3 (SONG, stopped) | The step / its part (at the END: appends the last part; past 99: the song ends there) |
| EDIT | The sampler's pages round: POINTS (START, END), LOOP (LOOP, MODE), TUNE (ROOT, FINE, RATE), KEYS (LO, HI, TRANSPOSE, CONSTANT) |
| ENV | OUT (FILTER, RELEASE, LEVEL), then the velocity amounts |
| LFO | SPEED, DEPTH, DELAY, PRESSURE |
| SELECT / K1-K4 (sampler pages) | The slot / the page's values. The strip under the LCD shows the slot, its sample and the program |
| HOME | The sampler's overview |
| SAVE | Lit when something isn't saved: settings, the sample parameters, the sequencer (stopped) |

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
| TRANSPOSE | Semitones, ±99 (SEL: octaves) | | The tracks lit; out of range folds back by octaves |
| MIDI CHANNEL | The track's channel, UNCHANGED or 1-16, at once | | |
| NAME | The cursor | The character | Stores the 14-character name (a part once used) |
| EDIT | The event's place (SEL: by beats) | Key, controller, program, bend (SEL: the channel) | Adds a C3 note where the event shown is; SEL + REC erases the event |

**EDIT** shows one track's events (the track key chooses which), as the MMT-8: `1 001.05 E3  100` is track 1, beat
1, clock 5, E3 at velocity 100, and `DUR 001.00 CH01` its duration (a beat) and channel (in brackets when the track
plays on a channel of its own). SELECT goes from event to event; K3 is the velocity (a controller's value, the bend
fine), K4 the duration (SEL: by beats; not a note: the channel). Past the last is END OF TRACK: K1 is where REC adds,
K2 what (a note, a program change, a controller, bend, aftertouch). In a **song**, EDIT shows its steps: SELECT the
step, K1 its part, the track keys its tracks, REC puts a step in before it (at the end: the part K1 chose), SEL + REC
takes it out.

**A slot into a program:** PRESET to the program, EDIT to the KEYS page, SELECT to the slot. LO and HI show `--`
while the slot isn't in the program (the strip says OUT). One detent of K1 (LO) or K2 (HI) puts it in with the whole
keyboard, C0-D#6; then K1 raises the low key and K2 lowers the high one. Turning LO past HI (or HI under LO) takes the
slot out again, and one detent back puts it in on the same keys. SAVE keeps it.

Samples received over MIDI are written to the flash at once, and their parameters are saved 3 s after the transfer
goes quiet.

## Layout

| | |
|---|---|
| `app/` | The firmware: `app.c` (glue, routing, render, pages), `fm1.toml`, `app.h` (contract, flash map) |
| `app/s700/` | The sampler: `s7engine.c` (voices), `s7mem.c` (flash pool, directory), `sds.c` (sample dump, SPRM, OVS) |
| `app/mmt8/` | The sequencer: `seq.c` (memory, takes, edits), `player.c` (clock, playback, recording, songs) |
| `app/ui/` | The LCD (`lcd.c`, a 5 × 7 font) and the pages; `seq_edit.c` the MMT-8's functions |
| `app/tests/` | Scripts for the host simulator (`sysex.txt` is generated by `test/gen_sysex_test.py`) |
| `test/` | C unit tests against the X7000 measurements, Axs7's fixtures and the MMT-8's behaviour; `run.sh` runs all |
| `tools/mkrom.py` | Samples built into the firmware, from WAV files and `.qd` images (`app/rom/`, not in the repository) |
| `documentation/` | The manuals and the S700/X7000 reference |

### Flash

| Range | Use |
|---|---|
| 0x93000-0xDC000 | Sample pool (73 sectors of 4 KB), packed 12-bit |
| 0xDC000, 0xDE000 | Store objects: settings; sample directory and parameters (A/B pairs) |
| 0xE5000-0xFC000 | Sequencer memory (3,840 bytes a sector: the update loader's bytes at 0xF00 kept erased) |
| 0xFC000 | Store object: the panel calibration (fm1-base's) |

## Building and testing

You need fm1-base's Python tools: Pillow, numpy and mido. There's a venv in `.venv`; Debian's Python has no ensurepip,
so pip was put into it with get-pip.py.

    git submodule update --init --recursive
    test/run.sh                                   # unit tests, then the host simulator's scripts
    cd fm1-base && ../.venv/bin/python tools/build.py ../app --host
    ../app/build/host/sm87_host ../app/tests/seq.txt ../app/build/host    # screenshots land there too

### Samples built in (a test build)

`tools/mkrom.py` puts samples into the firmware, as its slots' factory contents. That gives you something to play
without a sampler or computer sending any:
- It takes WAV files. Axs7's carry their settings and keygroups in an `axs7` chunk, which are taken as they are;
  other WAVs give their `smpl` loop and root key, and are resampled to the nearest of the sampler's rates.
- It takes S700/X7000 `.qd` disk images, with all their parameters and programs.
- It writes `app/rom/rom_samples.h`. That file is not in the repository (the sounds aren't ours to publish) and is
  built in while it exists. A dump sent to a slot replaces its built-in sample; `--none` removes the file.

    W=../Axs7SdsW/sounds/wav
    .venv/bin/python tools/mkrom.py $W/85_grand_piano_e1.wav $W/86_grand_piano_c2.wav $W/87_grand_piano_a2.wav \
        $W/88_grand_piano_f3.wav $W/89_grand_piano_d4.wav $W/90_double_bass.wav "some disk.qd@16"

That example is a piano split over 5 samples, the double bass on keys 36-52 in program 1, and a disk in slot 16.
Don't take the disk images in Axs7's `web/test/fixtures` for one: their audio is a test ramp through every 12-bit
value, a buzz that follows the pitch. The firmware with them is 447 KB of the 581 KB slot. `test/run.sh` sets your
file aside while it runs and puts it back.

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

Axs7 finds it as it finds an X7000, on channel 1, and sends and fetches samples (open loop: the FM-1 doesn't wait for
ACKs and ignores them).

`node test/emu_test.mjs` drives the built `app.wasm` as a browser runs it (fm1-base's harness): ROVS, a whole
32,752-word dump in, RSPRM, RSD back word for word, then the slot played. `test/run.sh` runs it when there is a build.

Two small changes to the fm1-base submodule make whole dumps work in the emulator. They're in
`patches/fm1-base-emu-sysex.patch` (the submodule stays at upstream's commit); apply them after cloning, before
building the emulator:

    git -C fm1-base apply ../patches/fm1-base-emu-sysex.patch

Both are worth sending upstream:
- `web/emu/emu_web.c`: the frame size comes from `APP_WEB_SX_IN` in `app.h` (8 KB before), so a 66 KB dump arrives
  in one frame.
- `web/emu/parts/midi_worklet.js`: a SysEx frame streamed over several frames of the main loop (plat_sysex_begin ..
  end, as the device allows) reaches the page whole. Before, only frames that were complete within one render
  quantum did.

### On the device

The device build needs JieLi's toolchain and three files from JieLi's AC79 SDK. JieLi's GitHub repository is gone;
its gitee repository has the revision fm1-base pins, and `get_sdk.sh` checks the files' SHA-256:

    sh fm1-base/tools/get_toolchain.sh            # ~/.jieli/toolchain
    SDK_URL=https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK sh fm1-base/bsp/tools/get_sdk.sh   # ~/.jieli/ac79-sdk
    cd fm1-base && JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/.jieli/ac79-sdk JIELI_DOCKER=0 \
        ../.venv/bin/python tools/build.py ../app --no-ble
    python3 tools/fm1_install.py ../app/build/sm87.fwsc     # over USB MIDI (or fm1-base's web installer)

The build's checks pass: no 64-bit multiply beside a load (the chip's erratum), no soft doubles, the image 108 KB of
the 581 KB slot, RAM 46 of 96 KB, the pool 274 of 344 KB.

For recovery if an install goes wrong:
- Felucca's boot-loop guard: hold OCT- + OCT+ for 5 s.
- m-bootloader.
- MvaveFM1Unbricker.

The first things to measure on a device, before more is built on these assumptions:
- `plat_cpu_pct()` with 6 voices playing from the flash (the status strip shows CPU).
- `plat_xruns()`.
- A full 66 KB dump from Axs7 over USB, checked bit for bit with RSD.

## Next

- The MMT-8's song QUANTIZE (a clock offset for each track, ±48) and a part's events shown as it plays in EDIT.
- MIDI clock in, MMT-8 sysex memory dumps in and out (`documentation/MMT-8 SYSEX.odt`), and the HR-16 drum machine.
