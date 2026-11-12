/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the HR-16: its patterns (hits in, out, a pad's out, the length, COPY), quantize with swing, the player (alone,
 * with the sequencer's part, a song's steps), recording as it loops, the pads' voices (the hats' group, a pad's poly,
 * the pan), and a pattern's own sounds. Built and run by test/run.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hr.h"
#include "player.h"
#include "s7bank.h"

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

/* OFFSET: a pad's hits, or all, moved by clocks, round at the ends; FILL: a pad held repeating on the grid */
static void test_offset_fill(void)
{
    uint32_t c0, i;
    sq_clear();
    hr_set_length(3, 2, scratch);         /* 192 clocks: the kick at 0 and 180, the snare at 96 */
    hr_add(3, 0, 0, 100, scratch);
    hr_add(3, 96, 1, 90, scratch);
    hr_add(3, 180, 0, 80, scratch);
    hr_offset(3, 0, 20, scratch);         /* the kick 20 later: 180 round to 8 */
    CHECK(hr_hits(3) == 3u && hr_hit_tick(hr_hit_at(3, 0)) == 8u && hr_hit_at(3, 0)[3] == 80u &&
              hr_hit_tick(hr_hit_at(3, 1)) == 20u && hr_hit_tick(hr_hit_at(3, 2)) == 96u && hr_hit_at(3, 2)[2] == 1u,
          "the kick +20: at %u %u, the snare at %u", hr_hit_tick(hr_hit_at(3, 0)), hr_hit_tick(hr_hit_at(3, 1)),
          hr_hit_tick(hr_hit_at(3, 2)));
    hr_offset(3, HR_PADS, -10, scratch);  /* all 10 earlier: 8 round to 190 */
    CHECK(hr_hit_tick(hr_hit_at(3, 0)) == 10u && hr_hit_tick(hr_hit_at(3, 1)) == 86u &&
              hr_hit_tick(hr_hit_at(3, 2)) == 190u && hr_hit_at(3, 2)[3] == 80u,
          "all -10: at %u %u %u", hr_hit_tick(hr_hit_at(3, 0)), hr_hit_tick(hr_hit_at(3, 1)),
          hr_hit_tick(hr_hit_at(3, 2)));
    hr_offset(3, 5, 30, scratch);         /* a pad without hits: nothing moves */
    CHECK(hr_hits(3) == 3u && hr_hit_tick(hr_hit_at(3, 0)) == 10u && sq_valid(), "a pad without hits: as it was");

    /* FILL: the hat held from clock 29 (the 1/16 at 24 its own hit's): repeats at 48, 72, 96, 120; recorded */
    sq_clear();
    sqp_init(FS);
    hrp.pattern = 6;
    hrp.quant = 3;
    hrp.swing = 50;
    hrp.rec = 1;
    n_hit = 0;
    sqp_aux_play(0);
    c0 = sqp.clock;
    ticks(30);
    hrp.fill = 1;
    hrp_fill_pad(2, 77, 1);
    ticks(100);
    CHECK(played(0, c0, "48:2 72:2 96:2 120:2"), "FILL: on the 1/16s after its own");
    for (i = 0; i < n_hit; i++)
        if (hit[i].vel != 77u)
            break;
    CHECK(i == n_hit, "at its velocity");
    hrp_fill_pad(2, 0, 0);                /* let go: no more */
    ticks(100);
    CHECK(n_hit == 4u, "let go: %u", n_hit);
    take_hits();
    CHECK(hr_hits(6) == 4u && hr_hit_tick(hr_hit_at(6, 0)) == 48u, "recorded: %u hits", hr_hits(6));
    hrp_fill_pad(1, 90, 1);               /* FILL let go (the button): none */
    hrp.fill = 0;
    ticks(100);
    CHECK(n_hit == 4u, "FILL let go: %u", n_hit);
    sqp_stop();
    hrp.rec = 0;
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
    /* a slot as a pad's sound in the global set-up, renumbered when the kit changes: SLOT01 of a kit of 3, then of 2 */
    hr_std[7].sound = 3;
    hr_kit_use(KIT, 2, KIT_PAD, 4);
    hr_pads_rekit(3);
    CHECK(hr_std[7].sound == 2u, "the kit one shorter: SLOT01 is sound %u", hr_std[7].sound);
}

