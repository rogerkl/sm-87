/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the effect (app/fx): silence stays silent, off or at 0 it adds nothing, a burst comes out as late as the
 * program itself has it at the MIDIVerb's rate (the rate conversion), a program change clears what the last left,
 * and what it costs on the host. Built and run by test/run.sh */
#define _POSIX_C_SOURCE 199309L   /* (clock_gettime under -std=c99) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../app/config.h"
#include "../app/fx/fx.h"

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

#if SM87_FX
extern void (*mv_progs[63])(int16_t, int16_t *, int16_t *);
extern int16_t mv_mem[16384];

#define FS 44117.647f
#define N 128u
static float s[N], l[N], r[N];

/* n frames of the send through it: for the first k of them v, a square wave at 245 Hz if tone, else v itself
 * (a click); 0 after. The largest output after `from` frames, and the frame where it is (*at) */
static float run(uint32_t n, float v, uint32_t k, int tone, uint32_t from, int32_t *at)
{
    uint32_t done = 0, i;
    float peak = 0.0f;
    *at = -1;
    while (done < n) {
        uint32_t m = n - done < N ? n - done : N;
        for (i = 0; i < m; i++) {
            uint32_t t = done + i;
            s[i] = t >= k ? 0.0f : !tone || t % 180u < 90u ? v : -v;
            l[i] = r[i] = 0.0f;
        }
        fx_render(s, l, r, m);
        for (i = 0; i < m; i++) {
            float a = fabsf(l[i]) > fabsf(r[i]) ? fabsf(l[i]) : fabsf(r[i]);
            if (done + i >= from && a > peak) {
                peak = a;
                *at = (int32_t)(done + i);
            }
        }
        done += m;
    }
    return peak;
}

/* program p's own: a click of b samples at its rate; the sample its output is furthest from where it idles, in a
 * second */
static int32_t own_peak(uint32_t p, int16_t v, uint32_t b)
{
    int16_t ol, orr, idle;
    uint32_t i;
    int32_t best = -1, most = 0;
    for (i = 0; i < 16384u; i++)
        mv_mem[i] = 0;
    for (i = 0; i < 50000u; i++)  /* (settled: what it idles at) */
        mv_progs[p](0, &ol, &orr);
    idle = ol;
    for (i = 0; i < (uint32_t)FX_RATE; i++) {
        int32_t a;
        mv_progs[p](i < b ? v : 0, &ol, &orr);
        a = abs(ol - idle) > abs(orr - idle) ? abs(ol - idle) : abs(orr - idle);
        if (a > most)
            most = a, best = (int32_t)i;
    }
    return best;
}

static void on(uint32_t p)
{
    int32_t f;
    fx_init(FS);
    fx_program(p);
    fx_set(99, 1);
    (void)run(4000, 0.0f, 0, 0, 0, &f);   /* its memory cleared, faded in (35 ms) */
}

static void test_silence(void)
{
    int32_t f;
    float pk;
    on(28);
    pk = run((uint32_t)FS, 0.0f, 0, 0, (uint32_t)FS / 2u, &f);
    CHECK(pk < 1e-3f, "nothing in, nothing out (the idling -1 taken away): %g", pk);
    fx_set(0, 1);
    for (uint32_t i = 0; i < N; i++)
        s[i] = 1000.0f, l[i] = r[i] = 0.0f;
    fx_render(s, l, r, N);
    CHECK(l[0] == 0.0f && l[N - 1] == 0.0f && r[N - 1] == 0.0f, "return 0: nothing added");
    fx_set(99, 0);
    fx_render(s, l, r, N);
    CHECK(l[N - 1] == 0.0f && r[N - 1] == 0.0f, "off: nothing added");
}

/* a click comes out as late as the program has it at its own rate (FX_RATE, not the FM-1's): its loudest moment,
 * in programs with one clearly so (in others echoes nearly as loud take turns as the filters have them) */
static void test_rate(void)
{
    static const uint32_t P[5] = {0, 7, 30, 50, 62};   /* 0.2 S Br, 0.6 S Br, 2.5 M Wm, Gate 100, Revs 600 */
    uint32_t i;
    for (i = 0; i < 5u; i++) {
        int32_t d = own_peak(P[i], 3000, 6), f;
        float want;
        on(P[i]);
        (void)run((uint32_t)FS, 3000.0f, 11, 0, 0, &f);
        want = (float)d * FS / FX_RATE;
        CHECK(d > 0 && f > 0 && fabsf((float)f - want) <= 8.0f,
              "program %u: loudest %d frames after the click, its own at %d samples of %.1f Hz: %.1f frames",
              P[i] + 1u, f, d, (double)FX_RATE, (double)want);
    }
}

/* a program change: faded out, the memory cleared, the new one in: nothing of the last one's tail after it */
static void test_change(void)
{
    int32_t f;
    float tail, after;
    on(48);                       /* 18 X Br: 18 seconds */
    tail = run((uint32_t)(FS * 0.5f), 3000.0f, 2000, 1, (uint32_t)(FS * 0.3f), &f);
    fx_program(50);               /* Gate 100 */
    CHECK(fx_program_now() == 50u, "the one coming in: %u", fx_program_now());
    /* (after it, nothing but the output's coupling settling: 2 at most at first, gone in a few tenths of a second) */
    after = run((uint32_t)(FS * 0.6f), 0.0f, 0, 0, (uint32_t)(FS * 0.3f), &f);
    CHECK(tail > 100.0f && after < 0.05f, "18 X Br's tail %.1f; from 0.3 s after the change %.4f", (double)tail,
          (double)after);
    fx_set(0, 0);
    fx_program(5);
    CHECK(fx_program_now() == 5u, "off: changed at once (%u)", fx_program_now());
    fx_program(63);
    CHECK(fx_program_now() == 5u, "63: no such program (%u)", fx_program_now());
    CHECK(!strcmp(fx_name(28), "2.0 M Wm") && !strcmp(fx_name(62), "Revs 600") && !strcmp(fx_name(63), ""),
          "the names: %s, %s", fx_name(28), fx_name(62));
}

/* what it costs on the host: a second of a burst and its tail, each program */
static void test_cost(void)
{
    uint32_t p;
    double worst = 0, all = 0;
    for (p = 0; p < FX_PROGS; p++) {
        int32_t f;
        struct timespec a, b;
        double t;
        on(p);
        clock_gettime(CLOCK_MONOTONIC, &a);
        (void)run((uint32_t)FS, 2000.0f, 4000, 1, 0, &f);
        clock_gettime(CLOCK_MONOTONIC, &b);
        t = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) * 1e-9;
        all += t;
        worst = t > worst ? t : worst;
    }
    printf("fx: a second of audio on the host: %.2f ms on average, %.2f ms at worst\n", all / FX_PROGS * 1e3,
           worst * 1e3);
}
#endif

int main(void)
{
#if SM87_FX
    test_silence();
    test_rate();
    test_change();
    test_cost();
#endif
    printf("fx: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
