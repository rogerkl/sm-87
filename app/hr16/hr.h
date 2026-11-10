/* SM-87: the HR-16. An Alesis HR-16 style drum machine (documentation/ALESIS-HR16-HR16B-MMT8-User-Manual.pdf), cut
 * down to what the FM-1 holds:
 *   - 16 pads, each playing one sound at its own tuning, level and pan, on its own MIDI note; the hats choke each
 *     other (a group), the crash rings over itself (two voices);
 *   - the sounds: a kit built into the firmware (tools/mkkit.py from tools/kit.txt: app/rom/hr_kit.h), or any of the
 *     sampler's 16 slots. They are the sampler's format (12-bit words, packed, at one of its rates) and play on its
 *     voices (s700/s7.h s7_drum_start), without its filter;
 *   - 100 patterns of up to 682 beats, in the sequencer's memory (mmt8/seq.h): recorded as they loop, each hit
 *     quantized (1/4 .. 1/64, or off) and swung (50 .. 75%) as it goes in, the HR-16's way;
 *   - no songs of its own: a step of an MMT-8 song chooses the pattern that plays with it.
 *
 * The kit grows by a longer list: a pad keeps its sound by number, so new sounds go at the list's end. */
#pragma once
#include <stdint.h>
#include "../s700/s7.h"
#include "../mmt8/seq.h"

#define HR_PADS 16u
#define HR_DEFAULT_BEATS 8u       /* an empty pattern's length, the HR-16's */
#define HR_NO_SOUND 0xFFu
#define HR_TUNE_MAX 24            /* semitones either way */
#define HR_PAN_MAX 3              /* the HR-16's seven places: <3 .. <> .. 3> */
#define HR_NAME 6u                /* a sound's or a pad's name, at most */
#define HR_SWING_MAX 75u

typedef struct {
    uint8_t sound;                /* the kit's 0..n-1, then the sampler's slots (n + 0..15); HR_NO_SOUND: none */
    int8_t tune;                  /* semitones */
    uint8_t level;                /* 0..99 */
    int8_t pan;                   /* -3 (left) .. 3 (right) */
    uint8_t note;                 /* its MIDI note, in and out on the drum channel */
    uint8_t group;                /* 0, or a group: a hit cuts the others of its group (the hats) */
    uint8_t poly;                 /* its voices at once: 1 (a hit cuts the last), 2 (the HR-16's crash) */
    uint8_t rsv;
} hr_pad_t;

/* ---- the kit and the pads (hrkit.c) ---- */
typedef struct {                  /* a sound built in (app/rom/hr_kit.h) */
    const uint8_t *data;          /* packed 12-bit words (s7.h) */
    uint16_t words;
    uint8_t rate;                 /* its rate code (RECBW) */
    uint8_t vel;                  /* velocity's say in its level, 0..99 (VELLDN) */
    const char *name;
} hr_kit_sound_t;
typedef struct {                  /* a pad as the kit sets it up */
    const char *name;
    hr_pad_t pad;
} hr_kit_pad_t;

extern hr_pad_t hr_pad[HR_PADS]; /* the pads (the app's settings keep them) */
extern volatile uint32_t hr_lit;  /* the pads hit since the main loop last took them, a bit each */

uint32_t hr_kit_sounds(void);     /* the kit's sounds (0: a build without one) */
uint32_t hr_sounds(void);         /* the sounds a pad can have: the kit's, then the 16 slots */
void hr_sound_name(uint32_t sound, char *b);   /* "KICK", "SLOT01", "-----" (HR_NAME + 1 bytes) */
const char *hr_pad_name(uint32_t pad);         /* the kit's name for it, else "PAD 07" */
void hr_pad_default(uint32_t pad);             /* as the kit sets it up */
/* the pads saved when the kit had kit_n sounds, as the kit is now: the slots renumbered after it, a sound gone none */
void hr_pads_rekit(uint32_t kit_n);
/* for tests: another kit than the one built in */
void hr_kit_use(const hr_kit_sound_t *s, uint32_t n, const hr_kit_pad_t *pads, uint32_t np);

