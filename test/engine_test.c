/* SM-87: the sampler engine against the X7000's measurements (documentation/akai-s700-x7000/04-playback.md) and
 * Axs7's engine tests (engine/src/voice.rs). Built and run by test/run.sh, on the host. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "s7.h"

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

/* ---------------------------------------------------------------------------------------- packed words --- */
static void test_packing(void)
{
    static uint8_t d[S7_PACKED(4096)];
    uint32_t i, bad = 0;
    memset(d, 0, sizeof d);
    for (i = 0; i < 4096u; i++)
        s7_put_w12(d, i, (i * 2654435761u) >> 20);
    for (i = 0; i < 4096u; i++)
        bad += s7_w12(d, i) != (int32_t)((i * 2654435761u) >> 20) - 2048;
    CHECK(bad == 0, "packed words: %u wrong", bad);
    /* the layout of documentation/akai-s700-x7000/03-samples-and-programs.md */
    memset(d, 0, 4);
    s7_put_w12(d, 0, 0xABC);
    s7_put_w12(d, 1, 0x123);
    CHECK(d[0] == 0xBC && d[1] == 0x3A && d[2] == 0x12, "layout: %02X %02X %02X", d[0], d[1], d[2]);
}

/* -------------------------------------------------------------------------- index mapping (voice.rs tests) --- */
static void indices(uint32_t mode, uint32_t len, uint32_t ls, uint32_t le, int32_t from, int32_t to, const int32_t *want,
                    const char *what)
{
    s7_loop_t l = {mode, len, ls, le};
    int32_t v;
    for (v = from; v < to; v++) {
        int32_t got = s7_index(&l, v);
        CHECK(got == want[v - from], "%s: position %d reads %d, not %d", what, v, got, want[v - from]);
    }
}

static void test_index(void)
{
    static const int32_t fwd[] = {2, 3, 4, 5, 6, 4, 5, 6, 4, 5};
    static const int32_t alt[] = {5, 6, 7, 6, 5, 4, 5, 6, 7, 6, 5};
    static const int32_t one[] = {0, 0, 1, 2, 3, 4, -1, -1};
    indices(S7_LOOP, 10, 4, 7, 2, 12, fwd, "forward loop wraps to its start");
    indices(S7_ALT, 10, 4, 8, 5, 16, alt, "alternating turns at both ends");
    indices(S7_ONESHOT, 5, 0, 5, -1, 7, one, "one shot ends");
    indices(S7_DRUM, 5, 0, 5, -1, 7, one, "drum trigger ends");
}

/* ------------------------------------------------------------------------------------ the formulas --- */
static void test_tables(void)
{
    /* dB at MIDI velocities 1, 64, 127 with VEL>LEVEL 0, 50, 99, LEVEL 99 (measured, tools/x7000-lab vellevel) */
    static const struct {
        uint32_t amount;
        float db[3];
    } vl[] = {{0, {-7.7f, -7.7f, -7.7f}}, {50, {-24.6f, -14.2f, -4.0f}}, {99, {-40.3f, -20.1f, 0.0f}}};
    static const uint32_t vels[3] = {1, 64, 127};
    /* T60 by the formula's column of 04-playback.md's table */
    static const struct {
        uint32_t r;
        float t60;
    } rel[] = {{0, 0.018f}, {10, 0.036f}, {20, 0.071f}, {30, 0.143f}, {40, 0.285f}, {50, 0.570f}, {60, 1.14f},
               {70, 2.28f}, {80, 4.56f}, {83, 5.70f}, {87, 7.60f}, {90, 11.4f}, {99, 11.4f}};
    static const struct {
        uint32_t f;
        float hz;
    } cut[] = {{0, 306}, {30, 1000}, {60, 3400}, {80, 7600}, {90, 11300}, {99, 16200}};
    uint32_t i, j;
    for (i = 0; i < 3u; i++)
        for (j = 0; j < 3u; j++) {
            float got = s7_level_db(99, vl[i].amount, vels[j]);
            CHECK(fabsf(got - vl[i].db[j]) < 0.6f, "level: VEL>LEVEL %u velocity %u: %.2f dB, measured %.1f",
                  vl[i].amount, vels[j], got, vl[i].db[j]);
        }
    CHECK(fabsf(s7_level_db(0, 99, 127) + 34.2f) < 0.01f, "LEVEL 0 is -34.2 dB, not silent");
    for (i = 0; i < sizeof rel / sizeof rel[0]; i++) {
        float got = s7_release_t60(rel[i].r);
        CHECK(fabsf(got / rel[i].t60 - 1.0f) < 0.03f, "release %u: T60 %.3f s, formula %.3f", rel[i].r, got,
              rel[i].t60);
    }
    for (i = 0; i < sizeof cut / sizeof cut[0]; i++) {
        float got = s7_cutoff_hz(cut[i].f), semis = 12.0f * log2f(got / cut[i].hz);
        CHECK(fabsf(semis) < 0.9f, "filter %u: %.0f Hz, measured %.0f (%.2f semitones)", cut[i].f, got, cut[i].hz,
              semis);
    }
    CHECK(fabsf(s7_rate(76) - 39555.4f) < 0.5f && fabsf(s7_rate(48) - 7848.74f) < 0.05f &&
              fabsf(s7_rate(36) - 3924.38f) < 0.05f,
          "rates: %.2f %.2f %.2f", s7_rate(76), s7_rate(48), s7_rate(36));
}

