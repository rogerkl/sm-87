/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the MMT-8's memory and player: the arena, takes made into tracks, the edits, playing a part on time, the
 * count-in and a first take setting its length, mutes, a part queued, a song. Built and run by test/run.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "player.h"

static int fails, checks;
#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        checks++;                                                                                                     \
        if (!(cond)) {                                                                                                \
            fails++;                                                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                      \
            printf("\n");                                                                                             \
        }                                                                                                             \
    } while (0)

#define FS 44100.0f
static uint8_t scratch[SQ_ARENA];

void sq_lock(void) {}
void sq_unlock(void) { sqp_resync(); }

/* what the player sent, with the frame it went at */
static struct {
    uint32_t frame, st, d1, d2;
} out[4096];
static uint32_t n_out, frame_now, clicks, accents, rt_f8, rt_start, rt_stop;

void sqp_emit(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (n_out < 4096u) {
        out[n_out].frame = frame_now;
        out[n_out].st = st;
        out[n_out].d1 = d1;
        out[n_out].d2 = d2;
        n_out++;
    }
}
void sqp_on_tick(void) {}
void sqp_click(int accent)
{
    clicks++;
    accents += (uint32_t)accent;
}
void sqp_midi_rt(uint32_t b)
{
    rt_f8 += b == 0xF8u;
    rt_start += b == 0xFAu;
    rt_stop += b == 0xFCu;
}

/* as app_render does it: stretches to the next tick, 128 frames a block */
static void run(uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames) {
        uint32_t blk = frames - done < 128u ? frames - done : 128u, k = 0;
        while (k < blk) {
            uint32_t seg = sqp_frames_to_tick();
            if (seg > blk - k)
                seg = blk - k;
            k += seg;
            frame_now += seg;
            sqp_advance(seg);
        }
        done += blk;
    }
}

static uint32_t frames_for_ticks(float ticks) { return (uint32_t)(ticks * FS * 60.0f / (120.0f * 96.0f)); }

static void raw(sq_raw_t *r, uint32_t tick, uint32_t st, uint32_t d1, uint32_t d2)
{
    r->tick = tick;
    r->st = (uint8_t)st;
    r->d1 = (uint8_t)d1;
    r->d2 = (uint8_t)d2;
}

