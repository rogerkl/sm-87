/* SM-87: the HR-16: its patterns (hits in, out, a pad's out, the length, COPY), quantize with swing, the player (alone,
 * with the sequencer's part, a song's steps), recording as it loops, and the pads' voices (the hats' group, a pad's
 * poly, the pan). Built and run by test/run.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hr.h"
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
static uint8_t scratch[SQ_SCRATCH];

void sq_lock(void) {}
void sq_unlock(void)
{
    sqp_resync();
    hrp_resync();
}
void sqp_emit(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }
void sqp_click(int accent) { (void)accent; }
void sqp_midi_rt(uint32_t b) { (void)b; }
void sqp_on_tick(void) { hrp_tick(); }

/* the pattern hits played: the clock, the pad, the velocity */
static struct {
    uint32_t clock, pad, vel;
} hit[1024];
static uint32_t n_hit;
void hr_emit(uint32_t pad, uint32_t vel)
{
    if (n_hit < 1024u) {
        hit[n_hit].clock = sqp.clock;
        hit[n_hit].pad = pad;
        hit[n_hit].vel = vel;
        n_hit++;
    }
}

/* one tick of the clock, as app_render plays them */
static void tick1(void)
{
    uint32_t seg = sqp_frames_to_tick();
    sqp_advance(seg);
}
static void ticks(uint32_t n)
{
    while (n--)
        tick1();
}
/* the main loop's: what the recording did, into the pattern */
static void take_hits(void)
{
    hr_rec_t r;
    while (hrp_take(&r)) {
        if (r.kind == HR_REC_ERASE)
            hr_remove(r.pattern, r.tick, r.pad, scratch);
        else
            hr_add(r.pattern, r.tick, r.pad, r.vel, scratch);
    }
}

static void test_patterns(void)
{
    const uint8_t *h;
    int r;
    sq_clear();
    CHECK(hr_beats(3) == 8u && hr_hits(3) == 0u, "an empty pattern: %u beats", hr_beats(3));
    hr_add(3, 96, 1, 100, scratch);
    hr_add(3, 0, 2, 90, scratch);
    hr_add(3, 0, 0, 80, scratch);
    hr_add(3, 48, 2, 70, scratch);
    h = hr_hit_at(3, 0);
    CHECK(hr_hits(3) == 4u && h[2] == 0u && hr_hit_at(3, 1)[2] == 2u && hr_hit_tick(hr_hit_at(3, 2)) == 48u &&
              hr_hit_tick(hr_hit_at(3, 3)) == 96u,
          "in order of time, then pad: %u hits", hr_hits(3));
    hr_add(3, 48, 2, 127, scratch);
    CHECK(hr_hits(3) == 4u && hr_hit_at(3, 2)[3] == 127u, "the same place again: its velocity, %u", hr_hit_at(3, 2)[3]);
    hr_remove(3, 0, 2, scratch);
    CHECK(hr_hits(3) == 3u && hr_hit_at(3, 1)[2] == 2u && hr_hit_tick(hr_hit_at(3, 1)) == 48u, "one out: %u",
          hr_hits(3));
    hr_add(3, 200, 2, 50, scratch);
    hr_erase(3, 2, scratch);
    CHECK(hr_hits(3) == 2u && hr_hit_at(3, 0)[2] == 0u && hr_hit_at(3, 1)[2] == 1u, "a pad's hits out: %u left",
          hr_hits(3));
    hr_add(3, 700, 4, 50, scratch);
    r = hr_set_length(3, 4, scratch);
    CHECK(r == 0 && hr_beats(3) == 4u && hr_hits(3) == 2u, "4 beats: the hit at 700 gone, %u left", hr_hits(3));
    /* COPY */
    r = hr_copy(3, 5, scratch);
    CHECK(r == 0 && hr_beats(5) == 4u && hr_hits(5) == 2u, "to an empty one: a copy, %u beats", hr_beats(5));
    r = hr_copy(3, 5, scratch);
    CHECK(r == 0 && hr_beats(5) == 8u && hr_hits(5) == 4u && hr_hit_tick(hr_hit_at(5, 2)) == 384u &&
              hr_hit_tick(hr_hit_at(5, 3)) == 480u,
          "after it: 8 beats, the hits at %u and %u", hr_hit_tick(hr_hit_at(5, 2)), hr_hit_tick(hr_hit_at(5, 3)));
    r = hr_copy(5, 5, scratch);
    CHECK(r == 0 && hr_beats(5) == 16u && hr_hits(5) == 8u, "to itself: twice as long, %u beats", hr_beats(5));
    CHECK(hr_copy(9, 5, scratch) == SQ_E_NONE, "nothing to copy");
    hr_set_length(6, 680, scratch);
    hr_add(6, 0, 0, 100, scratch);
    CHECK(hr_copy(5, 6, scratch) == SQ_E_LONG && hr_hits(6) == 1u, "too long: not done");
    hr_erase(5, HR_PADS, scratch);
    CHECK(hr_hits(5) == 0u && hr_beats(5) == 8u && !sq.pattern[5].beats, "the whole pattern out, its length too");
    CHECK(sq_valid(), "the memory holds together");

    /* quantize: to the nearest, every second place swung */
    CHECK(hr_quantize(13, 24, 50) == 24u && hr_quantize(11, 24, 50) == 0u && hr_quantize(37, 24, 50) == 48u,
          "1/16: 13 -> %u, 11 -> %u, 37 -> %u", hr_quantize(13, 24, 50), hr_quantize(11, 24, 50),
          hr_quantize(37, 24, 50));
    CHECK(hr_quantize(24, 24, 66) == 32u && hr_quantize(15, 24, 66) == 0u && hr_quantize(17, 24, 66) == 32u &&
              hr_quantize(41, 24, 66) == 48u && hr_quantize(72, 24, 66) == 80u,
          "1/16 swung 66%%: 24 -> %u, 15 -> %u, 17 -> %u, 41 -> %u", hr_quantize(24, 24, 66), hr_quantize(15, 24, 66),
          hr_quantize(17, 24, 66), hr_quantize(41, 24, 66));
    CHECK(hr_quantize(37, 1, 66) == 37u && hr_quantize(130, 96, 50) == 96u, "off, and 1/4");
}