/* a pattern's own sounds: stored, edited (the pads' until stored), taken as the pattern changes, copied, let go */
static void same_as_std(int *ok)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++)
        *ok &= hr_pad[i].sound == hr_std[i].sound && hr_pad[i].tune == hr_std[i].tune &&
               hr_pad[i].level == hr_std[i].level && hr_pad[i].pan == hr_std[i].pan;
}

static void test_sounds(void)
{
    uint32_t i;
    int ok = 1;
    sq_clear();
    sqp_init(FS);
    hr_kit_use(KIT, 3, KIT_PAD, 4);
    for (i = 0; i < HR_PADS; i++) {
        hr_pad_default(i);
        hr_std[i] = hr_pad[i];
    }
    hr_sounds_apply(0);
    CHECK(hr_snd.p == 0u && !hr_snd.own && !hr_sounds_own(0), "pattern 0: the global set-up's");
    /* an edit of the global set-up's: the set-up's too */
    hr_pad[1].tune = 5;
    hr_pad_changed(1);
    CHECK(hr_std[1].tune == 5 && !hr_snd.edited, "a global edit: in the set-up (%d)", hr_std[1].tune);

    /* kept with pattern 2: an edit its own, until stored; another pattern taken, gone */
    hr_sounds_apply(2);
    hr_pad[0].sound = 3 + 1;              /* SLOT02 */
    hr_pad[0].level = 40;
    hr_pad[0].pan = -2;
    hr_pad_changed(0);                    /* (still the global set-up's: it takes them) */
    CHECK(hr_sounds_store(2) == 0 && hr_sounds_own(2) && hr_snd.own && hr_snd.p == 2u && !hr_snd.edited &&
              sq_len(SQ_PSND(2)) == SQ_SOUNDS_LEN,
          "stored with pattern 2");
    hr_std[0].sound = 0;                  /* (the set-up as it was) */
    hr_std[0].level = 99;
    hr_std[0].pan = 0;
    hr_pad[2].level = 10;
    hr_pad_changed(2);
    CHECK(hr_snd.edited && hr_std[2].level == 99u, "its own edited: not the set-up's (%u)", hr_std[2].level);
    hr_pad[2].level = 99;
    hr_pad_changed(2);
    CHECK(!hr_snd.edited, "edited back: as stored");
    hr_pad[2].level = 10;
    hr_pad_changed(2);
    hr_sounds_apply(3);
    same_as_std(&ok);
    CHECK(ok && !hr_snd.own && hr_snd.p == 3u, "pattern 3: the set-up's");
    hr_sounds_apply(2);
    CHECK(hr_snd.own && !hr_snd.edited && hr_pad[2].level == 99u && hr_pad[0].sound == 4u && hr_pad[0].level == 40u &&
              hr_pad[0].pan == -2 && hr_pad[1].tune == 5,
          "pattern 2 again: as stored, the edit gone (%u)", hr_pad[2].level);

    /* the player: the pads take the sounds of the pattern as it changes */
    hr_set_length(2, 1, scratch);
    hr_set_length(3, 1, scratch);
    hrp.pattern = 3;
    hr_sounds_apply(3);
    sqp_aux_play(0);
    ticks(2);
    CHECK(!hr_snd.own && hr_snd.p == 3u, "playing pattern 3");
    hrp.next = 2;
    ticks(96);
    CHECK(hrp.pattern == 2u && hr_snd.own && hr_snd.p == 2u && hr_pad[0].sound == 4u,
          "pattern 2 at 3's end: its sounds (pad 1: %u)", hr_pad[0].sound);
    sqp_stop();

    /* the kit grown: a slot stays the slot, a kit sound its number */
    hr_kit_use(KIT, 2, KIT_PAD, 4);
    hr_sounds_apply(2);
    CHECK(hr_pad[0].sound == 3u && hr_pad[1].sound == 1u, "a kit of 2: SLOT02 is %u", hr_pad[0].sound);
    hr_kit_use(KIT, 1, KIT_PAD, 4);
    hr_sounds_apply(2);
    CHECK(hr_pad[0].sound == 2u && hr_pad[1].sound == HR_NO_SOUND, "a kit of 1: the snare gone (%u)",
          hr_pad[1].sound);
    hr_kit_use(KIT, 3, KIT_PAD, 4);

    /* COPY: to a pattern without hits, its sounds too; to one with, that one's kept */
    hr_add(2, 0, 0, 100, scratch);
    CHECK(hr_copy(2, 5, scratch) == 0 && hr_sounds_own(5) &&
              !memcmp(sq_data(SQ_PSND(5)), sq_data(SQ_PSND(2)), SQ_SOUNDS_LEN),
          "copied to an empty pattern: its sounds too");
    hr_add(6, 0, 1, 100, scratch);
    CHECK(hr_copy(2, 6, scratch) == 0 && !hr_sounds_own(6) && hr_hits(6) == 2u, "appended: 6's (none) kept");
    CHECK(hr_copy(3, 5, scratch) == 0 && hr_sounds_own(5), "appended to 5: its own kept");
    hr_erase(5, HR_PADS, scratch);
    CHECK(hr_sounds_own(5), "erased: its sounds kept");
    CHECK(hr_copy(3, 5, scratch) == 0 && !hr_sounds_own(5), "3 copied to 5 (erased): 3's, none");

    /* let go: the set-up's again */
    hr_sounds_apply(2);
    CHECK(hr_sounds_drop(2) == 0 && !hr_sounds_own(2) && !hr_snd.own && sq_len(SQ_PSND(2)) == 0u, "2's let go");
    ok = 1;
    same_as_std(&ok);
    CHECK(ok, "the pads: the set-up's");
    CHECK(sq_valid(), "the memory holds together");
}

