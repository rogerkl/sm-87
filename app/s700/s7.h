/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the S700/X7000 sampler engine.
 *
 * Plays samples the way an Akai S700 / X7000 does, from the measurements in documentation/akai-s700-x7000/
 * 04-playback.md, ported from Axs7's engine (github.com/rogerkl/Axs7, engine/src/voice.rs, filter.rs, envelope.rs).
 *
 * A slot holds a sample as packed 12-bit words (two words in three bytes, offset binary) and its parameters as the
 * S700's sample parameter message (SPRM) carries them: 155 values, the 32 programs' key ranges first. The engine
 * reads both in place; the sample memory (s7mem.c) owns them.
 *
 * Everything here runs in the audio interrupt: app_render takes events and renders. */
#pragma once
#include <stdint.h>

#define S7_SLOTS 16u              /* the X7000 with its memory board */
#define S7_PROGRAMS 32u
#define S7_VOICES 16u             /* the most it may play at once (s7_poly), as an S1000 */
#define S7_POLY_STD 6u            /* as the S700: what it plays unless told otherwise */
#define S7_NPARAM 155u            /* SPRM's values */
#define S7_MAXWORDS 32752u        /* a sample dump's limit */
#define S7_CHANNELS 16u
#define S7_PACKED(words) (((words) + 1u) / 2u * 3u + 1u)   /* bytes a sample takes packed (+1: a 32-bit read's pad) */

/* the SPRM values (k: the value's number, documentation/akai-s700-x7000/02-sample-parameters.md) */
#define S7_P_KEYL(p) (4u * (p))           /* program p (0..31): lowest key */
#define S7_P_KEYH(p) (4u * (p) + 1u)      /* highest key; KEYL > KEYH: not in the program */
#define S7_P_TRANSP(p) (4u * (p) + 2u)    /* low 7 bits: transpose, signed; top bit: constant pitch */
#define S7_P_ATPTCH(p) (4u * (p) + 3u)    /* audio trigger (unused: the FM-1 has no audio input) */
#define S7_P_UNDEF 128u
#define S7_P_RPMODE 129u                  /* 1 one shot, 2 looping, 4 alternating, 9 drum trigger */
#define S7_P_SSTRT 130u                   /* word (two values, low byte first): START */
#define S7_P_LEN1 132u                    /* word: END - START - 1, as the sampler keeps it */
#define S7_P_SEND 134u                    /* word: END + 1 */
#define S7_P_SLOOP 136u                   /* word: the loop's start; it runs to END */
#define S7_P_LEN2 138u                    /* word: END - LOOP - 1 */
#define S7_P_LFSP 140u
#define S7_P_LFDP 141u
#define S7_P_LFDL 142u
#define S7_P_FLTFRQ 143u
#define S7_P_RLEASE 144u
#define S7_P_LDNSS 145u
#define S7_P_SFNTUN 146u                  /* signed, 1/16 semitone */
#define S7_P_RECNP 147u                   /* root key */
#define S7_P_RECBW 148u                   /* rate code */
#define S7_P_RVRSE 149u                   /* display only: a reversed sample is stored as it plays */
#define S7_P_VELFIL 150u
#define S7_P_VELLDN 151u
#define S7_P_VELRLS 152u

enum { S7_ONESHOT = 1, S7_LOOP = 2, S7_ALT = 4, S7_DRUM = 9 };

typedef struct {
    const uint8_t *data;          /* packed words, 0 = empty slot */
    uint32_t words;               /* words in data */
    uint8_t p[S7_NPARAM];         /* the SPRM values */
    uint8_t pressure;             /* the X7000's pressure sens, 0..99: per sample, not in SPRM */
} s7_slot_t;

/* the slots, the sample memory's (s7mem.c): the engine reads them in the audio interrupt */
extern s7_slot_t s7_slot[S7_SLOTS];

static inline uint32_t s7_word(const uint8_t *p, uint32_t k) { return p[k] | (uint32_t)p[k + 1u] << 8; }
static inline void s7_set_word(uint8_t *p, uint32_t k, uint32_t v)
{
    p[k] = (uint8_t)v;
    p[k + 1u] = (uint8_t)(v >> 8);
}
/* a slot that plays: a sample, and in at least one program */
int s7_slot_in_program(const s7_slot_t *s, uint32_t prog);

/* word i of a packed sample, signed (-2048..2047) */
static inline int32_t s7_w12(const uint8_t *d, uint32_t i)
{
    const uint8_t *b = d + 3u * (i >> 1);
    return (int32_t)((i & 1u) ? ((uint32_t)b[1] >> 4 | (uint32_t)b[2] << 4) : (b[0] | ((uint32_t)b[1] & 15u) << 8))
           - 2048;
}
/* packs word i (0..4095, offset binary) into d, d's bytes for its pair already zero for an even i */
static inline void s7_put_w12(uint8_t *d, uint32_t i, uint32_t w)
{
    uint8_t *b = d + 3u * (i >> 1);
    if (i & 1u) {
        b[1] = (uint8_t)((b[1] & 0x0Fu) | (w & 0x0Fu) << 4);
        b[2] = (uint8_t)(w >> 4);
    } else {
        b[0] = (uint8_t)w;
        b[1] = (uint8_t)((b[1] & 0xF0u) | (w >> 8 & 0x0Fu));
    }
}

