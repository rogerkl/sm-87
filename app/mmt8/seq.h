/* SM-87: the MMT-8's memory. 100 parts of 8 tracks and 100 songs, as documentation/MMT-8_Manual.pdf describes them:
 * a part is up to 682 beats at 96 clocks a beat, its 8 tracks played together, each with a MIDI channel (or
 * UNCHANGED: the channels it was recorded on); a song is a list of up to 255 steps, each a part and which of its
 * tracks play.
 *
 * Every track and every song's steps is a blob in one arena of 64 KB, as the MMT-8 kept its memory in one: about
 * 9,000 notes. A track's events are in time order:
 *   a note:       tick (2 bytes, little-endian, from the part's start), 9n, key, velocity, duration (2 bytes)   7 bytes
 *   anything else: tick (2), status, data 1, data 2                                                        5 bytes
 *
 * Pure code: the player (player.c) reads it in the audio interrupt; the main loop changes it inside plat_lock. */
#pragma once
#include <stdint.h>

#define SQ_PARTS 100u
#define SQ_TRACKS 8u
#define SQ_SONGS 100u
#define SQ_STEPS 255u
#define SQ_PPQN 96u
#define SQ_MAX_BEATS 682u         /* 682 x 96 = 65472: a part's clocks fit 16 bits */
#define SQ_ARENA 65536u
#define SQ_NAME 14u
#define SQ_UNCHANGED 0u           /* a track's channel: as recorded */
#define SQ_SCRATCH 16384u        /* a track is made in a scratch this long: at most 2,340 notes a track */
#define SQ_NOTE_LEN 7u
#define SQ_EV_LEN 5u

typedef struct {
    uint16_t beats;               /* 0: an empty part (no length yet) */
    uint8_t chan[SQ_TRACKS];      /* 0 UNCHANGED, 1..16 */
    char name[SQ_NAME];
} sq_part_t;

typedef struct {
    uint8_t tempo;                /* BPM */
    uint8_t rsv;
    char name[SQ_NAME];
} sq_song_t;

typedef struct {
    uint16_t off, len;            /* in the arena */
} sq_blob_t;

#define SQ_BLOBS (SQ_PARTS * SQ_TRACKS + SQ_SONGS)
typedef struct {
    uint32_t magic;
    uint16_t version, used;       /* used: the arena's bytes taken (65536 never: one byte kept) */
    sq_part_t part[SQ_PARTS];
    sq_song_t song[SQ_SONGS];
    sq_blob_t blob[SQ_BLOBS];     /* part p track t: p * 8 + t; song s's steps: 800 + s */
    uint8_t arena[SQ_ARENA];
} sq_mem_t;

extern sq_mem_t sq;

#define SQ_TRK(p, t) ((p) * SQ_TRACKS + (t))
#define SQ_SONG(s) (SQ_PARTS * SQ_TRACKS + (s))
#define SQ_MAGIC 0x38544D4Du      /* "MMT8" */
#define SQ_VERSION 1u

static inline uint32_t sq_ev_len(const uint8_t *e) { return (e[2] & 0xF0u) == 0x90u ? SQ_NOTE_LEN : SQ_EV_LEN; }
static inline uint32_t sq_ev_tick(const uint8_t *e) { return e[0] | (uint32_t)e[1] << 8; }
static inline uint32_t sq_ev_dur(const uint8_t *e) { return e[5] | (uint32_t)e[6] << 8; }
static inline const uint8_t *sq_data(uint32_t blob) { return sq.arena + sq.blob[blob].off; }
static inline uint32_t sq_len(uint32_t blob) { return sq.blob[blob].len; }
static inline uint32_t sq_part_ticks(uint32_t p) { return sq.part[p].beats * SQ_PPQN; }

/* the app's: held around every change of the arena the player reads in the audio interrupt (plat_lock, and the
 * player's places in the tracks found again before the unlock). Only the moves are held, not the sorting */
void sq_lock(void);
void sq_unlock(void);

void sq_clear(void);              /* every part and song empty */
int sq_valid(void);               /* a memory loaded from the flash holds together */
uint32_t sq_free(void);           /* the arena's free bytes */
/* a blob's bytes replaced by data (len bytes); -1: no room (nothing changed) */
int sq_replace(uint32_t blob, const uint8_t *data, uint32_t len);
uint32_t sq_part_used(uint32_t p);   /* tracks with data, a bit each */
int sq_part_empty(uint32_t p);
void sq_part_name_default(uint32_t p);

/* ---- recording: a take, captured as it is played, then made into a track ---- */
typedef struct {
    uint32_t tick;                /* from the take's start (the part's start) */
    uint8_t st, d1, d2, rsv;
} sq_raw_t;