static void test_arena(void)
{
    uint8_t a[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    sq_clear();
    CHECK(sq_valid() && sq_free() == SQ_ARENA - 1u, "empty");
    CHECK(sq_replace(SQ_TRK(0, 0), a, 5) == 0 && sq_replace(SQ_TRK(3, 2), a + 5, 5) == 0 &&
              sq_replace(SQ_SONG(7), a, 9) == 0,
          "three blobs");
    CHECK(sq_replace(SQ_TRK(0, 0), a, 10) == 0, "grown");
    CHECK(!memcmp(sq_data(SQ_TRK(3, 2)), a + 5, 5) && !memcmp(sq_data(SQ_SONG(7)), a, 9) &&
              !memcmp(sq_data(SQ_TRK(0, 0)), a, 10),
          "the others moved with it");
    CHECK(sq_replace(SQ_TRK(3, 2), 0, 0) == 0 && sq_len(SQ_TRK(3, 2)) == 0u && sq.used == 19u && sq_valid(),
          "deleted: used %u", sq.used);
    CHECK(!memcmp(sq_data(SQ_SONG(7)), a, 9), "after the delete");
    sq_clear();
}

static void test_commit(void)
{
    sq_raw_t t[8];
    const uint8_t *d;
    /* out of order, an off by note on 0, a note still held at the end, a controller */
    raw(&t[0], 10, 0x90, 60, 100);
    raw(&t[1], 5, 0xB0, 1, 64);
    raw(&t[2], 40, 0x80, 60, 0);
    raw(&t[3], 50, 0x90, 62, 90);
    raw(&t[4], 70, 0x90, 62, 0);
    raw(&t[5], 300, 0x91, 64, 80);
    raw(&t[6], 500, 0x90, 65, 80);   /* past the end: dropped */
    sq_clear();
    CHECK(sq_commit(4, 2, t, 7, 4, scratch) == 0, "commit");
    d = sq_data(SQ_TRK(4, 2));
    CHECK(sq.part[4].beats == 4u && sq_len(SQ_TRK(4, 2)) == 5u + 3u * 7u, "length %u beats %u",
          sq_len(SQ_TRK(4, 2)), sq.part[4].beats);
    CHECK(sq_ev_tick(d) == 5u && d[2] == 0xB0u, "the controller first");
    CHECK(sq_ev_tick(d + 5) == 10u && d[5 + 3] == 60u && sq_ev_dur(d + 5) == 30u, "note 60, 30 long: %u",
          sq_ev_dur(d + 5));
    CHECK(sq_ev_dur(d + 12) == 20u, "note 62 ended by a note on at 0: %u", sq_ev_dur(d + 12));
    CHECK(sq_ev_tick(d + 19) == 300u && d[19 + 2] == 0x91u && sq_ev_dur(d + 19) == 84u, "held to the end: %u",
          sq_ev_dur(d + 19));
}

static void test_edits(void)
{
    sq_raw_t t[6];
    const uint8_t *d;
    sq_clear();
    raw(&t[0], 13, 0x90, 120, 100);
    raw(&t[1], 50, 0x90, 60, 100);
    raw(&t[2], 70, 0x80, 120, 0);
    raw(&t[3], 80, 0x80, 60, 0);
    raw(&t[4], 150, 0x90, 62, 100);
    raw(&t[5], 190, 0x80, 62, 0);
    sq_commit(0, 0, t, 6, 2, scratch);
    /* the MMT-8's default: the starts to the grid, the ends where they were */
    sq_quantize(0, 1, 24, SQ_Q_START, scratch);
    d = sq_data(SQ_TRK(0, 0));
    CHECK(sq_ev_tick(d) == 24u && sq_ev_dur(d) == 46u && sq_ev_tick(d + 7) == 48u && sq_ev_dur(d + 7) == 32u &&
              sq_ev_tick(d + 14) == 144u && sq_ev_dur(d + 14) == 46u,
          "quantized to 1/16: %u+%u %u+%u %u+%u", sq_ev_tick(d), sq_ev_dur(d), sq_ev_tick(d + 7), sq_ev_dur(d + 7),
          sq_ev_tick(d + 14), sq_ev_dur(d + 14));
    sq_quantize(0, 1, 24, SQ_Q_BOTH, scratch);
    d = sq_data(SQ_TRK(0, 0));
    CHECK(sq_ev_dur(d) == 48u && sq_ev_dur(d + 7) == 24u, "and the ends: %u %u", sq_ev_dur(d), sq_ev_dur(d + 7));
    sq_transpose(0, 1, 12, scratch);
    d = sq_data(SQ_TRK(0, 0));
    CHECK(d[3] == 120u && d[7 + 3] == 72u && d[14 + 3] == 74u, "transposed, 132 folded back to 120: %u %u", d[3],
          d[7 + 3]);
    sq_set_length(0, 1, scratch);
    d = sq_data(SQ_TRK(0, 0));
    CHECK(sq.part[0].beats == 1u && sq_len(SQ_TRK(0, 0)) == 14u && sq_ev_dur(d) == 48u,
          "a beat: the third note gone, %u bytes", sq_len(SQ_TRK(0, 0)));
    sq.part[0].chan[1] = 5;
    sq_replace(SQ_TRK(0, 1), sq_data(SQ_TRK(0, 0)), 7);
    sq_merge(0, 0, 1, scratch);
    d = sq_data(SQ_TRK(0, 1));
    CHECK(sq_len(SQ_TRK(0, 1)) == 21u && sq.part[0].chan[1] == 0u && (d[2] & 0x0Fu) == 4u,
          "merged: %u bytes, channel %u baked in", sq_len(SQ_TRK(0, 1)), d[2] & 0x0Fu);
    sq_copy_tracks(0, 9, 3);
    CHECK(sq_len(SQ_TRK(9, 1)) == 21u && sq.part[9].beats == 1u && sq_valid(), "copied to part 10");
    sq_erase(9, 3);
    CHECK(sq_part_empty(9) && sq_valid(), "erased");
}

/* the ticks of a track's events, for the checks */
static uint32_t ticks_of(uint32_t p, uint32_t t, uint32_t *out, uint32_t max)
{
    uint32_t n = sq_events(p, t), i;
    for (i = 0; i < n && i < max; i++)
        out[i] = sq_ev_tick(sq_event(p, t, i));
    return n;
}

/* part 1, track 1: a note at 0 (half a beat), a bend on channel 2 at 100, a program change at 150; two beats */
static void two_beats(int bend)
{
    sq_raw_t t[4];
    uint32_t n = 0;
    sq_clear();
    raw(&t[n++], 0, 0x90, 60, 100);
    raw(&t[n++], 48, 0x80, 60, 0);
    if (bend)
        raw(&t[n++], 100, 0xE1, 0, 80);
    raw(&t[n++], 150, 0xC0, 5, 0);
    sq_commit(0, 0, t, n, 2, scratch);
}

static void test_panel_edits(void)
{
    uint32_t tk[8], n;
    uint8_t part, mask, ev[SQ_NOTE_LEN];
    int32_t at;
    int r;
    two_beats(1);
    CHECK(!memcmp(sq.part[0].name, "NO PART NAME", 12), "a part once used: NO PART NAME");
    /* ERASE with a filter: channel 2 only (the bend), then the notes only */
    sq_erase_filter(0, 1, SQ_F_CHAN + 1u, scratch);
    CHECK(sq_events(0, 0) == 2u && sq_len(SQ_TRK(0, 0)) == 12u, "channel 2 erased: %u events", sq_events(0, 0));
    sq_erase_filter(0, 1, SQ_F_NOTES, scratch);
    CHECK(sq_events(0, 0) == 1u && (sq_event(0, 0, 0)[2] & 0xF0u) == 0xC0u, "the notes erased: the program left");
    sq_erase(0, 0xFFu);
    CHECK(sq_part_empty(0) && !memcmp(sq.part[0].name, "EMPTY PART", 10), "the whole part: EMPTY PART again");

    /* LENGTH at the top: two beats more (everything later), then three off (the note in them gone) */
    two_beats(0);
    sq_set_length_top(0, 4, scratch);
    n = ticks_of(0, 0, tk, 8);
    CHECK(sq.part[0].beats == 4u && n == 2u && tk[0] == 192u && tk[1] == 342u, "4 beats from the top: %u %u", tk[0],
          tk[1]);
    sq_set_length_top(0, 1, scratch);
    n = ticks_of(0, 0, tk, 8);
    CHECK(sq.part[0].beats == 1u && n == 1u && tk[0] == 54u, "1 beat: the program at %u, the note gone", tk[0]);

    /* COPY a part: to an empty one (a copy, its channels and name), to itself (twice as long), notes only */
    two_beats(0);
    sq.part[0].chan[0] = 3;
    memcpy(sq.part[0].name, "VERSE         ", SQ_NAME);
    r = sq_append_part(0, 5, SQ_F_ALL, scratch);
    CHECK(r == 0 && sq.part[5].beats == 2u && sq.part[5].chan[0] == 3u && !memcmp(sq.part[5].name, "VERSE", 5) &&
              sq_len(SQ_TRK(5, 0)) == sq_len(SQ_TRK(0, 0)) &&
              !memcmp(sq_data(SQ_TRK(5, 0)), sq_data(SQ_TRK(0, 0)), sq_len(SQ_TRK(0, 0))),
          "part 1 to an empty part 6: a copy (%d)", r);
    r = sq_append_part(0, 0, SQ_F_ALL, scratch);
    n = ticks_of(0, 0, tk, 8);
    CHECK(r == 0 && sq.part[0].beats == 4u && n == 4u && tk[0] == 0u && tk[1] == 150u && tk[2] == 192u &&
              tk[3] == 342u,
          "part 1 to itself: 4 beats, %u events", n);
    r = sq_append_part(0, 5, SQ_F_NOTES, scratch);
    n = ticks_of(5, 0, tk, 8);
    CHECK(r == 0 && sq.part[5].beats == 6u && n == 4u && tk[2] == 192u && tk[3] == 384u,
          "its notes after part 6's: 6 beats, %u events", n);
    sq_set_length(7, 682, scratch);
    CHECK(sq_append_part(0, 7, SQ_F_ALL, scratch) == SQ_E_LONG && sq.part[7].beats == 682u, "past 682 beats: refused");
    CHECK(sq_append_part(20, 21, SQ_F_ALL, scratch) == SQ_E_NONE, "an empty part: nothing to copy");
    /* COPY a track: to an empty part (its length), to a part of one beat (cut to it) */
    r = sq_copy_track(0, 0, 9, 2, SQ_F_ALL, scratch);
    CHECK(r == 0 && sq.part[9].beats == 4u && sq_events(9, 2) == 4u && sq.part[9].chan[2] == 3u,
          "track 1 to part 10's track 3: %u events", sq_events(9, 2));
    sq_set_length(10, 1, scratch);
    r = sq_copy_track(0, 0, 10, 1, SQ_F_ALL, scratch);
    CHECK(r == 0 && sq.part[10].beats == 1u && sq_events(10, 1) == 1u, "to a part of one beat: %u events",
          sq_events(10, 1));
    CHECK(sq_valid(), "the memory holds together after the copies");

    /* songs: COPY to itself, a step in, a step out, COPY to an empty one, ERASE */
    sq_song_set(0, 0, 1, 0xFFu, scratch);
    sq_song_set(0, 1, 2, 0x0Fu, scratch);
    r = sq_song_append(0, 0, scratch);
    CHECK(r == 0 && sq_song_steps(0) == 4u, "a song to itself: %u steps", sq_song_steps(0));
    sq_song_insert(0, 1, 7, 0x01u, SQ_NO_DRUMS, scratch);
    sq_song_step(0, 1, &part, &mask);
    CHECK(sq_song_steps(0) == 5u && part == 7u && mask == 1u, "a step in at 2: part %u", part);
    sq_song_step(0, 2, &part, &mask);
    CHECK(part == 2u && mask == 0x0Fu, "the steps after it moved: part %u", part);
    sq_song_delete(0, 0, scratch);
    sq_song_step(0, 0, &part, &mask);
    CHECK(sq_song_steps(0) == 4u && part == 7u, "the first step out: %u steps, part %u first", sq_song_steps(0), part);
    r = sq_song_append(0, 3, scratch);
    CHECK(r == 0 && sq_song_steps(3) == 4u, "to an empty song");
    sq_song_erase(0);
    CHECK(sq_song_steps(0) == 0u && sq_song_append(0, 3, scratch) == SQ_E_NONE, "erased: nothing to copy");

    /* EDIT: an event moved (it goes where its tick is), one added, one taken out */
    n = sq_events(0, 0);
    memcpy(ev, sq_event(0, 0, 2), SQ_NOTE_LEN);  /* the note at 192 */
    ev[0] = 10;
    ev[1] = 0;
    at = sq_event_put(0, 0, 2, ev, scratch);
    n = ticks_of(0, 0, tk, 8);
    CHECK(at == 1 && n == 4u && tk[0] == 0u && tk[1] == 10u && tk[2] == 150u && tk[3] == 342u,
          "the note moved to tick 10: index %d, ticks %u %u %u %u", at, tk[0], tk[1], tk[2], tk[3]);
    ev[0] = 0;
    ev[2] = 0xB0;
    ev[3] = 7;
    ev[4] = 100;
    at = sq_event_put(0, 0, 4, ev, scratch);
    n = ticks_of(0, 0, tk, 8);
    CHECK(at == 1 && n == 5u && sq_len(SQ_TRK(0, 0)) == 7u + 5u + 7u + 5u + 5u, "a controller added at 0: index %d",
          at);
    sq_event_delete(0, 0, 1, scratch);
    n = ticks_of(0, 0, tk, 8);
    CHECK(n == 4u && tk[1] == 10u && sq_valid(), "and taken out again: %u events", n);
}

static void test_play(void)
{
    sq_raw_t t[4];
    uint32_t i, ons = 0;
    sq_clear();
    raw(&t[0], 0, 0x90, 60, 100);
    raw(&t[1], 48, 0x80, 60, 0);
    raw(&t[2], 96, 0x90, 64, 90);
    raw(&t[3], 144, 0x80, 64, 0);
    sq_commit(0, 0, t, 4, 2, scratch);
    sqp_init(FS);
    n_out = frame_now = 0;
    sqp.part = 0;
    sqp_play();
    run(frames_for_ticks(2.0f * 96.0f) + 10u);
    for (i = 0; i < n_out; i++)
        ons += (out[i].st & 0xF0u) == 0x90u;
    CHECK(ons == 3u, "two notes and the loop's first again: %u note ons", ons);
    CHECK(out[0].st == 0x90u && out[0].d1 == 60u && out[0].frame == 0u, "the first at once");
    CHECK(out[1].st == 0x80u && out[1].d1 == 60u &&
              fabsf((float)out[1].frame - (float)frames_for_ticks(48.0f)) <= 2.0f,
          "its off half a beat later: frame %u, want %u", out[1].frame, frames_for_ticks(48.0f));
    CHECK(out[2].d1 == 64u && fabsf((float)out[2].frame - (float)frames_for_ticks(96.0f)) <= 2.0f,
          "the second on the beat: frame %u", out[2].frame);
    CHECK(rt_start == 1u && rt_f8 >= 48u, "MIDI clock: start %u, %u clocks", rt_start, rt_f8);
    /* muted: nothing from track 1 */
    sqp_stop();
    n_out = 0;
    sqp.mask = 0xFEu;
    sqp_play();
    run(frames_for_ticks(2.0f * 96.0f));
    CHECK(n_out == 0u, "muted: %u events", n_out);
    sqp_stop();
    sqp.mask = 0xFFu;
}

static void test_record(void)
{
    uint32_t c0;
    sq_clear();
    sqp_init(FS);
    n_out = frame_now = clicks = accents = 0;
    sqp.part = 3;
    sqp.rec_armed = 1;
    sqp.rec_track = 2;
    sqp_play();
    CHECK(sqp.state == SQP_COUNTIN, "a count-in first");
    run(frames_for_ticks(4.0f * 96.0f) + 300u);
    CHECK(sqp.state == SQP_PLAYING && clicks == 5u && accents == 2u, "four clicks, then the first beat's: %u (%u)",
          clicks, accents);
    c0 = clicks;
    run(frames_for_ticks(10.0f));
    sqp_input(0x90, 60, 100);
    run(frames_for_ticks(48.0f));
    sqp_input(0x80, 60, 0);
    run(frames_for_ticks(96.0f + 30.0f));     /* stopped a little past two beats: the take is two beats */
    sqp_stop();
    CHECK(clicks - c0 == 1u, "a click a beat while recording (beat 2): %u", clicks - c0);
    CHECK(sqp.take_state == SQP_TAKE_DONE && sqp.take_beats == 2u && sqp.take_n == 2u, "take: %u, %u beats, %u",
          sqp.take_state, sqp.take_beats, sqp.take_n);
    CHECK(sq_commit(sqp.take_part, sqp.take_track, sqp_take, sqp.take_n, sqp.take_beats, scratch) == 0 &&
              sq.part[3].beats == 2u && sq_len(SQ_TRK(3, 2)) == 7u,
          "made a track");
    {
        const uint8_t *d = sq_data(SQ_TRK(3, 2));
        CHECK(sq_ev_tick(d) >= 9u && sq_ev_tick(d) <= 12u && sq_ev_dur(d) >= 46u && sq_ev_dur(d) <= 50u,
              "the note at %u, %u long", sq_ev_tick(d), sq_ev_dur(d));
    }
    /* the next take on a part with a length punches out at its end, and the part plays on */
    sqp.take_state = SQP_TAKE_NONE;
    sqp.rec_armed = 1;
    sqp.rec_track = 0;
    sqp_play();
    run(frames_for_ticks(4.0f * 96.0f + 2.0f * 96.0f + 20.0f));
    CHECK(sqp.take_state == SQP_TAKE_DONE && sqp.state == SQP_PLAYING && sqp.tick < 40u,
          "punched out at the end: take %u, state %u, tick %u", sqp.take_state, sqp.state, sqp.tick);
    sqp_stop();
}

/* a note a little early, in the count-in's last sixteenth, lands on the one; earlier ones are not recorded; a take
 * stopped in the count-in, or with nothing in it on an empty part, keeps nothing and says so */
static void test_record_edges(void)
{
    sq_clear();
    sqp_init(FS);
    sqp.part = 0;
    sqp.rec_armed = 1;
    sqp.rec_track = 0;
    sqp_play();
    run(frames_for_ticks(2.0f * 96.0f));
    sqp_input(0x90, 60, 100);
    CHECK(sqp.state == SQP_COUNTIN && sqp.take_n == 0u, "halfway through the count-in: not recorded (%u)", sqp.take_n);
    run(frames_for_ticks(2.0f * 96.0f - 12.0f));
    sqp_input(0x90, 62, 100);
    CHECK(sqp.state == SQP_COUNTIN && sqp.take_n == 1u && sqp_take[0].tick == 0u && sqp_take[0].d1 == 62u,
          "in its last sixteenth (%d left): on the one", sqp.countin);
    sqp_stop();
    CHECK(sqp.take_state == SQP_TAKE_NONE && sqp.take_nothing == 1u, "stopped in the count-in: nothing kept, said");
    sqp.take_nothing = 0;
    sqp.rec_armed = 1;
    sqp_play();
    run(frames_for_ticks(4.0f * 96.0f + 150.0f));
    sqp_stop();
    CHECK(sqp.take_state == SQP_TAKE_NONE && sqp.take_nothing == 1u && sq_part_empty(0),
          "nothing played into an empty part: nothing kept, said");
}

/* an empty part has no end: one chosen while it plays starts on the next beat (not at an end that never comes); a
 * part queued goes when the sequencer stops */
static void test_empty_part(void)
{
    sq_raw_t t[2];
    sq_clear();
    raw(&t[0], 0, 0x90, 60, 100);
    raw(&t[1], 10, 0x80, 60, 0);
    sq_commit(0, 0, t, 2, 1, scratch);     /* part 00: a beat; part 01 empty */
    sqp_init(FS);
    sqp.part = 1;
    sqp_play();
    run(frames_for_ticks(330.0f));
    CHECK(sqp.state == SQP_PLAYING && sqp.part == 1u && sqp.tick >= 300u, "the empty part plays on: tick %u",
          (unsigned)sqp.tick);
    sqp_select_part(0);
    CHECK(sqp.next_part == 0u, "part 00 queued");
    run(frames_for_ticks(40.0f));       /* tick 370: before the beat at 384 */
    CHECK(sqp.part == 1u, "not before the beat: part %u at tick %u", sqp.part, (unsigned)sqp.tick);
    run(frames_for_ticks(20.0f));       /* past it */
    CHECK(sqp.part == 0u && sqp.next_part == 0xFFu && sqp.tick < 20u, "on the next beat: part %u, tick %u", sqp.part,
          (unsigned)sqp.tick);
    sqp_select_part(1);                 /* back to the empty part: at part 00's end, then nothing more */
    run(frames_for_ticks(120.0f));
    CHECK(sqp.part == 1u && sqp.state == SQP_PLAYING, "into the empty part at part 00's end: part %u", sqp.part);
    sqp_select_part(0);
    sqp_stop();
    CHECK(sqp.next_part == 0xFFu && sqp.part == 1u, "stopped: the queue gone, the part as it was (%u)", sqp.part);
    sqp_select_part(0);
    CHECK(sqp.part == 0u && sqp.next_part == 0xFFu, "stopped: chosen at once");
}

static void test_queue_and_song(void)
{
    sq_raw_t t[2];
    uint32_t i, seen[3] = {0, 0, 0};
    sq_clear();
    for (i = 0; i < 3u; i++) {
        raw(&t[0], 0, 0x90, 60 + i, 100);
        raw(&t[1], 10, 0x80, 60 + i, 0);
        sq_commit(i, 0, t, 2, 1, scratch);
    }
    sqp_init(FS);
    n_out = 0;
    sqp.part = 0;
    sqp_play();
    run(frames_for_ticks(30.0f));
    sqp_select_part(2);
    CHECK(sqp.part == 0u && sqp.next_part == 2u, "queued");
    run(frames_for_ticks(96.0f));
    CHECK(sqp.part == 2u, "switched at the part's end: %u", sqp.part);
    sqp_stop();
    /* a song: part 1, then part 3, then the end */
    sq_song_set(5, 0, 1, 0xFF, scratch);
    sq_song_set(5, 1, 2, 0xFF, scratch);
    sq.song[5].tempo = 120;
    CHECK(sq_song_steps(5) == 2u, "two steps");
    sqp.song_mode = 1;
    sqp_select_song(5);
    n_out = 0;
    sqp_play();
    run(frames_for_ticks(3.0f * 96.0f));
    for (i = 0; i < n_out; i++)
        if ((out[i].st & 0xF0u) == 0x90u)
            seen[out[i].d1 - 60u]++;
    CHECK(seen[0] == 0u && seen[1] == 1u && seen[2] == 1u && sqp.state == SQP_STOPPED,
          "the song played parts 2 and 3 once and stopped: %u %u %u, state %u", seen[0], seen[1], seen[2], sqp.state);
    sq_song_set(5, 1, 0xFF, 0, scratch);
    CHECK(sq_song_steps(5) == 1u, "ended at step 2");
    sqp.song_mode = 0;
}

/* SONG QUANTIZE: track 1 ten clocks late, track 2 ten early (its first beat can't be: at once), the early note of the
 * second step before the step changes, and not cut there */
static void test_song_offsets(void)
{
    sq_raw_t t[2];
    uint32_t i, f60 = 0, f70 = 0, f72 = 0, off72 = 0, n60 = 0, n72 = 0;
    sq_clear();
    raw(&t[0], 0, 0x90, 60, 100);
    raw(&t[1], 20, 0x80, 60, 0);
    sq_commit(0, 0, t, 2, 1, scratch);
    raw(&t[0], 0, 0x90, 70, 100);
    raw(&t[1], 20, 0x80, 70, 0);
    sq_commit(0, 1, t, 2, 1, scratch);
    raw(&t[0], 0, 0x90, 72, 100);
    raw(&t[1], 20, 0x80, 72, 0);
    sq_commit(1, 1, t, 2, 1, scratch);
    sq_song_set(6, 0, 0, 0xFF, scratch);
    sq_song_set(6, 1, 1, 0xFF, scratch);
    sq.song[6].tempo = 120;
    sq.offset[0] = 10;
    sq.offset[1] = -10;
    sqp_init(FS);
    sqp.song_mode = 1;
    sqp_select_song(6);
    n_out = frame_now = 0;
    sqp_play();
    run(frames_for_ticks(3.0f * 96.0f));
    for (i = 0; i < n_out; i++) {
        if (out[i].st == 0x90u && out[i].d1 == 60u)
            f60 = out[i].frame, n60++;
        if (out[i].st == 0x90u && out[i].d1 == 70u)
            f70 = out[i].frame;
        if (out[i].st == 0x90u && out[i].d1 == 72u)
            f72 = out[i].frame, n72++;
        if (out[i].st == 0x80u && out[i].d1 == 72u && !off72)
            off72 = out[i].frame;
    }
    CHECK(n60 == 1u && fabsf((float)f60 - (float)frames_for_ticks(10.0f)) <= 2.0f, "late: clock 10 (frame %u, want %u)",
          f60, frames_for_ticks(10.0f));
    CHECK(f70 == 0u, "early on the song's first beat: at once (frame %u)", f70);
    CHECK(n72 == 1u && fabsf((float)f72 - (float)frames_for_ticks(86.0f)) <= 2.0f,
          "early from step 2: clock 86, before the step (frame %u, want %u)", f72, frames_for_ticks(86.0f));
    CHECK(fabsf((float)off72 - (float)frames_for_ticks(106.0f)) <= 2.0f, "and not cut at the step: off at %u, want %u",
          off72, frames_for_ticks(106.0f));
    CHECK(sqp.state == SQP_STOPPED, "the song over");
    sqp.song_mode = 0;
}

/* a memory saved by an older version loads: version 1 (no song offsets), 3 (two-byte song steps), 4 (no pattern's
 * own sounds), 5 (no HR-16 header) */
static void test_image_versions(void)
{
    static uint8_t img[sizeof(sq_mem_t)];
    uint32_t t1 = sq_image_tables(1), t3 = sq_image_tables(3), t4 = sq_image_tables(4), t5 = sq_image_tables(5);
    uint32_t t6 = sq_image_tables(6), used, i;
    sq_raw_t t[2];
    uint8_t part, mask;
    CHECK(SQ_VERSION == 6u && t6 == (uint32_t)(sq.arena - (uint8_t *)&sq) && t1 == t3 - SQ_TRACKS - SQ_MMT8_HDR &&
              sq_image_tables(2) == t3 - SQ_MMT8_HDR && t4 == t3 + SQ_PATTERNS * 4u + SQ_PATTERNS * 2u &&
              t5 == t4 + SQ_PATTERNS * 4u && t6 == t5 + SQ_HR16_HDR + SQ_HR16_DRUMS && !sq_image_tables(9),
          "tables: %u, version 5 %u, version 4 %u, version 3 %u, version 1 %u", t6, t5, t4, t3, t1);
    sq_clear();
    raw(&t[0], 0, 0x90, 60, 100);
    raw(&t[1], 20, 0x80, 60, 0);
    sq_commit(4, 2, t, 2, 3, scratch);
    used = sq.used;
    memcpy(img, &sq, t1);         /* as version 1 saved it: the tables up to the offsets, then the arena */
    img[4] = 1;
    img[5] = 0;
    memcpy(img + t1, sq.arena, used);
    memset(&sq, 0x55, sizeof sq);
    memcpy(&sq, img, t1);
    memcpy(sq.arena, img + t1, used);
    CHECK(sq_image_upgrade(scratch) == 0 && sq.version == SQ_VERSION && sq_valid() && sq.offset[0] == 0 &&
              sq.offset[7] == 0 && sq.part[4].beats == 3u && sq_events(4, 2) == 1u,
          "version 1 loaded: version %u, valid %d", sq.version, sq_valid());

    /* version 3, as it was saved: its blobs (parts and songs), the offsets, the header; a song's steps two bytes */
    sq_clear();
    sq_commit(4, 2, t, 2, 3, scratch);
    sq.offset[3] = -7;
    sq.mmt8[0xCF] = 0x42;
    memcpy(img, &sq, t1);
    img[4] = 3;
    {
        uint8_t steps[6] = {4, 0xFF, 9, 0x05, 4, 0x01};   /* song 2: part 4, part 9 (tracks 1 and 3), part 4 */
        used = sq.used;
        memcpy(img + t1, &sq.offset, SQ_TRACKS);
        memcpy(img + t1 + SQ_TRACKS, sq.mmt8, SQ_MMT8_HDR);
        /* the song's blob after the track's: blob 802 in version 3's table */
        ((sq_blob_t *)(img + offsetof(sq_mem_t, blob)))[SQ_SONG(2)].off = (uint16_t)used;
        ((sq_blob_t *)(img + offsetof(sq_mem_t, blob)))[SQ_SONG(2)].len = 6;
        memcpy(img + t3, sq.arena, used);
        memcpy(img + t3 + used, steps, 6);
        ((uint16_t *)(img + 4))[1] = (uint16_t)(used + 6u);   /* sq.used */
        memset(&sq, 0x55, sizeof sq);
        memcpy(&sq, img, t3);
        memcpy(sq.arena, img + t3, used + 6u);
    }
    CHECK(sq_image_upgrade(scratch) == 0 && sq.version == SQ_VERSION && sq_valid(), "version 3 loaded: valid %d",
          sq_valid());
    sq_song_step(2, 1, &part, &mask);
    CHECK(sq_song_steps(2) == 3u && part == 9u && mask == 0x05u && sq_song_drums(2, 1) == SQ_NO_DRUMS &&
              sq_song_drums(2, 2) == SQ_NO_DRUMS,
          "its song: %u steps, step 2 part %u tracks %02X drums %u", sq_song_steps(2), part, mask, sq_song_drums(2, 1));
    for (i = 0, used = 0; i < SQ_PATTERNS; i++)
        used += sq_len(SQ_PAT(i)) + sq.pattern[i].beats;
    CHECK(sq.offset[3] == -7 && sq.mmt8[0xCF] == 0x42 && used == 0 && sq_events(4, 2) == 1u,
          "its offsets and header moved, no drum patterns: offset %d, header %02X", sq.offset[3], sq.mmt8[0xCF]);

    /* version 4, as it was saved: the drum patterns' blobs but not their sounds', then the offsets, the header, the
     * patterns' lengths */
    sq_clear();
    sq_commit(4, 2, t, 2, 3, scratch);
    sq_song_set(2, 0, 4, 0xFF, scratch);
    sq_song_set_drums(2, 0, 7);
    {
        uint8_t hits[8] = {0, 0, 0, 100, 48, 0, 2, 90};
        uint32_t blobs4 = (uint32_t)offsetof(sq_mem_t, blob) + SQ_PAT(SQ_PATTERNS) * sizeof(sq_blob_t);
        uint32_t tail = (uint32_t)(offsetof(sq_mem_t, hr16) - offsetof(sq_mem_t, offset));
        sq_replace(SQ_PAT(7), hits, 8);
        sq.pattern[7].beats = 2;
        sq.offset[5] = 9;
        sq.mmt8[3] = 0x33;
        used = sq.used;
        memcpy(img, &sq, blobs4);
        img[4] = 4;
        memcpy(img + blobs4, &sq.offset, tail);
        CHECK(blobs4 + tail == t4, "version 4's tables: %u", blobs4 + tail);
        memcpy(img + t4, sq.arena, used);
        memset(&sq, 0x55, sizeof sq);
        memcpy(&sq, img, t4);
        memcpy(sq.arena, img + t4, used);
    }
    CHECK(sq_image_upgrade(scratch) == 0 && sq.version == SQ_VERSION && sq_valid(), "version 4 loaded: valid %d",
          sq_valid());
    for (i = 0, used = 0; i < SQ_PATTERNS; i++)
        used += sq_len(SQ_PSND(i));
    CHECK(sq_len(SQ_PAT(7)) == 8u && sq_data(SQ_PAT(7))[6] == 2u && sq.pattern[7].beats == 2u &&
              sq_song_drums(2, 0) == 7u && sq.offset[5] == 9 && sq.mmt8[3] == 0x33 && used == 0 &&
              sq_events(4, 2) == 1u,
          "its drum patterns, song, offsets and header kept, no pattern's sounds: beats %u, offset %d",
          sq.pattern[7].beats, sq.offset[5]);

    /* version 5, as it was saved: everything but the HR-16's header, after the patterns' lengths */
    sq_clear();
    sq_commit(4, 2, t, 2, 3, scratch);
    {
        uint8_t hits[4] = {0, 0, 0, 100}, snd[SQ_SOUNDS_LEN] = {0};
        sq_replace(SQ_PAT(3), hits, 4);
        sq_replace(SQ_PSND(3), snd, SQ_SOUNDS_LEN);
        sq.pattern[3].beats = 4;
        sq.mmt8[7] = 0x77;
        used = sq.used;
        memcpy(img, &sq, t5);
        img[4] = 5;
        memcpy(img + t5, sq.arena, used);
        memset(&sq, 0x55, sizeof sq);
        memcpy(&sq, img, t5);
        memcpy(sq.arena, img + t5, used);
    }
    CHECK(sq_image_upgrade(scratch) == 0 && sq.version == SQ_VERSION && sq_valid(), "version 5 loaded: valid %d",
          sq_valid());
    for (i = 0, used = 0; i < SQ_HR16_HDR; i++)
        used |= sq.hr16[i];
    for (i = 0; i < SQ_HR16_DRUMS; i++)
        used |= sq.hr16_voice[i];
    CHECK(sq_len(SQ_PAT(3)) == 4u && sq_len(SQ_PSND(3)) == SQ_SOUNDS_LEN && sq.pattern[3].beats == 4u &&
              sq.mmt8[7] == 0x77 && sq_events(4, 2) == 1u && used == 0,
          "its patterns, their sounds and the MMT-8 header kept, no HR-16 header: beats %u", sq.pattern[3].beats);
}

/* MIDI clock in, as app_render has it: the clocks that came during a block given at its start (midi_in) */
static float clk_next, clk_per;
static uint32_t clk_at[512], n_clk;
static void run_clocked(uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames) {
        uint32_t blk = frames - done < 128u ? frames - done : 128u, k = 0;
        while (clk_next <= (float)frame_now) {
            sqp_rt_in(0xF8, 0, frame_now);
            if (n_clk < 512u)
                clk_at[n_clk++] = frame_now;
            clk_next += clk_per;
        }
        while (k < blk) {
            uint32_t seg = sqp_frames_to_tick();
            if (seg > blk - k)
                seg = blk - k;
            k += seg;
            frame_now += seg;
            sqp_advance(seg);
        }
        done += blk;
    }
}