/* ---------------------------------------------------------------------------------------- a bank's kit --- */
static uint32_t bank_mem[2048];

static void test_bank_kit(void)
{
    uint8_t *m = (uint8_t *)bank_mem;
    s7_bank_sound_t *ks = (s7_bank_sound_t *)(void *)m;
    s7_bank_pad_t *kp = (s7_bank_pad_t *)(void *)(m + 2u * sizeof *ks);
    uint32_t at = (uint32_t)(2u * sizeof *ks + 2u * sizeof *kp), size, i;
    s7_bank_foot_t *f;
    s7_bank_t b;
    char nm[HR_NAME + 1];
    CHECK(hr_kit_tag() == HR_KIT_BUILT_IN && hr_kit_same(0) && hr_kit_same(HR_KIT_BUILT_IN) && !hr_kit_same(77),
          "the kit built in: saved with it (or before the tag), the same");
    hr_kit_bank(0);
    CHECK(hr_kit_tag() == HR_KIT_BUILT_IN, "no bank: the kit built in stays");
    memset(bank_mem, 0, sizeof bank_mem);
    ks[0].words = 100, ks[0].rate = 60, ks[0].vel = 30, ks[0].at = at;
    strcpy(ks[0].name, "BOOM");
    at += (S7_PACKED(100u) + 3u) & ~3u;
    ks[1].words = 60, ks[1].rate = 76, ks[1].vel = 99, ks[1].at = at;
    strcpy(ks[1].name, "TSS");
    at += (S7_PACKED(60u) + 3u) & ~3u;
    strcpy(kp[0].name, "BIG");
    kp[0].sound = 0, kp[0].tune = -5, kp[0].level = 70, kp[0].pan = 2, kp[0].note = 35, kp[0].poly = 1;
    kp[1].sound = 1 + 2, kp[1].level = 99, kp[1].note = 44, kp[1].group = 1, kp[1].poly = 2;   /* slot 2 */
    size = at;
    f = (s7_bank_foot_t *)(void *)(m + size);
    f->magic = S7B_MAGIC, f->version = S7B_VERSION, f->count = 0, f->size = size;
    f->kit_sounds = 2, f->kit_pads = 2;
    f->crc = s7bank_crc32(m, size);
    f->fcrc = s7bank_crc32(f, 28u);
    CHECK(s7bank_parse(m + size + S7B_FOOT, size + S7B_FOOT, &b) == 0 && b.kit_sounds == 2u, "a bank with a kit");
    hr_kit_bank(&b);
    CHECK(hr_kit_sounds() == 2u && hr_sounds() == 2u + S7_SLOTS, "its two sounds, then the slots");
    hr_sound_name(1, nm);
    CHECK(!strcmp(nm, "TSS"), "a sound's name: %s", nm);
    hr_sound_name(2 + 2, nm);
    CHECK(!strcmp(nm, "SLOT03"), "after them the slots: %s", nm);
    CHECK(!strcmp(hr_pad_name(0), "BIG") && !strcmp(hr_pad_name(1), "PAD 02") && !strcmp(hr_pad_name(2), "PAD 03"),
          "pad names: the kit's, else PAD nn");
    for (i = 0; i < HR_PADS; i++)
        hr_pad_default(i);
    CHECK(hr_pad[0].sound == 0u && hr_pad[0].tune == -5 && hr_pad[0].level == 70u && hr_pad[0].pan == 2 &&
              hr_pad[0].note == 35u && hr_pad[1].sound == 3u && hr_pad[1].group == 1u && hr_pad[1].poly == 2u &&
              hr_pad[2].sound == HR_NO_SOUND,
          "the pads as the bank's kit sets them up, the rest none");
    CHECK(hr_pad[2].note == 36u, "a pad beyond the kit's: the first General MIDI note left (%u)", hr_pad[2].note);
    CHECK(hr_kit_tag() != HR_KIT_BUILT_IN && hr_kit_tag() != 0u &&
              hr_kit_tag() == ((b.kit_id ^ b.kit_id >> 16) & 0xFFFFu),
          "its tag: its CRC folded");
    CHECK(hr_kit_same(hr_kit_tag()) && !hr_kit_same(0) && !hr_kit_same(HR_KIT_BUILT_IN),
          "pads saved with the kit built in (or before the tag): not this kit's");
    hr_kit_use(KIT, 3, KIT_PAD, 4);   /* (the others' kit again) */
}