/* the hits played between from and to (clocks from the start c0), as "clock:pad" */
static int played(uint32_t from_i, uint32_t c0, const char *want)
{
    char got[512] = "", t[24];
    uint32_t i;
    for (i = from_i; i < n_hit; i++) {
        snprintf(t, sizeof t, "%s%u:%u", got[0] ? " " : "", hit[i].clock - c0, hit[i].pad);
        strcat(got, t);
    }
    if (strcmp(got, want))
        printf("  played: %s\n  wanted: %s\n", got, want);
    return !strcmp(got, want);
}

static void test_player(void)
{
    sq_raw_t t[2] = {{0, 0x90, 60, 100, 0}, {10, 0x80, 60, 0, 0}};
    uint32_t c0;
    sq_clear();
    sqp_init(FS);
    hr_set_length(0, 1, scratch);         /* pattern 0: one beat, the kick on 1, the hat on the "and" */
    hr_add(0, 0, 0, 100, scratch);
    hr_add(0, 48, 2, 90, scratch);
    hr_set_length(1, 2, scratch);         /* pattern 1: two beats, the snare on 2 */
    hr_add(1, 96, 1, 110, scratch);
    hrp.pattern = 0;

    /* alone (PLAY on its page): its pattern over and over; the one queued at its end */
    n_hit = 0;
    sqp_aux_play(0);
    c0 = sqp.clock;
    ticks(200);
    CHECK(played(0, c0, "0:0 48:2 96:0 144:2 192:0"), "alone: one beat over and over");
    hrp.next = 1;
    ticks(88);                            /* to 288: the end of the beat that started at 192 */
    CHECK(hrp.pattern == 1u && hrp.next == 0xFFu, "the next one at the end: pattern %u", hrp.pattern);
    ticks(192);
    CHECK(played(5, c0, "240:2 384:1"), "then pattern 1's snare on its second beat");
    sqp_stop();
    CHECK(!sqp.aux && sqp_frames_to_tick() == 0xFFFFFFFFu, "stopped: no clock");

    /* with the sequencer's part: its own loop beside the part's (3 beats against 2) */
    sq_commit(0, 0, t, 2, 3, scratch);
    sqp_select_part(0);
    hrp.pattern = 1;
    n_hit = 0;
    sqp_play();
    ticks(3 * 96 * 2);
    CHECK(played(0, 0, "96:1 288:1 480:1"), "with a part of 3 beats: every 2 beats");
    sqp_stop();
    hrp.mute = 1;
    n_hit = 0;
    sqp_play();
    ticks(400);
    CHECK(n_hit == 0u, "muted: %u hits", n_hit);
    sqp_stop();
    hrp.mute = 0;

    /* a song: each step's pattern from the step's start, cut at its end; none: quiet */
    sq_song_set(0, 0, 0, 0xFF, scratch);  /* part 0 (3 beats), pattern 1 */
    sq_song_set_drums(0, 0, 1);
    sq_song_set(0, 1, 0, 0xFF, scratch);  /* (takes the drums of the step before) */
    sq_song_set(0, 2, 0, 0xFF, scratch);
    sq_song_set_drums(0, 2, SQ_NO_DRUMS);
    sq_song_set(0, 3, 0, 0xFF, scratch);
    sq_song_set_drums(0, 3, 0);
    CHECK(sq_song_drums(0, 1) == 1u && sq_song_drums(0, 2) == SQ_NO_DRUMS, "a new step: the drums before it, %u",
          sq_song_drums(0, 1));
    sqp.song_mode = 1;
    sqp_select_song(0);
    n_hit = 0;
    sqp_play();
    ticks(4 * 288);
    CHECK(played(0, 0, "96:1 384:1 864:0 912:2 960:0 1008:2 1056:0 1104:2"),
          "a song: pattern 1 from each step's start (its beat 3 cut), none, then pattern 0");
    CHECK(sqp.state == SQP_STOPPED, "the song's end: stopped");
    sqp.song_mode = 0;
}

