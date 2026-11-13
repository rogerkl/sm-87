/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the effect, the Alesis MIDIVerb (fx.h). The send through the MIDIVerb's input filter, taken at its rate
 * (FX_RATE: a sample in about every 1.9 of the FM-1's, between them by a straight line), its program run once a
 * sample (Eric Brombaugh's compiled code, mv_progs.c), its two outputs back at the FM-1's rate the same way and
 * through its output filter. A program change fades out, clears the memory and fades in, as g081_audio does it */
#include "../config.h"
#if SM87_FX
#include "fx.h"
#include "mv_names.h"             /* (g081_audio's: mv_prog_names, the program chart's) */

extern void (*mv_progs[63])(int16_t, int16_t *, int16_t *);
extern int16_t mv_mem[16384];
extern uint16_t mv_addr;
extern int16_t mv_acc;

/* the filters: matched second-order sections (M. Vicanek, "Matched Second Order Digital Filters", 2016) of the
 * analogue ones MIDIVerb_RE's spice/ models, at 44,117.6 Hz: b0, b1 (b2 0), a1, a2. In, three Sallen-Key stages,
 * 3.9 kHz Q 0.82, 8.8 kHz Q 2.75, 10.5 kHz Q 7.3 (as theirs within 0.1 dB to 11.7 kHz: 5 dB up at 10 kHz, 7 dB down
 * at the MIDIVerb's half rate); out, one, 10.5 kHz Q 0.74 */
static const float AA[3][4] = {{0.170590556f, 0.046223494f, -1.294326279f, 0.511140328f},
                               {0.837100797f, 0.260997545f, -0.537407921f, 0.635506263f},
                               {1.254677546f, 0.425401989f, -0.134387711f, 0.814467246f}};
static const float DAC[4] = {0.630225943f, 0.176252374f, -0.326150352f, 0.132628669f};

typedef struct {
    float z1, z2;
} bq_t;

/* the coupling capacitors in and out: a high-pass at 5 Hz (1 - 2 pi 5 / 44117.6). Out, as most programs idle a step
 * below 0 (their ~acc of 0, -1), which the MIDIVerb's own output capacitor takes away */
#define DC_R 0.999288f
typedef struct {
    float x1, y1;
} dc_t;

static inline float dc(dc_t *s, float x)
{
    float y = x - s->x1 + DC_R * s->y1;
    s->x1 = x;
    s->y1 = y;
    return y;
}

/* a state decayed to nothing made 0: not on into the denormals (pi32v2's FPU on them: not known) */
static inline void tiny(float *z)
{
    if (*z > -1e-20f && *z < 1e-20f)
        *z = 0.0f;
}

static inline float bq(bq_t *s, const float *c, float x)
{
    float y = c[0] * x + s->z1;
    s->z1 = c[1] * x - c[2] * y + s->z2;
    s->z2 = -c[3] * y;
    return y;
}

enum { RUN, FADE_OUT, CLEAR, FADE_IN };
#define FADE_STEP 8               /* the fade, of 4096 a sample: 512 samples, 22 ms (g081_audio's) */
#define CLEAR_STEP 64u            /* words of its memory cleared a sample: 256 samples, 11 ms */

static bq_t aa[3], out_l, out_r;
static dc_t dc_in, dc_l, dc_r;
static float step, inv_step, ph, x_last, yl0, yl1, yr0, yr1, ret_gain;
static uint8_t prog, next, state, on;
static int32_t fade;              /* 0..4096: the input's and the output's */
static uint32_t clr;

void fx_init(float fs)
{
    uint32_t i;
    step = FX_RATE / fs;
    inv_step = fs / FX_RATE;
    for (i = 0; i < 3u; i++)
        aa[i].z1 = aa[i].z2 = 0.0f;
    out_l.z1 = out_l.z2 = out_r.z1 = out_r.z2 = 0.0f;
    dc_in.x1 = dc_in.y1 = dc_l.x1 = dc_l.y1 = dc_r.x1 = dc_r.y1 = 0.0f;
    ph = x_last = yl0 = yl1 = yr0 = yr1 = 0.0f;
    ret_gain = 0.0f;
    prog = next = 0;
    on = 0;
    state = CLEAR;                /* (as it comes on: its memory cleared, then in) */
    fade = 0;
    clr = 0;
}

void fx_program(uint32_t p)
{
    if (p >= FX_PROGS)
        return;
    next = (uint8_t)p;
    if (on && (state == RUN || state == FADE_IN) && next != prog)
        state = FADE_OUT;         /* (fading out or clearing: the one now asked for comes in) */
}

uint32_t fx_program_now(void) { return next; }

const char *fx_name(uint32_t p) { return p < FX_PROGS ? mv_prog_names[p] : ""; }

void fx_set(uint32_t ret, int run)
{
    float g = (float)(ret > 99u ? 99u : ret) / 99.0f;
    int now = run && ret;
    ret_gain = g * g;             /* (a fader's: as the mix's levels) */
    if (now && !on) {             /* on again: what its memory had is gone, as after a program change */
        state = CLEAR;
        fade = 0;
        clr = 0;
    }
    on = (uint8_t)now;
}

/* one of the MIDIVerb's samples: x, the send at its instant, in its units (the mix's: 4096 full scale, its 12 bits
 * and sign: the two inputs summed, the lowest bit 0) */
static void mv_sample(float x)
{
    int32_t v = (int32_t)x;
    int16_t ol = 0, orr = 0;
    v = v > 4095 ? 4095 : v < -4096 ? -4096 : v;
    v = (int32_t)(((v * fade) >> 12) & ~1);
    if (state == CLEAR) {
        uint32_t k;
        for (k = 0; k < CLEAR_STEP; k++)
            mv_mem[clr + k] = 0;
        clr += CLEAR_STEP;
        if (clr >= 16384u) {
            prog = next;
            mv_acc = 0;
            state = FADE_IN;
        }
    } else {
        mv_progs[prog]((int16_t)v, &ol, &orr);
    }
    if (state == FADE_OUT) {
        fade -= FADE_STEP;
        if (fade <= 0) {
            fade = 0;
            clr = 0;
            state = CLEAR;
        }
    } else if (state == FADE_IN) {
        fade += FADE_STEP;
        if (fade >= 4096) {
            fade = 4096;
            state = RUN;
        }
    }
    /* out as its converter has them: 12 bits and sign (MIDIVerb_RE's emulator), faded */
    ol = ol > 4095 ? 4095 : ol < -4096 ? -4096 : ol;
    orr = orr > 4095 ? 4095 : orr < -4096 ? -4096 : orr;
    yl0 = yl1;
    yr0 = yr1;
    yl1 = (float)ol * (float)fade * (1.0f / 4096.0f);
    yr1 = (float)orr * (float)fade * (1.0f / 4096.0f);
}

void fx_render(const float *send, float *l, float *r, uint32_t n)
{
    uint32_t i;
    if (!on)
        return;
    for (i = 0; i < n; i++) {
        float x = bq(&aa[2], AA[2], bq(&aa[1], AA[1], bq(&aa[0], AA[0], dc(&dc_in, send[i]))));
        ph += step;
        if (ph >= 1.0f) {         /* its sample falls between the last frame and this: ph / step frames back */
            ph -= 1.0f;
            mv_sample(x - (x - x_last) * ph * inv_step);
        }
        x_last = x;
        l[i] += dc(&dc_l, bq(&out_l, DAC, yl0 + (yl1 - yl0) * ph)) * ret_gain;   /* (between its last two, a
                                                                                  * sample late) */
        r[i] += dc(&dc_r, bq(&out_r, DAC, yr0 + (yr1 - yr0) * ph)) * ret_gain;
    }
    for (i = 0; i < 3u; i++) {
        tiny(&aa[i].z1);
        tiny(&aa[i].z2);
    }
    tiny(&out_l.z1), tiny(&out_l.z2), tiny(&out_r.z1), tiny(&out_r.z2);
    tiny(&dc_in.x1), tiny(&dc_in.y1), tiny(&dc_l.x1), tiny(&dc_l.y1), tiny(&dc_r.x1), tiny(&dc_r.y1);
}

#endif /* SM87_FX */
