/* SM-87: the MMT-8's memory and player: the arena, takes made into tracks, the edits, playing a part on time, the
 * count-in and a first take setting its length, mutes, a part queued, a song. Built and run by test/run.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>
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
              sq_replace(SQ_SONG(7), a, 10) == 0,
          "three blobs");
    CHECK(sq_replace(SQ_TRK(0, 0), a, 10) == 0, "grown");
    CHECK(!memcmp(sq_data(SQ_TRK(3, 2)), a + 5, 5) && !memcmp(sq_data(SQ_SONG(7)), a, 10) &&
              !memcmp(sq_data(SQ_TRK(0, 0)), a, 10),
          "the others moved with it");
    CHECK(sq_replace(SQ_TRK(3, 2), 0, 0) == 0 && sq_len(SQ_TRK(3, 2)) == 0u && sq.used == 20u && sq_valid(),
          "deleted: used %u", sq.used);
    CHECK(!memcmp(sq_data(SQ_SONG(7)), a, 10), "after the delete");
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
    sq_song_insert(0, 1, 7, 0x01u, scratch);
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

int main(void)
{
    test_arena();
    test_commit();
    test_edits();
    test_panel_edits();
    test_play();
    test_record();
    test_record_edges();
    test_queue_and_song();
    printf("seq: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
