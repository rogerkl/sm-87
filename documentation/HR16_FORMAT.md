# Alesis HR-16 / HR-16B Technical Reference

Technical documentation of the firmware ROMs, sound ROMs, DM3AG audio encoding, SysEx bulk dump layout, kit structure, default kits, and pattern/song binary formats for the **Alesis HR-16** (1987) and **Alesis HR-16:B** (1989).

All addresses, bitfields, and tables below were verified directly against:
- `HR-16 V1.09.bin` (32 KB OS ROM), `HR16SU15.BIN` & `HR16SU16.BIN` (2 × 512 KB sound ROMs)
- `hr16b_OS.bin` (32 KB V2.00 OS ROM), `hr16b_snd_U15.BIN` & `hr16b_snd_U16.BIN` (2 × 512 KB sound ROMs)
- `HR16-factory.syx` and `drumatix-hr16.syx` (HR-16 SysEx bulk memory dumps)

---

## 1. Hardware Overview

| Component | Specification |
|---|---|
| CPU | Intel 8031 / 80C31 microcontroller (12.000 MHz crystal `Y1`) |
| Sound ASIC | Alesis custom **DM3AG** LSI (clocked at 6.000 MHz = `Y1 / 2`) |
| DAC | Burr-Brown **PCM54** 16-bit DAC (multiplexed) |
| Native Sample Rate | **46,875 Hz** (`6,000,000 Hz / 128`, marketed as "47 kHz") |
| Polyphony | 16 voices: the click (channel 15), one for each pad, except that pads 2–4 share channel 2 (hi-hat choking) and the crash (pad 13) alternates between two, 11 and 12 |
| OS ROM | 1 × 32 KB EPROM (`27C256`) |
| Sound ROMs | 2 × 512 KB mask ROM / EPROM (`27C4001` / `27C040`), sockets `U16` and `U15` (1 MB total) |
| Sequencer Resolution | **96 PPQN** (ticks per quarter note / beat) |

---

## 2. Firmware ROMs (`HR-16 V1.09.bin` & `hr16b_OS.bin`)

### 2.1 PCB Address-Line Bit Reversal
The lower 8 address lines (`A0–A7`) are routed in reverse order on the HR-16/HR-16B PCB to simplify trace layout. When dumped in a standard EPROM programmer, each 256-byte page appears scrambled. To descramble to the CPU's logical address space:

```python
rev8 = [int(f"{i:08b}"[::-1], 2) for i in range(256)]
logical = bytes(raw[(i & ~0xFF) | rev8[i & 0xFF]] for i in range(len(raw)))
```

### 2.2 Key Tables in Descrambled Firmware

| Structure | HR-16 (`V1.09`) | HR-16:B (`V2.00`) | Description |
|---|---:|---:|---|
| UI Strings | `0x198C` | `0x1F88` | LCD display strings & credits |
| Default Empty Pattern & Kit | `0x1539` (kit at `0x153D`) | `0x6BC1` (kit at `0x6BC5`) | 60-byte template (2-bar empty pattern + 48-byte default kit) |
| DM3AG Voice Setup Code | `0x2785` | `0x2E20` | Converts 3-byte pad entry to 5-byte DM3AG register block (2.3) |
| HW Channel / Choke Map | `0x27D4` | `0x2E6F` | 17-byte map from logical channel (click + pads 0–15) to DM3AG channel: `15, 0, 1, 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14` (channel 12 is the crash's second voice, 5.1) |
| Volume Curve Table | `0x2FDF` | `0x36BC` | 100-byte map from user volume `0–99` to the DM3AG's level: `0` for volume 0, then `0x36` (volume 1) up by 1 a step to `0x67` (volume 50), then by ½ a step (51 is `0x67` again, then each value twice) to `0x7F` (volume 98–99). It rises with the volume: a level, not an attenuation |
| Pad Panel Labels | `0x3220` | `0x3924` | 8-char ASCII names for `CLICK` + pads `0–15` |
| Velocity Attenuation Table | `0x432B` | `0x4A7F` | 8-byte table `[29, 20, 15, 12, 9, 6, 3, 0]` subtracted from volume for velocities `0–7` |
| **Voice Pointer Table** | `0x4333` | `0x6BFD` | 49 big-endian `uint16` page pointers (`byte_offset = ptr << 4`) |
| **Voice Name Table** | `0x4395` | `0x6C5F` | 49 × 16-byte ASCII sound names |
| **Built-in Demo Song** | *(none)* | `0x7179–0x7A22` | 11 patterns (`0x7179` ptr table, `0x71A1` song step list, `0x71AE` pattern data) |
| Unused ROM (`0xFF`) | `0x6580–0x7FFF` | `0x7A23–0x7FFF` | Empty EPROM padding |

### 2.3 The DM3AG Register Block
The voice setup code (`0x2785` on the HR-16) fills a channel's 5 bytes in the register cache (RAM `0x8333 + 5 × n`,
`n` the logical channel: 0 the click, 1–16 pads 0–15) from its 3-byte kit entry (RAM `0x8300 + 3 × n`):