static void test_record(void)
{
    uint32_t c0;
    sq_clear();
    sqp_init(FS);
    hrp.pattern = 4;
    hrp.quant = 3;                        /* 1/16 */
    hrp.swing = 50;
    hrp.rec = 1;
    n_hit = 0;
    CHECK(!hrp_recording(), "not running: not recording");
    sqp_aux_play(0);
    hrp_record(0, 100);                   /* before the first tick: on the one, heard now (not again then) */
    c0 = sqp.clock;
    ticks(21);                            /* just after the clock 20 */
    take_hits();
    hrp_record(2, 90);                    /* to 24, ahead: heard now, skipped when it gets there */
    take_hits();
    ticks(30);                            /* to 51 */
    hrp_record(1, 80);                    /* late for 48: there next time */
    take_hits();
    CHECK(hr_hits(4) == 3u && hr_hit_tick(hr_hit_at(4, 0)) == 0u && hr_hit_tick(hr_hit_at(4, 1)) == 24u &&
              hr_hit_tick(hr_hit_at(4, 2)) == 48u,
          "three hits, at %u %u %u", hr_hit_tick(hr_hit_at(4, 0)), hr_hit_tick(hr_hit_at(4, 1)),
          hr_hit_tick(hr_hit_at(4, 2)));
    CHECK(n_hit == 0u, "the first time round: none played again (%u)", n_hit);
    ticks(768 - 51);                      /* the 8 beats of an empty pattern: round to its start */
    ticks(60);
    CHECK(played(0, c0, "768:0 792:2 816:1"), "the second time round, all three");
    /* SEL + a pad: erased as it comes by */
    hrp.erase = 1u << 2;
    ticks(768);
    hrp.erase = 0;
    take_hits();
    CHECK(hr_hits(4) == 2u, "the hat held to erase: %u hits left", hr_hits(4));
    hrp.next = 7;
    ticks(800);
    CHECK(hrp.pattern == 4u, "recording: no other pattern at the end (%u)", hrp.pattern);
    sqp_stop();
    hrp.rec = 0;
    hrp.next = 0xFF;
    sqp.song_mode = 1;
    hrp.rec = 1;
    CHECK(!hrp_recording(), "a song playing: not recording");
    sqp.song_mode = 0;
    hrp.rec = 0;
}