/* the take into track t of part p: notes paired with their offs (one still held ends at the end), sorted, ticks
 * folded into the part (a take may run past its end only on the first, which sets its length: beats). Returns
 * -1 when there is no room. scratch: SQ_SCRATCH bytes, the track being built (every function's scratch) */
int sq_commit(uint32_t p, uint32_t t, const sq_raw_t *take, uint32_t n, uint32_t beats, uint8_t *scratch);

/* ---- songs ---- */
uint32_t sq_song_steps(uint32_t s);
void sq_song_step(uint32_t s, uint32_t i, uint8_t *part, uint8_t *mask);
/* step i set to part and mask (i = the song's length: appended); a part of 0xFF ends the song at i. -1: no room */
int sq_song_set(uint32_t s, uint32_t i, uint8_t part, uint8_t mask, uint8_t *scratch);

/* ---- edits, the MMT-8's ---- */
/* the part's length: beats (1..682); events past the end dropped, a note held over it shortened */
int sq_set_length(uint32_t p, uint32_t beats, uint8_t *scratch);
/* notes to a grid (ticks), in the tracks of mask, as the MMT-8's four ways: the start (its end stays: the default),
 * start and end, the end only, or the whole note moved */
enum { SQ_Q_START, SQ_Q_BOTH, SQ_Q_END, SQ_Q_KEEP };
int sq_quantize(uint32_t p, uint32_t mask, uint32_t grid, uint32_t mode, uint8_t *scratch);
int sq_transpose(uint32_t p, uint32_t mask, int32_t semis, uint8_t *scratch);
int sq_erase(uint32_t p, uint32_t mask);
/* track a over track b (b gets both, a stays) */
int sq_merge(uint32_t p, uint32_t a, uint32_t b, uint8_t *scratch);
/* part a's tracks of mask copied over part b's (b takes a's length when empty) */
int sq_copy_tracks(uint32_t a, uint32_t b, uint32_t mask);

/* ERASE's and COPY's filters, the MMT-8's 22: everything, a kind of event, or one MIDI channel (as recorded) */
enum { SQ_F_ALL, SQ_F_NOTES, SQ_F_BEND, SQ_F_CTRL, SQ_F_PROG, SQ_F_TOUCH, SQ_F_CHAN, SQ_F_N = SQ_F_CHAN + 16 };
int sq_filter_match(const uint8_t *e, uint32_t filter);
/* the events of the filter erased from the tracks of mask (SQ_F_ALL: sq_erase) */
int sq_erase_filter(uint32_t p, uint32_t mask, uint32_t filter, uint8_t *scratch);
/* LENGTH with the beats added or taken off at the part's top: the events move (those in beats taken off go, held
 * notes too) */
int sq_set_length_top(uint32_t p, uint32_t beats, uint8_t *scratch);
/* errors of the copies: no room, a part or song too long, nothing to copy */
enum { SQ_E_ROOM = -1, SQ_E_LONG = -2, SQ_E_NONE = -3 };
/* COPY a part: a's events of the filter after b's, b as long as both (b empty: a copy of a, its channels and name;
 * a = b: twice as long). All or nothing */
int sq_append_part(uint32_t a, uint32_t b, uint32_t filter, uint8_t *scratch);
/* COPY a track: part a's track ta (the filter's events) over part b's track tb, cut to b's length (b without one
 * takes a's). An empty track takes the channel too */
int sq_copy_track(uint32_t a, uint32_t ta, uint32_t b, uint32_t tb, uint32_t filter, uint8_t *scratch);

/* ---- songs, the MMT-8's song edits ---- */
int sq_song_append(uint32_t a, uint32_t b, uint8_t *scratch);   /* COPY: song a's steps after b's */
void sq_song_erase(uint32_t s);
int sq_song_insert(uint32_t s, uint32_t i, uint8_t part, uint8_t mask, uint8_t *scratch);  /* the steps from i on move */
int sq_song_delete(uint32_t s, uint32_t i, uint8_t *scratch);

/* ---- EDIT: a track's events one at a time ---- */
uint32_t sq_events(uint32_t p, uint32_t t);
const uint8_t *sq_event(uint32_t p, uint32_t t, uint32_t i);    /* 0 past the last */
/* event i replaced by ev (7 bytes for a note, else 5), or added (i = the count); the track sorted again. Returns
 * ev's index then, or -1 (no room) */
int32_t sq_event_put(uint32_t p, uint32_t t, uint32_t i, const uint8_t *ev, uint8_t *scratch);
int sq_event_delete(uint32_t p, uint32_t t, uint32_t i, uint8_t *scratch);