| Register | Contents |
|---|---|
| 0, 1 | The sound's page pointer, high byte then low (the Voice Pointer Table's entry for byte 0's voice) |
| 2 | The tuning, byte 2's bits 0–4, as they are: no table. The pitch of a step is the DM3AG's own (the user manual: 32 increments over an octave and a half) |
| 3 | The level: byte 1's volume through the Volume Curve Table |
| 4 | Byte 2's pan (bits 5–7), bit 4 set for MIX OUT (byte 1's bit 7 clear), and the channel from the HW Channel Map (bits 0–3) |

> **Factory Patterns in ROM:** Neither firmware contains a preset bank of factory patterns (pattern slots `00–99` start blank in battery-backed RAM). Only the HR-16:B firmware includes a built-in 11-pattern `"ALESIS DEMO SONG"` (step order `[0, 1, 2, 3, 10, 4, 5, 6, 7, 8, 9]` at tempo `0xAB` = 117 BPM).

---

## 3. Sound ROMs (`U16` & `U15`)

### 3.1 Memory Map
The DM3AG sees the two 512 KB chips as one contiguous 1 MB address space (`0x00000–0xFFFFF`):
- **`U16` (`HR16SU16.BIN` / `hr16b_snd_U16.BIN`):** `0x00000–0x7FFFF` (lower 512 KB)
- **`U15` (`HR16SU15.BIN` / `hr16b_snd_U15.BIN`):** `0x80000–0xFFFFF` (upper 512 KB)

Samples can cross the chip boundary seamlessly (e.g., HR-16 `#33 LOW CONGA SLAP` starts at `0x07FE90` in `U16` and continues into `U15`; HR-16B `#23 ROCK HI HAT` starts at `0x07F360` in `U16` and continues into `U15`).

> **Note on vintage `HR16SU15.BIN` / `HR16SU16.BIN` dumps:** The widely circulated HR-16 sound ROM dump has a `0x1100`-byte circular shift on each 512 KB chip (`logical_chip = raw[-0x1100:] + raw[:-0x1100]`). `extract_hr16.py` detects and corrects this automatically by checking pointer alignment against the firmware table. The HR-16B dumps (`hr16b_snd_U15.BIN` / `hr16b_snd_U16.BIN`) have no shift.

### 3.2 DM3AG "Christmas Tree" Sample Encoding
Although played through a 16-bit PCM54 DAC, samples are stored as **8-bit pseudo-floating-point PCM**:

1. Playback begins at `byte_offset = ptr << 4` (16-byte page aligned) with `shift = 0`.
2. Each byte `b` is read sequentially:
   - If `b != 0x80`: interpret `b` as signed 8-bit (`s8` in `-127..+127`) and output 16-bit PCM:
     ```python
     sample_16 = s8 << (8 - shift)   # equivalent to (s8 << 8) >> shift
     ```
   - If `b == 0x80`: increment `shift += 1` (attenuating subsequent output by 6 dB / 1 bit). **No audio sample is emitted for `0x80`.**
   - When `shift == 9`: **playback halts immediately.** Remaining `0x80` bytes pad to the next 16-byte (`0x10`) boundary.

As a sound decays below half full scale (`±64`), the encoder shifts the remaining audio up by 2× and inserts a `0x80` marker—preserving 8 bits of mantissa precision across 8 octaves (48 dB) of decay, which gives raw dumps a sideways "Christmas tree" waveform appearance.

### 3.3 Sample Sharing & Duplicates
- **HR-16:** All 49 voices (`00–48`) point to distinct waveforms.
- **HR-16:B:** 47 unique sounds (`00–46`):
  - `#32 CABASA` (`0x0CA3C0`) and `#33 FAST CABASA` (`0x0CA710`) share the same waveform, with `FAST CABASA` starting 848 bytes later for a tighter attack.
  - Slots `#46`, `#47`, and `#48` all point to `0x0FFB40` (`JAM BLOCK`).

### 3.4 HR-16 Sound List (Voices `00–48`)