/* STEP EDIT's places: the grid's steps and every hit, round at the ends; the hits at one; a hit moved */
static void test_step(void)
{
    uint32_t first, n;
    hr_rec_t r;
    sq_clear();
    hr_set_length(2, 2, scratch);         /* 192 clocks: hits at 0 (pads 1, 3), 30 (pad 2: off the grid), 96 */
    hr_add(2, 0, 0, 100, scratch);
    hr_add(2, 0, 2, 90, scratch);
    hr_add(2, 30, 1, 80, scratch);
    hr_add(2, 96, 0, 70, scratch);
    CHECK(hr_step_next(2, 0, 24, 50) == 24u && hr_step_next(2, 24, 24, 50) == 30u &&
              hr_step_next(2, 30, 24, 50) == 48u && hr_step_next(2, 168, 24, 50) == 0u,
          "on: 0 -> %u -> %u -> %u, 168 -> %u", hr_step_next(2, 0, 24, 50), hr_step_next(2, 24, 24, 50),
          hr_step_next(2, 30, 24, 50), hr_step_next(2, 168, 24, 50));
    CHECK(hr_step_prev(2, 0, 24, 50) == 168u && hr_step_prev(2, 48, 24, 50) == 30u &&
              hr_step_prev(2, 30, 24, 50) == 24u && hr_step_prev(2, 97, 1, 50) == 96u,
          "back: 0 -> %u, 48 -> %u, 30 -> %u", hr_step_prev(2, 0, 24, 50), hr_step_prev(2, 48, 24, 50),
          hr_step_prev(2, 30, 24, 50));
    CHECK(hr_step_next(2, 0, 24, 66) == 30u && hr_step_next(2, 30, 24, 66) == 32u &&
              hr_step_prev(2, 48, 24, 66) == 32u && hr_step_next(2, 33, 24, 66) == 48u,
          "swung 66%%: 0 -> %u, 30 -> %u", hr_step_next(2, 0, 24, 66), hr_step_next(2, 30, 24, 66));
    {                                     /* the whole pattern at 1/8: 4 steps, and the hit at 30 */
        uint32_t at = 0, stops = 0;
        do {
            at = hr_step_next(2, at, 48, 50);
            stops++;
        } while (at && stops < 20u);
        CHECK(stops == 5u, "1/8 round the pattern: %u places (0 48 96 144, and the hit at 30)", stops);
    }
    n = hr_hits_at(2, 0, &first);
    CHECK(n == 2u && first == 0u && hr_hits_at(2, 30, &first) == 1u && first == 2u && hr_hits_at(2, 24, &first) == 0u,
          "the hits at 0: %u, at 30: one, the third", n);
    hr_move(2, 30, 1, 24, 1, scratch);
    CHECK(hr_hits_at(2, 30, &first) == 0u && hr_hits_at(2, 24, &first) == 1u && hr_hit_at(2, first)[3] == 80u,
          "moved to 24, its velocity kept");
    hr_move(2, 24, 1, 24, 3, scratch);
    CHECK(hr_hits_at(2, 24, &first) == 1u && hr_hit_at(2, first)[2] == 3u, "to another pad");
    hr_move(2, 24, 3, 0, 0, scratch);     /* onto pad 1's hit at 0: replaces it */
    CHECK(hr_hits(2) == 3u && hr_hits_at(2, 0, &first) == 2u && hr_hit_at(2, first)[3] == 80u,
          "onto one there: replaced, %u hits", hr_hits(2));
    CHECK(sq_valid(), "the memory holds together");
    while (hrp_take(&r))
        ;
    hrp_step_in(2, 90);
    CHECK(hrp_take(&r) && r.kind == HR_REC_STEP && r.pad == 2u && r.vel == 90u, "a pad in STEP EDIT: for the place");
}

/* ------------------------------------------------------------------------------------------------ voices --- */
static uint8_t snd[S7_PACKED(4000)];
static const hr_kit_sound_t KIT[3] = {
    {snd, 4000, 69, 0, "TONE"}, {snd, 4000, 69, 0, "HATS"}, {snd, 4000, 69, 99, "VEL"}};
static const hr_kit_pad_t KIT_PAD[4] = {
    {"KICK", {0, 0, 99, 0, 36, 0, 1, 0}},
    {"CLHAT", {1, 0, 99, -3, 42, 1, 1, 0}},
    {"OPHAT", {1, 0, 99, 3, 46, 1, 1, 0}},
    {"CRASH", {0, -3, 99, 0, 49, 0, 2, 0}},
};
static float bl[8192], br[8192];

