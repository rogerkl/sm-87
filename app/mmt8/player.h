/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the MMT-8 playing and recording: its clock (96 a beat, from the audio's frames, so a tick lands on the
 * frame), the tracks' events out, the notes' offs on time, the count-in, the take, the songs' steps, MIDI clock out.
 * Runs in the audio interrupt (app_render); the main loop calls the transport inside plat_lock. */
#pragma once
#include <stdint.h>
#include "seq.h"

#define SQP_TAKE_MAX 2048u        /* events in one take (a note is two) */

enum { SQP_STOPPED, SQP_COUNTIN, SQP_PLAYING };
enum { SQP_TAKE_NONE, SQP_TAKE_RUNNING, SQP_TAKE_DONE };

typedef struct {
    uint8_t state;                /* SQP_* */
    uint8_t song_mode;
    uint8_t part, next_part;      /* next_part: queued for the part's end (0xFF none) */
    uint8_t song, step;
    uint8_t mask;                 /* the tracks that play: the mutes (in a song, the step's) */
    uint8_t rec_armed, rec_track; /* record ready: the track a take goes to */
    uint8_t take_state;
    uint8_t click_rec, click_play;
    uint8_t clock_out;            /* MIDI clock (F8, FA, FB, FC) out */
    uint8_t clock_in;             /* the clock: 0 its own (the tempo), 1 MIDI's (F8 in; FA, FB, FC, F2 the transport) */
    uint16_t ext_tempo;           /* MIDI clock in: the tempo it comes at, BPM (0: none yet) */
    uint8_t loop;                 /* a song plays again from its first step */
    uint16_t tempo;               /* BPM */
    uint32_t tick;                /* in the part */
    uint32_t clock;               /* ticks since start: the notes' offs */
    int32_t countin;              /* ticks of count-in left */
    uint32_t take_n, take_beats;  /* the take's events; the length a first take sets */
    uint8_t take_part, take_track, take_full;
    uint8_t take_nothing;         /* a take ended with nothing to keep (the app says so, and clears it) */
    uint8_t aux;                  /* the clock runs for the drums alone (the sequencer stopped: sqp_aux_play) */
    uint8_t started;              /* 1 from the top, 2 continued: for the drums' first tick to see (they clear it) */
} sqp_t;

extern volatile sqp_t sqp;
extern sq_raw_t sqp_take[SQP_TAKE_MAX];

/* the app's: where the events go (the sampler by channel, USB MIDI out), the click, a take done */
void sqp_emit(uint32_t st, uint32_t d1, uint32_t d2);
void sqp_click(int accent);
void sqp_midi_rt(uint32_t b);     /* a realtime byte out (MIDI clock) */
/* the app's: every tick of the clock while the sequencer plays (after its tracks', before the tick moves on: sqp.tick
 * is the one playing) or the drums run alone (sqp.aux): the drum machine's (hr16/) */
void sqp_on_tick(void);

void sqp_init(float fs);
void sqp_set_tempo(uint32_t bpm);
/* the transport (main loop, inside plat_lock) */
void sqp_play(void);              /* from the top: the part, or the song's first step; with a count-in when armed */
void sqp_stop(void);              /* a take running ends here (a first take sets its part's length) */
void sqp_continue(void);
void sqp_aux_play(int cont);      /* the clock for the drums alone, the sequencer stopped (MIDI start or continue out);
                                   * sqp_stop stops it */
void sqp_punch(void);             /* REC while a take runs: it ends (a first take: with the transport, as stop) */
void sqp_select_part(uint32_t p); /* at once when stopped, at the part's end while playing */
void sqp_select_song(uint32_t s);
void sqp_resync(void);            /* the tracks changed: each track's place found again */
void sqp_all_notes_off(void);
/* the audio interrupt */
uint32_t sqp_frames_to_tick(void);   /* frames until the next tick (the render's next stretch) */
void sqp_advance(uint32_t frames);   /* the stretch rendered: a tick due is played */
void sqp_input(uint32_t st, uint32_t d1, uint32_t d2);   /* MIDI in and the keys: into a take that runs */
uint32_t sqp_beat(void);          /* the beat now, from 1 (count-in: 0) */
int sqp_nearer_last(void);        /* between two ticks: nearer the one just played than the next (a hit played now) */
void sqp_off_after(uint32_t ticks, uint32_t st, uint32_t key);   /* a note's off (st: 8n), so many ticks on */
/* MIDI clock in (sqp.clock_in): a realtime message or the song position (F2: d its 16ths) from MIDI in, frame the
 * render's frame count when it came. The audio interrupt (the render, before the block) */
void sqp_rt_in(uint32_t st, uint32_t d, uint32_t frame);