| # | ROM Name | ROM Offset | Chip | Samples | Duration (@ 46,875 Hz) | Extracted WAV (`wav/hr16/`) |
|---:|---|---:|---|---:|---:|---|
| 00 | 24" POWER KICK | `0x01AE30` | U16 | 14,999 | 320.0 ms | `00_24in_POWER_KICK.wav` |
| 01 | 22" DEEP KICK | `0x014C70` | U16 | 24,999 | 533.3 ms | `01_22in_DEEP_KICK.wav` |
| 02 | 22" POWER KICK | `0x007D20` | U16 | 4,998 | 106.6 ms | `02_22in_POWER_KICK.wav` |
| 03 | 20" SWIFT KICK | `0x005600` | U16 | 9,999 | 213.3 ms | `03_20in_SWIFT_KICK.wav` |
| 04 | 22"DBL HD KICK A | `0x00B010` | U16 | 19,999 | 426.6 ms | `04_22inDBL_HD_KICK_A.wav` |
| 05 | 22"DBL HD KICK B | `0x00FE40` | U16 | 19,999 | 426.6 ms | `05_22inDBL_HD_KICK_B.wav` |
| 06 | 22" 60'S KICK | `0x0090C0` | U16 | 7,990 | 170.5 ms | `06_22in_60S_KICK.wav` |
| 07 | 22" GATED KICK | `0x000000` | U16 | 21,999 | 469.3 ms | `07_22in_GATED_KICK.wav` |
| 08 | ELECTRONIC KICK1 | `0x01E8E0` | U16 | 7,999 | 170.6 ms | `08_ELECTRONIC_KICK1.wav` |
| 09 | ELECTRONIC KICK2 | `0x020830` | U16 | 14,999 | 320.0 ms | `09_ELECTRONIC_KICK2.wav` |
| 10 | 8x14" WOOD SNARE | `0x0242E0` | U16 | 25,999 | 554.6 ms | `10_8x14in_WOOD_SNARE.wav` |
| 11 | AMBIENT WOOD SNR | `0x039AE0` | U16 | 29,999 | 640.0 ms | `11_AMBIENT_WOOD_SNR.wav` |
| 12 | 13" BRASS PICOLO | `0x02A880` | U16 | 29,999 | 640.0 ms | `12_13in_BRASS_PICOLO.wav` |
| 13 | GATED SNARE | `0x0344E0` | U16 | 21,999 | 469.3 ms | `13_GATED_SNARE.wav` |
| 14 | ELECTRONIC SNARE | `0x044AD0` | U16 | 12,999 | 277.3 ms | `14_ELECTRONIC_SNARE.wav` |
| 15 | RIMSHOT SNARE | `0x041020` | U16 | 14,999 | 320.0 ms | `15_RIMSHOT_SNARE.wav` |
| 16 | SIDE STICK SNARE | `0x031DC0` | U16 | 9,999 | 213.3 ms | `16_SIDE_STICK_SNARE.wav` |
| 17 | BRUSH HIT SNARE | `0x047DB0` | U16 | 19,471 | 415.4 ms | `17_BRUSH_HIT_SNARE.wav` |
| 18 | 10" POWER TOM | `0x053F10` | U16 | 39,999 | 853.3 ms | `18_10in_POWER_TOM.wav` |
| 19 | 16"PWR FLOOR TOM | `0x05DB60` | U16 | 39,999 | 853.3 ms | `19_16inPWR_FLOOR_TOM.wav` |
| 20 | 10" DBL HEAD TOM | `0x072F60` | U16 | 29,999 | 640.0 ms | `20_10in_DBL_HEAD_TOM.wav` |
| 21 | 14" DBL HEAD TOM | `0x0677B0` | U16 | 46,999 | 1002.6 ms | `21_14in_DBL_HEAD_TOM.wav` |
| 22 | ELECTRONIC TOM | `0x07A4A0` | U16 | 22,999 | 490.6 ms | `22_ELECTRONIC_TOM.wav` |
| 23 | CLOSED HI HAT A | `0x093730` | U15 | 7,999 | 170.6 ms | `23_CLOSED_HI_HAT_A.wav` |
| 24 | CLOSED HI HAT B | `0x095680` | U15 | 8,999 | 192.0 ms | `24_CLOSED_HI_HAT_B.wav` |
| 25 | HALF OPEN HI HAT | `0x0979C0` | U15 | 19,999 | 426.6 ms | `25_HALF_OPEN_HI_HAT.wav` |
| 26 | OPEN HI HAT | `0x09C7F0` | U15 | 34,999 | 746.6 ms | `26_OPEN_HI_HAT.wav` |
| 27 | FOOT CLOSED HAT | `0x0A50C0` | U15 | 9,999 | 213.3 ms | `27_FOOT_CLOSED_HAT.wav` |
| 28 | RIDE CYMBAL | `0x0A77E0` | U15 | 39,999 | 853.3 ms | `28_RIDE_CYMBAL.wav` |
| 29 | RIDE CYMBAL BELL | `0x0B1430` | U15 | 39,999 | 853.3 ms | `29_RIDE_CYMBAL_BELL.wav` |
| 30 | CRASH CYMBAL | `0x0BB080` | U15 | 89,999 | 1920.0 ms | `30_CRASH_CYMBAL.wav` |
| 31 | TIMBALE | `0x04C9D0` | U16 | 29,999 | 640.0 ms | `31_TIMBALE.wav` |
| 32 | HIGH CONGA SLAP | `0x089AE0` | U15 | 39,999 | 853.3 ms | `32_HIGH_CONGA_SLAP.wav` |
| 33 | LOW CONGA SLAP | `0x07FE90` | U16 → U15 | 39,999 | 853.3 ms | `33_LOW_CONGA_SLAP.wav` |
| 34 | SMALL WOOD BLOCK | `0x0D3740` | U15 | 9,499 | 202.6 ms | `34_SMALL_WOOD_BLOCK.wav` |
| 35 | LARGE WOOD BLOCK | `0x0D5C70` | U15 | 21,999 | 469.3 ms | `35_LARGE_WOOD_BLOCK.wav` |
| 36 | ROSEWOOD CLAVES | `0x0ED000` | U15 | 3,999 | 85.3 ms | `36_ROSEWOOD_CLAVES.wav` |
| 37 | CABASA | `0x0DB270` | U15 | 11,999 | 256.0 ms | `37_CABASA.wav` |
| 38 | MARACAS A | `0x0EDFB0` | U15 | 3,999 | 85.3 ms | `38_MARACAS_A.wav` |
| 39 | MARACAS B | `0x0FEAC0` | U15 | 1,170 | 25.0 ms | `39_MARACAS_B.wav` |
| 40 | SHAKER | `0x0D1020` | U15 | 9,999 | 213.3 ms | `40_SHAKER.wav` |
| 41 | AGOGO BELL | `0x0DE160` | U15 | 11,999 | 256.0 ms | `41_AGOGO_BELL.wav` |
| 42 | MEDIUM COW BELL | `0x0E3F50` | U15 | 14,999 | 320.0 ms | `42_MEDIUM_COW_BELL.wav` |
| 43 | LARGE COW BELL | `0x0E7A00` | U15 | 21,999 | 469.3 ms | `43_LARGE_COW_BELL.wav` |
| 44 | TRIANGLE | `0x0F2F20` | U15 | 29,999 | 640.0 ms | `44_TRIANGLE.wav` |
| 45 | TAMBOURINE | `0x0EEF60` | U15 | 16,299 | 347.7 ms | `45_TAMBOURINE.wav` |
| 46 | HAND CLAPS | `0x0FA460` | U15 | 17,999 | 384.0 ms | `46_HAND_CLAPS.wav` |
| 47 | FINGER SNAP | `0x0E1050` | U15 | 1,999 | 42.6 ms | `47_FINGER_SNAP.wav` |
| 48 | DRUM STICKS | `0x0E1830` | U15 | 9,999 | 213.3 ms | `48_DRUM_STICKS.wav` |

