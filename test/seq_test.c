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
    test_play();
    test_record();
    test_queue_and_song();
    printf("seq: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