static void test_voices(void)
{
    uint32_t i;
    float l, r;
    for (i = 0; i < 4000u; i++)           /* a sine, 26400 Hz / 66 = 400 Hz */
        s7_put_w12(snd, i, (uint32_t)(2048.0f + 1500.0f * sinf(2.0f * 3.14159265f * (float)i / 66.0f)));
    hr_kit_use(KIT, 3, KIT_PAD, 4);
    s7_init(FS);
    for (i = 0; i < HR_PADS; i++)
        hr_pad_default(i);
    CHECK(hr_pad[1].group == 1u && hr_pad[3].poly == 2u && hr_pad[4].sound == HR_NO_SOUND && hr_pad[4].note == 38u &&
              hr_pad[5].note == 51u,
          "the kit's pads, then the rest on General MIDI's notes free: pad 5 note %u", hr_pad[4].note);
    {
        char b[8];
        hr_sound_name(3, b);
        CHECK(!strcmp(b, "SLOT01") && hr_sounds() == 19u && !strcmp(hr_pad_name(6), "PAD 07"), "names: %s", b);
    }
    hr_hit(2, 127);                       /* the open hat, then the closed: one voice left playing */
    hr_hit(1, 127);
    hr_render(bl, br, 2048);
    CHECK(hr_voices_active() == 1u, "a closed hat cuts the open one: %u playing", hr_voices_active());
    hr_kill();
    hr_hit(0, 100);
    hr_hit(0, 100);
    hr_render(bl, br, 2048);
    CHECK(hr_voices_active() == 1u, "a pad again: its last cut (%u)", hr_voices_active());
    hr_kill();
    hr_hit(3, 100);
    hr_hit(3, 100);
    hr_hit(3, 100);
    hr_render(bl, br, 2048);
    CHECK(hr_voices_active() == 2u, "the crash: two at once (%u)", hr_voices_active());
    hr_kill();
    /* the pan: hard left, hard right */
    memset(bl, 0, sizeof bl);
    memset(br, 0, sizeof br);
    hr_hit(1, 127);
    hr_render(bl, br, 1000);
    for (i = 0, l = r = 0.0f; i < 1000u; i++)
        l += fabsf(bl[i]), r += fabsf(br[i]);
    CHECK(l > 1000.0f && r == 0.0f, "pan -3: left only (%.0f, %.0f)", l, r);
    hr_kill();
    /* the pitch: -3 semitones (the crash pad's) plays the sine 3 semitones down: its zero crossings */
    memset(bl, 0, sizeof bl);
    hr_hit(3, 127);
    hr_render(bl, br, 8192);
    {
        uint32_t z = 0;
        float want, got;
        for (i = 1; i < 7000u; i++)
            z += (bl[i - 1] < 0.0f) != (bl[i] < 0.0f);
        want = 400.0f * powf(2.0f, -3.0f / 12.0f);
        got = (float)z / 2.0f / (7000.0f / FS);
        CHECK(fabsf(got / want - 1.0f) < 0.02f, "tuned -3: %.1f Hz, wanted %.1f", got, want);
    }
    hr_kill();
    /* velocity: the kit's amount 0 (none), 99 (all of it) */
    {
        float a[2], b2[2];
        uint32_t s, v;
        for (s = 0; s < 2u; s++)
            for (v = 0; v < 2u; v++) {
                hr_pad[0].sound = (uint8_t)(s ? 2 : 0);
                memset(bl, 0, sizeof bl);
                hr_hit(0, v ? 127u : 20u);
                hr_render(bl, br, 1000);
                hr_kill();
                for (i = 0, l = 0.0f; i < 1000u; i++)
                    l = fmaxf(l, fabsf(bl[i]));
                (s ? b2 : a)[v] = l;
            }
        CHECK(fabsf(a[0] - a[1]) < 1.0f && b2[1] > 10.0f * b2[0], "velocity: none %.0f %.0f, all %.0f %.0f", a[0],
              a[1], b2[0], b2[1]);
        hr_pad[0].sound = 0;
    }
    /* the notes: a note on the drum channel plays its pad */
    hr_note(49, 100);
    CHECK(hr_voices_active() == 1u && hr_pad_of(49) == 3 && hr_pad_of(99) == -1, "note 49: the crash");
    hr_kill();
    /* a slot as a pad's sound, renumbered when the kit grows: SLOT01 of a kit of 3, then of 5 */
    hr_pad[7].sound = 3;
    hr_kit_use(KIT, 2, KIT_PAD, 4);
    hr_pads_rekit(3);
    CHECK(hr_pad[7].sound == 2u, "the kit one shorter: SLOT01 is sound %u", hr_pad[7].sound);
}

int main(void)
{
    test_patterns();
    test_player();
    test_record();
    test_step();
    test_voices();
    printf("hr16: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
