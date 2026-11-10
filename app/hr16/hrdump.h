/* SM-87: the HR-16's memory as its SysEx dump has it (documentation/hr16-sysex.md, from the HR-16/HR-16B Service
 * Manual 1.00, 11.0 MIDI Implementation, command 00H): F0 00 00 0E 01 00, the memory from its address 8200H packed
 * seven bytes in eight as the MMT-8's (mmt8/mmtdump.h md_packed_byte, the same table), F7.
 *
 * The memory: 400H bytes of header (the settings: the drums' MIDI notes, the swing, the tempo, the quantize; the
 * patterns' and songs' addresses, big-endian, +8200H; where the free memory starts and how much there is,
 * little-endian; bytes not to alter), then the patterns 00-99 there are and the songs 00-99, one after another. A
 * pattern: its length in bytes and its beats (BCD), each drum's sound, level (and output), pan and pitch, then its
 * events: a drum played (its dynamics 0-7 and the drum) or clocks to wait (0-126, 96 a beat), FFH at its end. A song:
 * its length, then its steps (a pattern, or a tempo change), FFH.
 *
 * What SM-87 takes of it:
 *   - the patterns: their hits (each drum on a pad by its role, HD_PAD; its dynamics as a velocity, 8 levels), their
 *     lengths, and each one's VOICE, TUNE and MIX as its own sounds (hr.h): a sound as the kit's of its kind, else the
 *     pad's in the global set-up (the kit has no toms, claps or percussion); the tuning's 32 steps over an octave and
 *     a half to the nearest semitone; level and pan; the output not (the FM-1 has one);
 *   - its swing and quantize (the recording's);
 *   - not its songs: SM-87's drums follow the MMT-8's songs. Counted, not taken;
 *   - the rest of the header (its MIDI settings, its tempo, bytes not to alter): kept as it came (sq.hr16) and sent
 *     back in ours, as with the MMT-8 (a dump made from nothing may confuse an HR-16). With none yet, ours are made
 *     up: the drums' notes the pads', its channel the drums', the tempo the sequencer's.
 * Ours sends each pad's sound as one of the HR-16's 49 of its kind: the sound most of the last dump's patterns had on
 * that drum (sq.hr16_voice) when it is that kind, else one of the kind; a pad without a kit sound (a slot, none),
 * that drum's sound as it was.
 *
 * Pure code (the main loop, the player stopped); the receiver is the MMT-8's (md_rx_t, with HD_PRE). */
#pragma once
#include <stdint.h>
#include "hr.h"

#define HD_HDR SQ_HR16_HDR
#define HD_BASE 0x8200u           /* the HR-16's address of the dump's first byte */
#define HD_END 0xFF00u            /* its memory's end */
#define HD_MAX (HD_END - HD_BASE) /* 32,000 bytes: the most a dump holds */
#define HD_PREFIX 6u              /* F0 00 00 0E 01 00 */
#define HD_ID 0x01u               /* the HR-16's, after Alesis' 00 00 0E */
#define HD_PAT_HDR 0x34u
#define HD_SOUNDS 49u             /* its sounds: 00 24" power kick .. 48 drum sticks */
#define HD_DRUMS SQ_HR16_DRUMS

/* commands after F0 00 00 0E 01: a whole memory, a request for one, the MIDI test (00 asks, 01 answers) */
enum { HD_CMD_DUMP = 0x00, HD_CMD_REQUEST = 0x07, HD_CMD_TEST = 0x09 };

extern const uint8_t HD_PRE[HD_PREFIX];
/* its drums in its order (KICK SNARE CLS-HAT MID-HAT OPN-HAT CLAPS PERC-3 PERC-4 TOM-1 .. TOM-4 RIDE CRASH PERC-1
 * PERC-2), on our pads (from 0): the kit's six by their names, then the others */
extern const uint8_t HD_PAD[HD_DRUMS];

typedef struct {
    uint16_t patterns;            /* taken in */
    uint16_t lost;                /* not: no room in the sequencer's memory, or not readable */
    int16_t first_lost;           /* the first of them, or -1 */
    uint16_t songs;               /* its songs with steps: not taken */
    int8_t quant;                 /* its quantize as HR_GRID's index (1/6: 1/8), -1 not one */
    int8_t swing;                 /* %, 50..74; -1 not one */
} hd_report_t;

/* an unpacked dump (len bytes from the header) into the drum patterns, replacing all of them (the MMT-8's parts and
 * songs kept). scratch: SQ_SCRATCH bytes. 0, or -1: not a dump */
int hd_import(const uint8_t *img, uint32_t len, uint8_t *scratch, hd_report_t *r);

typedef struct {
    uint8_t quant, swing;         /* the recording's: HR_GRID's index, % */
    uint8_t channel;              /* the drums', 0..15 (a header made up) */
    uint16_t tempo;               /* BPM (a header made up) */
} hd_settings_t;
/* the drum patterns as an HR-16 dump's memory (unpacked) into img: its length, or -1 when it doesn't fit max (an
 * HR-16's memory: HD_MAX) */
int32_t hd_export(uint8_t *img, uint32_t max, const hd_settings_t *s);
int hd_has_header(void);          /* an HR-16's header kept: a dump of ours an HR-16 takes */

/* for tests: an HR-16 sound's kind, and ours of it on drum d (the kit's names) */
uint32_t hd_sound_in(uint32_t hr_sound, uint32_t pad);
uint32_t hd_sound_out(uint32_t sound, uint32_t drum);
int32_t hd_tune_in(uint32_t bits);   /* the pitch bits (0 = -16 steps) as semitones */
uint32_t hd_tune_out(int32_t semis);