/* the audio interrupt */
void hr_hit(uint32_t pad, uint32_t vel);    /* a pad sounds */
void hr_note(uint32_t note, uint32_t vel);  /* a note on the drum channel: every pad on it sounds */
int32_t hr_pad_of(uint32_t note);           /* the first pad on a note, -1 none */
void hr_kill(void);
void hr_render(float *l, float *r, uint32_t n);   /* adds the drums, left and right */
uint32_t hr_voices_active(void);

/* ---- the patterns (hrpat.c): in the sequencer's memory, changed from the main loop ---- */
#define HR_GRIDS 9u
extern const uint8_t HR_GRID[HR_GRIDS];       /* quantize: 1/4 .. 1/64 in clocks, then off (1) */
extern const char *const HR_GRID_TXT[HR_GRIDS];
uint32_t hr_beats(uint32_t p);    /* its length: its own, else HR_DEFAULT_BEATS */
uint32_t hr_hits(uint32_t p);
static inline uint32_t hr_hit_tick(const uint8_t *h) { return h[0] | (uint32_t)h[1] << 8; }
const uint8_t *hr_hit_at(uint32_t p, uint32_t i);   /* hit i: tick (2), pad, velocity; 0 past the last */
/* a hit at tick (one of the pad's there already: its velocity replaced); -1 no room. scratch: SQ_SCRATCH bytes */
int hr_add(uint32_t p, uint32_t tick, uint32_t pad, uint32_t vel, uint8_t *scratch);
int hr_remove(uint32_t p, uint32_t tick, uint32_t pad, uint8_t *scratch);   /* that hit out (none: 0 too) */
int hr_erase(uint32_t p, uint32_t pad, uint8_t *scratch);   /* the pad's hits; HR_PADS: all, the length too */
int hr_set_length(uint32_t p, uint32_t beats, uint8_t *scratch);   /* 1..682: hits past the end go */
/* COPY: a's hits after b's, b as long as both (b without hits: a copy of a; a = b: twice as long). SQ_E_* */
int hr_copy(uint32_t a, uint32_t b, uint8_t *scratch);
/* where a hit played at tick goes: the nearest place on the grid (clocks), every second one swung (50..75%) */
uint32_t hr_quantize(uint32_t tick, uint32_t grid, uint32_t swing);

/* ---- the player (audio interrupt, sqp_on_tick) ----
 * With the sequencer playing a song: each step's pattern, from the step's start (none: quiet). Else its own loop:
 * the pattern chosen, over and over, the next one queued for its end; with the sequencer's part (unless muted), or
 * alone (sqp.aux: PLAY on the drum page) */
typedef struct {
    uint8_t pattern;              /* its own (PRESET on its page) */
    uint8_t next;                 /* queued for the pattern's end, 0xFF none */
    uint8_t mute;                 /* not with the sequencer's parts */
    uint8_t rec;                  /* record: hits played go into its own pattern while it loops */
    uint8_t quant, swing;         /* the recording's: HR_GRID's index, % */
    uint8_t playing;              /* the pattern sounding (a song step's, or its own), 0xFF none */
    uint16_t tick;                /* its own loop's next clock */
    uint16_t at;                  /* the clock playing (shown) */
    uint16_t erase;               /* the pads held to erase while recording (the HR-16's ERASE + a pad), a bit each */
} hrp_t;
extern volatile hrp_t hrp;
void hrp_tick(void);              /* a tick of the clock (sqp_on_tick) */
void hrp_resync(void);            /* the patterns changed (sq_unlock) */
int hrp_recording(void);          /* hits played go in now */
void hrp_record(uint32_t pad, uint32_t vel);   /* a pad played (on the drum channel): into the pattern, if recording */
/* what the recording did, for the main loop to put in the pattern (hrp_take, then hr_add or hr_remove) */
typedef struct {
    uint16_t tick;
    uint8_t pattern, pad, vel, erase;
} hr_rec_t;
int hrp_take(hr_rec_t *r);
void hr_emit(uint32_t pad, uint32_t vel);   /* the app's: a pattern's hit (the pad, and MIDI out) */