/* ------------------------------------------------------------------------------------ rendering --- */
static uint8_t smp[S7_PACKED(S7_MAXWORDS)];

/* a looping sine in slot 0: k whole cycles in `words` words at rate code 76, so the loop is seamless */
static float make_sine(float hz, uint32_t words, float amp)
{
    uint32_t i, k = (uint32_t)(hz * (float)words / s7_rate(76) + 0.5f);
    memset(smp, 0, sizeof smp);
    for (i = 0; i < words; i++)
        s7_put_w12(smp, i, (uint32_t)(2048.0f + amp * sinf(2.0f * 3.14159265f * (float)k * (float)i / (float)words)));
    return (float)k * s7_rate(76) / (float)words;
}

static void slot_setup(uint32_t words)
{
    s7_slot_t *s = &s7_slot[0];
    uint32_t i;
    memset(s7_slot, 0, sizeof s7_slot);
    s7_default_params(s->p);
    s->data = smp;
    s->words = words;
    s->p[S7_P_KEYL(0)] = 0;
    s->p[S7_P_KEYH(0)] = 99;
    s->p[S7_P_RPMODE] = S7_LOOP;
    s7_set_word(s->p, S7_P_SEND, words);
    s7_set_word(s->p, S7_P_SLOOP, 0);
    s->p[S7_P_RECNP] = 60;
    s->p[S7_P_RECBW] = 76;
    s->p[S7_P_LDNSS] = 99;
    s->p[S7_P_VELLDN] = 0;
    s->p[S7_P_FLTFRQ] = 99;
    s->p[S7_P_LFDP] = 0;
    for (i = 1; i < S7_SLOTS; i++)
        s7_slot[i].data = 0;
    s7_program(0);
}

static float out[1 << 20];

static void render(uint32_t n)
{
    uint32_t i;
    memset(out, 0, n * sizeof out[0]);
    for (i = 0; i < n; i += 128u)
        s7_render(out + i, n - i < 128u ? n - i : 128u);
}

static float rms_db(const float *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return (float)(10.0 * log10(s / n + 1e-30));
}