/* a program change on the drums' channel (the app's switch on): stopped, the pattern at once (100-127: 00-27);
 * playing, queued for the pattern's end, or at once as it starts over; not while recording, nor in a song */
static void test_program(void)
{
    sq_clear();
    sqp_init(FS);
    hrp.pattern = 0;
    hrp.next = 0xFF;
    hrp.rec = 0;
    hrp_program(5);
    CHECK(hrp.pattern == 5u && hrp.next == 0xFFu, "stopped: 05 at once (%u, %u)", hrp.pattern, hrp.next);
    hrp_program(112);
    CHECK(hrp.pattern == 12u, "program 112: pattern 12 (%u)", hrp.pattern);
    hrp_program(3);
    sqp_aux_play(0);              /* the drums alone, pattern 03: 8 beats */
    hrp_program(7);
    CHECK(hrp.pattern == 7u && hrp.next == 0xFFu, "from the top, before its first clock: 07 at once (%u, %u)",
          hrp.pattern, hrp.next);
    ticks(100);
    hrp_program(9);
    CHECK(hrp.pattern == 7u && hrp.next == 9u, "playing: 09 queued (%u, %u)", hrp.pattern, hrp.next);
    hrp_program(7);
    CHECK(hrp.next == 0xFFu, "the one playing again: the queue cleared (%u)", hrp.next);
    hrp_program(9);
    ticks(8u * SQ_PPQN - 101u);   /* to its last clock */
    CHECK(hrp.pattern == 7u, "its last clock: still 07 (%u)", hrp.pattern);
    ticks(1);
    CHECK(hrp.pattern == 9u && hrp.next == 0xFFu, "its end: 09 (%u, %u)", hrp.pattern, hrp.next);
    ticks(8u * SQ_PPQN);          /* round once: at its first clock again, before it plays */
    CHECK(hrp.tick == 0u, "at its start (%u)", hrp.tick);
    hrp_program(4);
    CHECK(hrp.pattern == 4u && hrp.next == 0xFFu, "as it starts over: 04 at once (%u, %u)", hrp.pattern, hrp.next);
    ticks(10);
    hrp.rec = 1;
    hrp_program(6);
    CHECK(hrp.pattern == 4u && hrp.next == 0xFFu, "recording: not taken (%u, %u)", hrp.pattern, hrp.next);
    hrp.rec = 0;
    sqp_stop();
    sqp.song_mode = 1;
    sqp.state = SQP_PLAYING;      /* (a song playing) */
    hrp_program(6);
    CHECK(hrp.pattern == 4u && hrp.next == 0xFFu, "a song playing: not taken (%u, %u)", hrp.pattern, hrp.next);
    sqp.state = SQP_STOPPED;
    sqp.song_mode = 0;
    hrp.pattern = 0;
    /* the sequencer's part located: its own loop moved as far, round its ends (8 beats: 768 clocks) */
    hrp.tick = 100;
    hrp_shift(-200);
    CHECK(hrp.tick == 668u, "moved back round its start: %u", hrp.tick);
    hrp_shift(200);
    CHECK(hrp.tick == 100u, "and on again: %u", hrp.tick);
}