/* the rate of a rate code (RECBW 36..77), Hz: the sampler's 41 rates, a semitone apart */
float s7_rate(uint32_t code);
/* the defaults of an empty slot's values (the X7000's test tone's, in no program) */
void s7_default_params(uint8_t *p);

/* ---- the engine (audio interrupt) ----
 * The S700's MIDI modes (its manual, 33-37; OVS's OMNION): OMNI OFF hears its basic channel, OMNI ON every channel,
 * both playing the program chosen; MULTI PROGRAM (its mode 7) hears every channel, channel 1..16 playing program
 * P1..P16, or P17..P32 while one of those is the program chosen (the manual says "or P17-32", not how: here, the
 * program chosen's bank). The voices (six, as the S700's: s7_poly) go to whichever channel needs one. The app sends the engine only what its
 * mode hears; the channel (0..15) is the message's */
enum { S7_OMNI_OFF, S7_OMNI_ON, S7_MULTI };
void s7_init(float fs);           /* the tables, at the output rate */
void s7_midi_mode(uint32_t mode);
uint32_t s7_program_of(uint32_t ch);   /* the program a channel plays (0..31) */
void s7_note_on(uint32_t ch, uint32_t key, uint32_t vel);
void s7_note_off(uint32_t ch, uint32_t key, uint32_t off_vel);   /* off_vel: the note off's (64: a note on at 0) */
void s7_program(uint32_t prog);   /* 0..31: P1..P32, the program chosen (MULTI: its bank) */
uint32_t s7_current_program(void);
/* the controllers: each channel's own in MULTI; in OMNI ON or OFF one set for all */
void s7_bend(uint32_t ch, int32_t v);   /* -8192..8191 */
void s7_bend_range(uint32_t semis);
void s7_modwheel(uint32_t ch, uint32_t v);   /* 0..127 */
void s7_pressure(uint32_t ch, uint32_t v);   /* channel aftertouch 0..127 */
void s7_all_off(void);            /* every voice released */
void s7_channel_off(uint32_t ch); /* a channel's voices released (all notes off) */
void s7_kill(void);               /* every voice silent now */
void s7_kill_slot(uint32_t slot); /* the voices playing a slot silent now (before its sample changes) */
/* the mix (the MIX page): a slot's voices' gains, left and right (1, 1: as the S700 is, mono, at its level) */
void s7_mix(uint32_t slot, float gl, float gr);
void s7_render(float *l, float *r, uint32_t n);   /* adds n samples, each voice at its slot's mix */
uint32_t s7_voices_active(void);
/* the voices a note may take, S7_POLY_STD..S7_VOICES (S7_POLY_STD: as the S700). Fewer than playing: those over it
 * released (they fade as a note let go), a new note takes one of the rest */
void s7_poly(uint32_t n);
uint32_t s7_poly_now(void);

/* ---- the drum machine's voices (hr16/): the sampler's, from any sound in its format (a slot, or one of the drum
 * kit's built in), played once from START to END without the filter, bend or vibrato, and panned. Audio interrupt.
 * The voice to use is the caller's choice (hr16/hrkit.c: one per pad, the hats sharing) ---- */
#define S7_DRUM_VOICES 8u
/* voice v starts sound s (slot: its slot, for s7_kill_slot; 0xFF none of them) semis from its own pitch, its gains
 * left and right (level and pan) */
void s7_drum_start(uint32_t v, const s7_slot_t *s, uint32_t slot, float semis, float gl, float gr);
void s7_drum_choke(uint32_t v);   /* gone in about 20 ms (a closed hat over an open one) */
int s7_drum_active(uint32_t v);
int s7_drum_released(uint32_t v);   /* choked, fading */
uint32_t s7_drum_age(uint32_t v); /* notes started since it was (the oldest: the largest) */
void s7_drum_kill(void);
void s7_drum_render(float *l, float *r, uint32_t n);   /* adds n samples, left and right */

/* for tests: a voice's index mapping (voice.rs index()) and the level and release the engine computes */
typedef struct {
    uint32_t mode, len, loop_start, loop_end;
} s7_loop_t;
int32_t s7_index(const s7_loop_t *l, int32_t v);    /* the word read at position v, -1 after the end */
float s7_level_db(uint32_t level, uint32_t vel_amount, uint32_t vel);
float s7_release_t60(uint32_t r);
float s7_cutoff_hz(uint32_t f);
