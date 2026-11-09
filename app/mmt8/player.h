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
    uint8_t loop;                 /* a song plays again from its first step */
    uint16_t tempo;               /* BPM */
    uint32_t tick;                /* in the part */
    uint32_t clock;               /* ticks since start: the notes' offs */
    int32_t countin;              /* ticks of count-in left */
    uint32_t take_n, take_beats;  /* the take's events; the length a first take sets */
    uint8_t take_part, take_track, take_full;
} sqp_t;

extern volatile sqp_t sqp;
extern sq_raw_t sqp_take[SQP_TAKE_MAX];

/* the app's: where the events go (the sampler by channel, USB MIDI out), the click, a take done */
void sqp_emit(uint32_t st, uint32_t d1, uint32_t d2);
void sqp_click(int accent);
void sqp_midi_rt(uint32_t b);     /* a realtime byte out (MIDI clock) */

void sqp_init(float fs);
void sqp_set_tempo(uint32_t bpm);
/* the transport (main loop, inside plat_lock) */
void sqp_play(void);              /* from the top: the part, or the song's first step; with a count-in when armed */
void sqp_stop(void);              /* a take running ends here (a first take sets its part's length) */
void sqp_continue(void);
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