/* COPY a pad's hits onto another pad (the HR-16's): merged, the destination's length kept, a hit there taking the
 * copied velocity; within one pattern too; none to copy: said */
static void test_copy_pad(void)
{
    sq_clear();
    hr_add(1, 0, 0, 100, scratch);
    hr_add(1, 96, 0, 90, scratch);
    hr_add(1, 200, 0, 80, scratch);   /* (past pattern 2's end) */
    hr_add(1, 96, 2, 70, scratch);
    sq.pattern[1].beats = 4;
    hr_add(2, 96, 3, 50, scratch);    /* pattern 2, 2 beats: pad 4 at 96, pad 6 at 96 */
    hr_add(2, 96, 5, 60, scratch);
    sq.pattern[2].beats = 2;
    CHECK(hr_copy_pad(1, 0, 2, 3, scratch) == 0 && hr_hits(2) == 3u && hr_beats(2) == 2u,
          "pad 1 of 01 onto pad 4 of 02: %u hits, %u beats", hr_hits(2), hr_beats(2));
    CHECK(hr_hit_tick(hr_hit_at(2, 0)) == 0u && hr_hit_at(2, 0)[2] == 3u && hr_hit_at(2, 0)[3] == 100u &&
              hr_hit_tick(hr_hit_at(2, 1)) == 96u && hr_hit_at(2, 1)[2] == 3u && hr_hit_at(2, 1)[3] == 90u &&
              hr_hit_at(2, 2)[2] == 5u,
          "in order: 0 pad 4 (100), 96 pad 4 (90, its own replaced), 96 pad 6");
    CHECK(hr_copy_pad(1, 0, 1, 1, scratch) == 0 && hr_hits(1) == 7u && hr_hit_at(1, 1)[2] == 1u,
          "within 01, pad 1 onto pad 2: %u hits", hr_hits(1));
    CHECK(hr_copy_pad(1, 9, 2, 3, scratch) == SQ_E_NONE, "a pad without hits: nothing to copy");
    CHECK(hr_copy_pad(1, 0, 7, 0, scratch) == 0 && hr_beats(7) == 8u && hr_hits(7) == 3u,
          "onto an empty pattern: its 8 beats now its own, %u hits", hr_hits(7));
}

int main(void)
{
    test_patterns();
    test_player();
    test_record();
    test_step();
    test_offset_fill();
    test_program();
    test_copy_pad();
    test_voices();
    test_sounds();
    test_bank_kit();
    printf("hr16: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