### 3.5 HR-16:B Sound List (Voices `00–48`)

| # | ROM Name | ROM Offset | Chip | Samples | Duration (@ 46,875 Hz) | Extracted WAV (`wav/hr16b/`) |
|---:|---|---:|---|---:|---:|---|
| 00 | MONSTER KICK | `0x000000` | U16 | 17,999 | 384.0 ms | `00_MONSTER_KICK.wav` |
| 01 | TILED ROOM KICK | `0x004660` | U16 | 21,999 | 469.3 ms | `01_TILED_ROOM_KICK.wav` |
| 02 | BIG FOOT | `0x009C60` | U16 | 17,999 | 384.0 ms | `02_BIG_FOOT.wav` |
| 03 | RAP KICK | `0x00E2C0` | U16 | 43,191 | 921.4 ms | `03_RAP_KICK.wav` |
| 04 | TECHNO KICK | `0x018B90` | U16 | 15,989 | 341.1 ms | `04_TECHNO_KICK.wav` |
| 05 | DEEP WOOD KICK | `0x01CA10` | U16 | 18,999 | 405.3 ms | `05_DEEP_WOOD_KICK.wav` |
| 06 | TIGHT KICK | `0x021460` | U16 | 5,999 | 128.0 ms | `06_TIGHT_KICK.wav` |
| 07 | COMBNATION SNARE | `0x022BE0` | U16 | 20,999 | 448.0 ms | `07_COMBNATION_SNARE.wav` |
| 08 | TECHNO SNARE | `0x027E00` | U16 | 24,999 | 533.3 ms | `08_TECHNO_SNARE.wav` |
| 09 | HI PICOLO RIM | `0x02DFC0` | U16 | 29,999 | 640.0 ms | `09_HI_PICOLO_RIM.wav` |
| 10 | MID PICOLO SNARE | `0x039B60` | U16 | 23,999 | 512.0 ms | `10_MID_PICOLO_SNARE.wav` |
| 11 | RAP SNARE | `0x035500` | U16 | 17,999 | 384.0 ms | `11_RAP_SNARE.wav` |
| 12 | 90'S GATED SNARE | `0x03F930` | U16 | 17,999 | 384.0 ms | `12_90S_GATED_SNARE.wav` |
| 13 | REVERB SNARE | `0x043F90` | U16 | 49,999 | 1066.6 ms | `13_REVERB_SNARE.wav` |
| 14 | MID PUNCH SNARE | `0x0502F0` | U16 | 21,999 | 469.3 ms | `14_MID_PUNCH_SNARE.wav` |
| 15 | FAT SNARE | `0x0558F0` | U16 | 23,999 | 512.0 ms | `15_FAT_SNARE.wav` |
| 16 | AGGRESSIVE SNARE | `0x05B6C0` | U16 | 16,999 | 362.6 ms | `16_AGGRESSIVE_SNARE.wav` |
| 17 | SIDESTICK | `0x05F940` | U16 | 8,999 | 192.0 ms | `17_SIDESTICK.wav` |
| 18 | GATED SIDESTICK | `0x061C80` | U16 | 16,999 | 362.6 ms | `18_GATED_SIDESTICK.wav` |
| 19 | CLOSED HI HAT | `0x065F00` | U16 | 4,449 | 94.9 ms | `19_CLOSED_HI_HAT.wav` |
| 20 | MID HI HAT | `0x067070` | U16 | 14,999 | 320.0 ms | `20_MID_HI_HAT.wav` |
| 21 | OPEN HI HAT | `0x06AB20` | U16 | 71,999 | 1536.0 ms | `21_OPEN_HI_HAT.wav` |
| 22 | R&B CLOSED HAT | `0x07C470` | U16 | 11,999 | 256.0 ms | `22_RnB_CLOSED_HAT.wav` |
| 23 | ROCK HI HAT | `0x07F360` | U16 → U15 | 27,999 | 597.3 ms | `23_ROCK_HI_HAT.wav` |
| 24 | HI POWER TOM | `0x0860D0` | U15 | 47,999 | 1024.0 ms | `24_HI_POWER_TOM.wav` |
| 25 | MID POWER TOM | `0x091C60` | U15 | 53,999 | 1152.0 ms | `25_MID_POWER_TOM.wav` |
| 26 | AGGRESSIVE TOM | `0x09EF60` | U15 | 19,999 | 426.6 ms | `26_AGGRESSIVE_TOM.wav` |
| 27 | CRASH CYMBAL | `0x0A3D90` | U15 | 69,999 | 1493.3 ms | `27_CRASH_CYMBAL.wav` |
| 28 | RIDE CYMBAL | `0x0B4F10` | U15 | 34,999 | 746.6 ms | `28_RIDE_CYMBAL.wav` |
| 29 | OPEN CONGA | `0x0BD7E0` | U15 | 28,001 | 597.4 ms | `29_OPEN_CONGA.wav` |
| 30 | CONGA SLAP | `0x0C4550` | U15 | 12,999 | 277.3 ms | `30_CONGA_SLAP.wav` |
| 31 | AMBIENT BONGO | `0x0C7880` | U15 | 10,919 | 232.9 ms | `31_AMBIENT_BONGO.wav` |
| 32 | CABASA | `0x0CA3C0` | U15 | 8,371 | 178.6 ms | `32_CABASA.wav` |
| 33 | FAST CABASA | `0x0CA710` | U15 | 7,523 | 160.5 ms | `33_FAST_CABASA.wav` |
| 34 | TAMBOURINE | `0x0CC480` | U15 | 21,999 | 469.3 ms | `34_TAMBOURINE.wav` |
| 35 | GATED CLAPS | `0x0D1A80` | U15 | 10,999 | 234.6 ms | `35_GATED_CLAPS.wav` |
| 36 | HI AGOGO BELL | `0x0D4590` | U15 | 14,199 | 302.9 ms | `36_HI_AGOGO_BELL.wav` |
| 37 | BOTTLES | `0x0D7D20` | U15 | 19,999 | 426.6 ms | `37_BOTTLES.wav` |
| 38 | PIPE | `0x0DCB50` | U15 | 11,749 | 250.6 ms | `38_PIPE.wav` |
| 39 | LIP POP | `0x0DF940` | U15 | 13,499 | 288.0 ms | `39_LIP_POP.wav` |
| 40 | AMBIENT PUH | `0x0E2E10` | U15 | 11,999 | 256.0 ms | `40_AMBIENT_PUH.wav` |
| 41 | SCRATCH | `0x0E5D00` | U15 | 7,999 | 170.6 ms | `41_SCRATCH.wav` |
| 42 | SYNTH BLOCK | `0x0E7C50` | U15 | 4,649 | 99.2 ms | `42_SYNTH_BLOCK.wav` |
| 43 | TORQUE WRENCH | `0x0E8E90` | U15 | 31,619 | 674.5 ms | `43_TORQUE_WRENCH.wav` |
| 44 | TRASH CAN | `0x0F0A20` | U15 | 39,999 | 853.3 ms | `44_TRASH_CAN.wav` |
| 45 | GLASS BREAK | `0x0FA670` | U15 | 21,699 | 462.9 ms | `45_GLASS_BREAK.wav` |
| 46 | JAM BLOCK | `0x0FFB40` | U15 | 999 | 21.3 ms | `46_JAM_BLOCK.wav` |
| 47 | JAM BLOCK | `0x0FFB40` | U15 | 999 | 21.3 ms | *(duplicate of #46)* |
| 48 | JAM BLOCK | `0x0FFB40` | U15 | 999 | 21.3 ms | *(duplicate of #46)* |

---

## 4. SysEx Bulk Dump & RAM Memory Map

### 4.1 SysEx Envelope & 7-Bit Packing
A full memory dump (`SEND ALL PATTS & SONGS OUT MIDI`) is a single SysEx message:

```text
F0 00 00 0E 01 00  <packed payload bytes>  F7
│  └────┬────┘ │ └─ Command: 0x00 = All Data Dump
│       │      └─── Device ID: 0x01 = HR-16 / HR-16:B
│       └────────── Alesis Manufacturer ID
└────────────────── Start of SysEx
```

Because MIDI data bytes must be 7-bit (`0x00–0x7F`), Alesis packs every **7 raw 8-bit RAM bytes into 8 MIDI bytes** as a 56-bit MSB-first bitstream:

```python
# Unpack 8 MIDI bytes -> 7 raw bytes
bits = 0
for b in chunk_8:
    bits = (bits << 7) | (b & 0x7F)
raw_7 = [(bits >> (k * 8)) & 0xFF for k in range(6, -1, -1)]
```

### 4.2 Unpacked RAM Map (`file_offset = ram_addr - 0x8200`)

| File Offset | HR-16 RAM | Size | Contents |
|---:|---:|---:|---|
| `0x0000–0x00C7` | `0x8200–0x82C7` | 200 B | System scratch / runtime state (often zeroed in offline-generated `.syx` files) |
| `0x00C8–0x00FF` | `0x82C8–0x82FF` | 56 B | MIDI & utility settings (`0x00CB` = Click MIDI note, `0x00CC–0x00DB` = MIDI notes for Pads `0–15`) |
| `0x0100–0x0132` | `0x8300–0x8332` | 51 B | Live active kit in RAM (`0x0100–0x0102` = Click, `0x0103–0x0132` = Pads `0–15`, 3 B each) |
| `0x0133–0x0187` | `0x8333–0x8387` | 85 B | Active DM3AG register cache (17 channels × 5 B) |
| `0x0188–0x01FF` | `0x8388–0x83FF` | 120 B | System variables |
| `0x0200–0x02C7` | `0x8400–0x84C7` | 200 B | **Pattern pointer table:** 100 big-endian `uint16` RAM addresses (`Pattern 00–99`; `0x0000` = empty) |
| `0x02C8–0x0301` | `0x84C8–0x8501` | 58 B | Sequencer / tempo / edit state |
| `0x0302–0x03C9` | `0x8502–0x85C9` | 200 B | **Song pointer table:** 100 big-endian `uint16` RAM addresses (`Song 00–99`; `0x0000` = empty) |
| `0x03E0–0x03FF` | `0x85E0–0x85FF` | 32 B | Live 2×16 LCD text buffer (e.g. `"SENDING OUT DATATO MIDI........."` in live dumps) |
| `0x0400+` | `0x8600+` | var | **Pattern pool** followed immediately by **Song pool** |

---

## 5. Kit Format, Pad Mapping & Default Kits

### 5.1 Pad Numbering & Hardware Choke Group
Pads are indexed `0–15` (bottom row `0–7`, top row `8–15`). Every pattern carries its own 48-byte kit table defining what each pad plays.

Two pads have special fixed hardware behavior:
- **Pads `2`, `3`, and `4`** (`CLS HAT`, `MID HAT`, `OPN HAT`): firmware table `0x27D4` assigns all three to **DM3AG hardware channel `2`**, so triggering any one of them immediately cuts off the others (hi-hat choking).
- **Pad `13`** (`CRASH`) has two voices that alternate, channels `11` and `12` (the map gives `11`; no entry gives `12`). The user manual: "repeatedly hitting the crash pad results in two independent crashes (of the same sound) that can overlap as they decay."

Every other pad has a channel of its own, so hitting it again cuts its last sound. The click is on channel `15`.

### 5.2 Per-Pad 3-Byte Encoding (`kit[3*pad .. 3*pad + 2]`)

```text
Byte 0:  [ v6 v5 v4 v3 v2 v1 v0 ]   Voice index 0..48 (sound ROM entry)
Byte 1:  [ O  m6 m5 m4 m3 m2 m1 m0 ]
           │  └────────┬────────┘
           │           └─────────── Volume 0..99 (0x00..0x63; 0 = muted)
           └─────────────────────── Output pair: 0 = MIX OUT (1/2), 1 = AUX OUT (3/4)
Byte 2:  [ p2 p1 p0  t4 t3 t2 t1 t0 ]
           └───┬───┘  └──────┬──────┘
               │             └───── Tuning 0..31 (pitch = value - 16, range -16..+15; 0x10 = 0;
               │                    the step is the DM3AG's: 32 over an octave and a half, the user manual says)
               └─────────────────── Pan 0..6: 0=3L, 1=2L, 2=1L, 3=Center, 4=1R, 5=2R, 6=3R
```

*Example:* `0x00 0x63 0x70` = Voice `0`, MIX OUT, Volume `99`, Pan `Center` (`3 << 5 = 0x60`), Tuning `0` (`16 = 0x10`, `0x60 | 0x10 = 0x70`).

### 5.3 Default Kits (Factory Reset / New Pattern Template)

| Pad | Label | HW Ch | HR-16 Default (`0x153D`) | Vol / Pan / Tune | HR-16:B Default (`0x6BC5`) | Vol / Pan / Tune |
|---:|---|---:|---|---|---|---:|
| 0 | `KICK` | 0 | `00` 24" POWER KICK | 99 / C / 0 | `00` MONSTER KICK | 99 / C / 0 |
| 1 | `SNARE` | 1 | `11` AMBIENT WOOD SNR | 90 / C / 0 | `08` TECHNO SNARE | 95 / C / 0 |
| 2 | `CLS HAT` | **2** | `23` CLOSED HI HAT A | 99 / 2L / 0 | `19` CLOSED HI HAT | 90 / 2L / 0 |
| 3 | `MID HAT` | **2** | `25` HALF OPEN HI HAT | 99 / 2L / 0 | `20` MID HI HAT | 90 / 2L / 0 |
| 4 | `OPN HAT` | **2** | `26` OPEN HI HAT | 99 / 2L / 0 | `21` OPEN HI HAT | 90 / 2L / 0 |
| 5 | `CLAPS` | 3 | `46` HAND CLAPS | 90 / C / 0 | `35` GATED CLAPS | 99 / C / 0 |
| 6 | `PERC 3` | 4 | `45` TAMBOURINE | 75 / 1R / 0 | `39` LIP POP | 99 / 1R / 0 |
| 7 | `PERC 4` | 5 | `44` TRIANGLE | 90 / 1L / 0 | `45` GLASS BREAK | 99 / 1L / 0 |
| 8 | `TOM 1` | 6 | `20` 10" DBL HEAD TOM | 85 / 3R / 0 | `24` HI POWER TOM | 85 / 3R / 0 |
| 9 | `TOM 2` | 7 | `21` 14" DBL HEAD TOM | 85 / C / 0 | `25` MID POWER TOM | 85 / C / 0 |
| 10 | `TOM 3` | 8 | `19` 16" PWR FLOOR TOM | 85 / 3L / 0 | `25` MID POWER TOM | 85 / 3L / -6 |
| 11 | `TOM 4` | 9 | `22` ELECTRONIC TOM | 85 / C / 0 | `26` AGGRESSIVE TOM | 90 / 1L / -3 |
| 12 | `RIDE` | 10 | `28` RIDE CYMBAL | 90 / 2R / 0 | `28` RIDE CYMBAL | 90 / 2R / 0 |
| 13 | `CRASH` | 11, 12 | `30` CRASH CYMBAL | 85 / 2L / 0 | `27` CRASH CYMBAL | 85 / 2L / 0 |
| 14 | `PERC 1` | 13 | `31` TIMBALE | 75 / 2R / 0 | `41` SCRATCH | 77 / 2R / 0 |
| 15 | `PERC 2` | 14 | `33` LOW CONGA SLAP | 75 / 2L / 0 | `43` TORQUE WRENCH | 82 / 2L / 0 |

*(Click channel defaults to voice `48` `DRUM STICKS` at Vol `75`, Pan `C`, Tune `0` on HR-16.)*

### 5.4 Per-Pattern Kits vs. `MANUAL VOICE/TUNE/MIX`
- **Normal mode (`MANUAL VOICE/TUNE/MIX: OFF`):** Selecting or playing a pattern loads that pattern's own 48-byte kit into active RAM (`0x8303–0x8332`). This is why SysEx banks like `HR16-factory.syx` (58 patterns, 48 distinct kits) and `drumatix-hr16.syx` (100 patterns, 76 distinct kits) freely reassign any voice to any pad—for example, placing `DRUM STICKS` on pads 0–2 for a count-in, doubling kicks on `PERC 3`/`PERC 4`, doubling snares on `PERC 2`, or placing a non-choking hi-hat on `PERC 1`.
- **Manual mode (`MANUAL VOICE/TUNE/MIX: ON`):** The machine ignores each pattern's 48-byte kit and plays all patterns through the single active kit in RAM (`0x8303–0x8332`).

---

## 6. Pattern & Song Binary Formats

### 6.1 Pattern Record Format
Pointed to by `RAM 0x8400 + 2 * pattern_num` (big-endian `uint16`):

```text
Offset  Size      Field
0x00    2 bytes   Total record size in bytes (little-endian uint16)
0x02    2 bytes   Length field: the beats + 1 in BCD (little-endian uint16)
                  (e.g., 0x0009 = 8 beats = 768 ticks at 96 PPQN; 0x0017 = 16 beats; 0x0033 = 32 beats,
                  as the patterns in HR16-factory.syx have them; read as binary, 0x0033 would be 50 beats)
0x04    48 bytes  Kit table: 16 pads × 3 bytes (see Section 5.2)
0x34    N bytes   Event stream, ending with 0xFF
```

#### Event Stream Bytes
- **`0x00–0x7F` (bit 7 = 0) — Pad Trigger:**
  - `pad = byte & 0x0F` (`0–15`)
  - `velocity = (byte >> 4) & 0x07` (`0–7`, where `7` is full volume; firmware subtracts `[29, 20, 15, 12, 9, 6, 3, 0][velocity]` from the pad's stored volume `0–99`)
- **`0x80–0xFE` (bit 7 = 1) — Delta Time:**
  - `delta_ticks = byte & 0x7F` (`0–126` ticks at **96 PPQN**; consecutive delta bytes sum together, e.g., an empty 8-beat pattern is `FE FE FE FE FE FE 8C FF` = `6 × 126 + 12 = 768` ticks)
- **`0xFF` — End of Pattern**

*Example (`drumatix-hr16.syx` Pattern 01, 4 beats = 384 ticks):*
```text
56 00 05 00 <48-byte kit>
80              ; +0 ticks (beat 1.0)
72 7E 70 57     ; Pad 2 (v7), Pad 14 (v7), Pad 0 (v7), Pad 7 (v5)
B0              ; +48 ticks (1/8th note)
52 5E           ; Pad 2 (v5), Pad 14 (v5)
B0              ; +48 ticks (beat 2.0)
72 7E 71 7F     ; Pad 2 (v7), Pad 14 (v7), Pad 1 (v7), Pad 15 (v7)
...
FF              ; End of pattern
```

### 6.2 Song Record Format
Pointed to by `RAM 0x8502 + 2 * song_num` (big-endian `uint16`):

```text
Offset  Size      Field
0x00    2 bytes   Total record size in bytes (little-endian uint16)
0x02    N bytes   Step stream, ending with 0xFF
```

#### Song Step Bytes
- **`0x00–0x63` (`0–99`):** Play Pattern `00–99`.
- **`0x64–0xFE` (`100–254`):** Set Song Tempo:
  ```python
  bpm = byte - 54   # (byte - 0x64) + 46 -> e.g. 0x7C=70, 0x9A=100, 0xA4=110, 0xAE=120 BPM
  ```
- **`0xFF`:** End of Song.

---