/* the level of one frequency (Goertzel), dB: what a filter does to a tone, apart from the resampling's images */
static float tone_db(const float *x, uint32_t n, float hz)
{
    double w = 2.0 * 3.14159265358979 * hz / FS, c = 2.0 * cos(w), s0, s1 = 0, s2 = 0, p;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * cos(2.0 * 3.14159265358979 * i / (n - 1));
        s0 = x[i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return (float)(10.0 * log10(p + 1e-30));
}

/* frequency by zero crossings with interpolation */
static float freq_of(const float *x, uint32_t n)
{
    uint32_t i, first = 0, last = 0, crossings = 0;
    float f0 = 0, f1 = 0;
    for (i = 1; i < n; i++)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            float t = (float)(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (!crossings)
                f0 = t, first = i;
            f1 = t, last = i;
            crossings++;
        }
    (void)first;
    (void)last;
    return crossings > 1 ? (float)(crossings - 1) * FS / (f1 - f0) : 0.0f;
}

static void test_filter(void)
{
    static const struct {
        uint32_t f;
        float mult, want, tol;
    } pts[] = {{30, 1.0f, -3.0f, 0.8f}, {30, 2.0f, -36.0f, 2.5f}, {40, 1.0f, -3.0f, 0.8f}, {40, 4.0f, -72.0f, 4.0f}};
    uint32_t i;
    for (i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        float fc = s7_cutoff_hz(pts[i].f), hz = make_sine(fc * pts[i].mult, 32000, 1800.0f), open, shut;
        slot_setup(32000);
        s7_note_on(60, 100);
        render(8192);
        open = tone_db(out + 4096, 4096, hz);
        s7_kill();
        s7_slot[0].p[S7_P_FLTFRQ] = (uint8_t)pts[i].f;
        s7_note_on(60, 100);
        render(8192);
        shut = tone_db(out + 4096, 4096, hz);
        s7_kill();
        CHECK(fabsf(shut - open - pts[i].want) < pts[i].tol, "filter %u at %.0f Hz (fc %.0f): %.1f dB, want %.0f",
              pts[i].f, hz, fc, shut - open, pts[i].want);
    }
}

static void test_pitch(void)
{
    float hz = make_sine(1000.0f, 32000, 1800.0f), got;
    slot_setup(32000);
    s7_note_on(60, 100);              /* the root: the sample's own rate */
    render(44100);
    got = freq_of(out + 1000, 40000);
    CHECK(fabsf(got / hz - 1.0f) < 1e-3f, "root: %.2f Hz, want %.2f", got, hz);
    s7_kill();
    s7_note_on(72, 100);              /* an octave up */
    render(44100);
    got = freq_of(out + 1000, 40000);
    CHECK(fabsf(got / (2.0f * hz) - 1.0f) < 1e-3f, "octave: %.2f Hz, want %.2f", got, 2.0f * hz);
    s7_kill();
    s7_slot[0].p[S7_P_TRANSP(0)] = 0x80u | 5u;   /* constant pitch, +5 semitones */
    s7_slot[0].p[S7_P_SFNTUN] = (uint8_t)(int8_t)-8;   /* half a semitone down */
    s7_note_on(40, 100);
    render(44100);
    got = freq_of(out + 1000, 40000);
    CHECK(fabsf(12.0f * log2f(got / hz) - 4.5f) < 0.01f, "constant pitch +5, fine -8: %.3f semitones, want 4.5",
          12.0f * log2f(got / hz));
    s7_kill();
}

static void test_release(void)
{
    static const uint32_t rs[] = {30, 50, 70};
    uint32_t i;
    make_sine(1000.0f, 32000, 1800.0f);
    for (i = 0; i < 3u; i++) {
        float t60 = s7_release_t60(rs[i]), a, b, slope, got;
        uint32_t span = (uint32_t)(t60 * FS * 0.25f);
        slot_setup(32000);
        s7_slot[0].p[S7_P_RLEASE] = (uint8_t)rs[i];
        s7_note_on(60, 100);
        render(4410);
        s7_note_off(60, 64);
        render(span * 2u + 2048u);
        a = rms_db(out + 1024, 1024);
        b = rms_db(out + 1024 + span, 1024);
        slope = (a - b) / ((float)span / FS);     /* dB a second */
        got = 60.0f / slope;
        CHECK(fabsf(got / t60 - 1.0f) < 0.05f, "release %u: T60 %.3f s, want %.3f", rs[i], got, t60);
        s7_kill();
    }
    /* VEL>RELEASE follows the note off's velocity: off velocity 0 makes it longer */
    {
        float slow, fast;
        slot_setup(32000);
        s7_slot[0].p[S7_P_RLEASE] = 50;
        s7_slot[0].p[S7_P_VELRLS] = 99;
        s7_note_on(60, 100);
        render(4410);
        s7_note_off(60, 0);
        render(22050);
        slow = rms_db(out + 20000, 1024);
        s7_kill();
        s7_note_on(60, 100);
        render(4410);
        s7_note_off(60, 127);
        render(22050);
        fast = rms_db(out + 20000, 1024);
        s7_kill();
        CHECK(slow > fast + 20.0f, "VEL>RELEASE: off velocity 0 %.1f dB, 127 %.1f dB half a second later", slow, fast);
    }
    /* a drum trigger ignores key off and plays to its end */
    {
        slot_setup(32000);
        s7_slot[0].p[S7_P_RPMODE] = S7_DRUM;
        s7_slot[0].p[S7_P_RLEASE] = 0;
        s7_note_on(60, 100);
        render(4410);
        s7_note_off(60, 64);
        render(4410);
        CHECK(s7_voices_active() == 1u && rms_db(out, 4410) > -30.0f, "drum trigger: stopped at key off");
        render(44100);
        CHECK(s7_voices_active() == 0u, "drum trigger: still playing after its end");
        s7_kill();
    }
}

static void test_level(void)
{
    float a, b;
    make_sine(1000.0f, 32000, 2000.0f);
    slot_setup(32000);
    s7_slot[0].p[S7_P_VELLDN] = 99;
    s7_note_on(60, 127);
    render(8192);
    a = rms_db(out + 4096, 4096);
    s7_kill();
    s7_note_on(60, 64);
    render(8192);
    b = rms_db(out + 4096, 4096);
    s7_kill();
    CHECK(fabsf(b - a + 20.1f) < 0.6f, "VEL>LEVEL 99: velocity 64 %.1f dB under 127, measured 20.1", a - b);
}

static void test_vibrato(void)
{
    /* DEPTH 99: ±47 cents; measured as the spread of the pitch over a few cycles of the LFO */
    float hz = make_sine(2000.0f, 32000, 1800.0f), lo = 1e9f, hi = 0;
    uint32_t i, w = 2205;             /* 50 ms windows */
    slot_setup(32000);
    s7_slot[0].p[S7_P_LFDP] = 99;
    s7_slot[0].p[S7_P_LFDL] = 0;
    s7_slot[0].p[S7_P_LFSP] = 1;      /* 1.7 Hz: slow, so a 50 ms window sees one pitch */
    s7_note_on(60, 100);
    render(44100 * 3);
    for (i = 44100; i + w < 44100u * 3u; i += w / 2u) {
        float f = freq_of(out + i, w);
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }
    s7_kill();
    {
        float cents = 1200.0f * log2f(hi / lo) / 2.0f;
        CHECK(fabsf(cents - 47.1f) < 4.0f, "vibrato DEPTH 99: ±%.1f cents, measured ±47.1", cents);
    }
    (void)hz;
}

int main(void)
{
    s7_init(FS);
    test_packing();
    test_index();
    test_tables();
    test_filter();
    test_pitch();
    test_release();
    test_level();
    test_vibrato();
    printf("engine: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