/* the frame note key went on at, from the i-th on (0: none) */
static uint32_t note_frame(uint32_t key, uint32_t from_i, uint32_t *i_out)
{
    uint32_t i;
    for (i = from_i; i < n_out; i++)
        if (out[i].st == 0x90u && out[i].d1 == key) {
            *i_out = i + 1u;
            return out[i].frame;
        }
    *i_out = n_out;
    return 0;
}

static void test_clock_in(void)
{
    sq_raw_t t[6];
    uint32_t c0, i = 0, f60, f62, f64, f60b, c1;
    sq_clear();
    raw(&t[0], 0, 0x90, 60, 100);
    raw(&t[1], 10, 0x80, 60, 0);
    raw(&t[2], 48, 0x90, 62, 100);
    raw(&t[3], 58, 0x80, 62, 0);
    raw(&t[4], 96, 0x90, 64, 100);
    raw(&t[5], 106, 0x80, 64, 0);
    sq_commit(0, 0, t, 6, 2, scratch);
    sqp_init(FS);
    sqp.clock_in = 1;
    sqp.part = 0;
    n_out = frame_now = n_clk = 0;
    clk_next = 0.0f;
    clk_per = FS * 60.0f / (100.0f * 24.0f);  /* 100 BPM */
    run_clocked((uint32_t)(clk_per * 48.0f));
    CHECK(n_out == 0u && sqp.state == SQP_STOPPED, "stopped: the clocks play nothing (%u out)", n_out);
    CHECK(sqp.ext_tempo >= 99u && sqp.ext_tempo <= 101u, "their tempo: %u BPM", sqp.ext_tempo);
    sqp_rt_in(0xFA, 0, frame_now);
    c0 = n_clk;                   /* the first clock after the start plays the first tick */
    run_clocked((uint32_t)(clk_per * 30.0f));
    f60 = note_frame(60, 0, &i);
    f62 = note_frame(62, i, &i);
    f64 = note_frame(64, i, &i);
    CHECK(f60 == clk_at[c0] && f62 == clk_at[c0 + 12u] && f64 == clk_at[c0 + 24u],
          "on the clocks: %u %u %u, the clocks %u %u %u", f60, f62, f64, clk_at[c0], clk_at[c0 + 12u],
          clk_at[c0 + 24u]);
    /* faster, 140 BPM: the part's next time round on its clock too */
    clk_per = FS * 60.0f / (140.0f * 24.0f);
    run_clocked((uint32_t)(clk_per * 60.0f));
    f60b = note_frame(60, i, &c1);
    CHECK(f60b == clk_at[c0 + 48u] && sqp.ext_tempo >= 138u && sqp.ext_tempo <= 142u,
          "140 BPM: round again on clock 48 (%u, the clock %u), tempo %u", f60b, clk_at[c0 + 48u], sqp.ext_tempo);
    /* stop, the song position at beat 2 (four 16ths), continue: the next clock plays beat 2 */
    sqp_rt_in(0xFC, 0, frame_now);
    CHECK(sqp.state == SQP_STOPPED, "FC stops");
    sqp_rt_in(0xF2, 4, frame_now);
    CHECK(sqp.tick == 96u, "F2: beat 2 (tick %u)", sqp.tick);
    n_out = 0;
    sqp_rt_in(0xFB, 0, frame_now);
    c1 = n_clk;
    run_clocked((uint32_t)(clk_per * 4.0f));
    f64 = note_frame(64, 0, &i);
    CHECK(f64 && f64 == clk_at[c1], "FB: E3 (beat 2) on the next clock (%u, the clock %u)", f64, clk_at[c1]);
    sqp_stop();
    /* its own clock: the clocks in change nothing */
    sqp.clock_in = 0;
    sqp_rt_in(0xFA, 0, frame_now);
    CHECK(sqp.state == SQP_STOPPED, "clock in off: FA does nothing");
}

int main(void)
{
    test_arena();
    test_commit();
    test_edits();
    test_panel_edits();
    test_play();
    test_record();
    test_record_edges();
    test_empty_part();
    test_queue_and_song();
    test_song_offsets();
    test_image_versions();
    test_clock_in();
    printf("seq: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
