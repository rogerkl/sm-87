/* SM-87: the S700/X7000 voices. See s7.h, and documentation/akai-s700-x7000/04-playback.md for every formula.
 * Single-precision float (the FM-1's FPU is float-only; fm1-base's fastmath.h for the maths), positions as 32.32
 * fixed point in two 32-bit words, so nothing needs a double or a 64-bit multiply. */
#include "s7.h"
#include "fastmath.h"

s7_slot_t s7_slot[S7_SLOTS];

/* ------------------------------------------------------------------------------------------------- tables --- */
/* the sampler's periods in ns for rate codes 36..76 (the X7000's; rate = 1e9 / period) */
static const uint32_t PERIOD_NS[41] = {
    254817, 240515, 227016, 214275, 202248, 190897, 180183, 170070, 160525, 151515, 143011, 134985, 127409, 120258,
    113508, 107137, 101124, 95449,  90091,  85035,  80262,  75758,  71506,  67492,  63704,  60129,  56754,  53569,
    50562,  47724,  45046,  42518,  40131,  37879,  35753,  33746,  31852,  30064,  28377,  26784,  25281,
};

float s7_rate(uint32_t code)
{
    if (code < 36u)
        code = 36u;
    if (code <= 76u)
        return 1.0e9f / (float)PERIOD_NS[code - 36u];
    return 26400.0f * fm_exp2f((float)((int32_t)code - 69) / 12.0f);   /* 77: Akai's spec allows it */
}

/* the 6th order Butterworth: three 2-pole state variable sections (Axs7 filter.rs), Q of each */
static const float BW6_Q[3] = {0.5176381f, 0.70710678f, 1.9318516f};
static float flt[100][3][3];      /* FILTER 0..99: each section's a1, a2, a3 */
static float rel_g[100];          /* RELEASE 0..99: the level's factor a sample */
static float fs_now = 44117.647f;
static float att_inc;             /* the 2 ms attack's step */
static uint32_t ctl_lfo_step;     /* samples between vibrato steps for LFO DELAY 0, scaled below */

float s7_cutoff_hz(uint32_t f) { return 306.0f * fm_exp2f((float)f / 17.29f); }

float s7_release_t60(uint32_t r)
{
    float k;
    int32_t ki;
    if (r > 99u)
        r = 99u;
    k = 40.0f * fm_exp2f((float)(50 - 10 * (int32_t)(r / 10u)) / 10.0f) * (1.0f - (float)(r % 10u) / 20.0f);
    ki = (int32_t)k;              /* floor: k > 0 */
    if (ki < 2)
        ki = 2;
    return 22.8f / (float)ki;
}

float s7_level_db(uint32_t level, uint32_t vel_amount, uint32_t vel)
{
    float a = (float)(vel_amount > 99u ? 99u : vel_amount) / 99.0f, v = (float)(vel < 1u ? 1u : vel > 127u ? 127u : vel);
    float l = (float)(level > 99u ? 99u : level);
    return -34.2f * (1.0f - l / 99.0f) + (1.0f - a) * -7.7f + a * -40.3f * (1.0f - (v - 1.0f) / 126.0f);
}

void s7_init(float fs)
{
    uint32_t f, s;
    fs_now = fs;
    for (f = 0; f < 100u; f++) {
        float fc = s7_cutoff_hz(f), g;
        if (fc > 0.45f * fs)
            fc = 0.45f * fs;
        g = fm_tanf(FM_PI * fc / fs);
        for (s = 0; s < 3u; s++) {
            float k = 1.0f / BW6_Q[s], a1 = 1.0f / (1.0f + g * (g + k));
            flt[f][s][0] = a1;
            flt[f][s][1] = g * a1;
            flt[f][s][2] = g * g * a1;
        }
        rel_g[f] = fm_expf(-6.90775528f / (s7_release_t60(f) * fs));   /* 0.001 ^ (1 / (T60 fs)) */
    }
    att_inc = 1.0f / (0.002f * fs);
    ctl_lfo_step = (uint32_t)(2.35f * fs);
}

void s7_default_params(uint8_t *p)
{
    uint32_t i;
    for (i = 0; i < S7_PROGRAMS; i++) {   /* in no program: KEYL 24, KEYH 0, transpose 0, audio trigger off (C3) */
        p[S7_P_KEYL(i)] = 24;
        p[S7_P_KEYH(i)] = 0;
        p[S7_P_TRANSP(i)] = 0;
        p[S7_P_ATPTCH(i)] = 0x80u | 60u;
    }
    for (i = S7_P_UNDEF; i < S7_NPARAM; i++)
        p[i] = 0;
    p[S7_P_UNDEF] = 2;
    p[S7_P_RPMODE] = S7_LOOP;
    s7_set_word(p, S7_P_SSTRT, 0);
    s7_set_word(p, S7_P_SEND, 360);
    s7_set_word(p, S7_P_LEN1, 359);
    s7_set_word(p, S7_P_SLOOP, 0);
    s7_set_word(p, S7_P_LEN2, 359);
    p[S7_P_LFSP] = 50;
    p[S7_P_LFDP] = 0;
    p[S7_P_LFDL] = 64;
    p[S7_P_FLTFRQ] = 99;
    p[S7_P_RLEASE] = 30;
    p[S7_P_LDNSS] = 75;
    p[S7_P_SFNTUN] = 0;
    p[S7_P_RECNP] = 60;
    p[S7_P_RECBW] = 48;
    p[S7_P_RVRSE] = 0;
    p[S7_P_VELFIL] = 0;
    p[S7_P_VELLDN] = 50;
    p[S7_P_VELRLS] = 0;
}

int s7_slot_in_program(const s7_slot_t *s, uint32_t prog)
{
    return s->data && s->words && s->p[S7_P_KEYL(prog)] <= s->p[S7_P_KEYH(prog)];
}

/* -------------------------------------------------------------------------------------------------- voices --- */
enum { ENV_OFF, ENV_ATTACK, ENV_HOLD, ENV_RELEASE };

typedef struct {
    uint8_t stage, key, slot, mode, vel, lfo_full, released;
    uint8_t ch;                   /* its channel's controllers (in OMNI ON or OFF: one for all, 0) */
    uint8_t drum;                 /* the drum machine's: no filter, no bend nor vibrato, panned (s7_drum_start) */
    const s7_slot_t *src;         /* the sound: a slot, or one of the drum kit's */
    const uint8_t *data;
    uint32_t len, loop_start, loop_end, period;   /* period: the loop's in positions (0: none) */
    uint32_t pos, frac;           /* the position, 32.32: whole words and the fraction */
    uint32_t step_i, step_f;      /* the step now, with bend and vibrato */
    float base_step;              /* the step at no bend nor vibrato */
    float level, env, rel;
    float gl, gr;                 /* a drum voice's gains, left and right */
    const float (*fc)[3];         /* the filter's sections, for this note's cutoff */
    float ic1[3], ic2[3];
    float lfo_phase, lfo_inc;     /* in turns */
    uint32_t age, lfo_delay;      /* samples since key on; samples a vibrato step takes to come in */
    uint32_t ctl;                 /* samples to the next vibrato update */
    int32_t win_i;                /* the Hermite window: the values at positions win_i - 1 .. win_i + 2 */
    float win[4];
    uint32_t started;
} voice_t;

static voice_t vc[S7_VOICES];
static voice_t dv[S7_DRUM_VOICES];   /* the drum machine's */
static uint32_t prog_now, note_count, bend_range = 2u, midi_mode = S7_OMNI_OFF;
static uint8_t wheel[S7_CHANNELS], press[S7_CHANNELS];   /* each channel's (MULTI), else [0] */
static int16_t bend_raw[S7_CHANNELS];
static float bend_ratio[S7_CHANNELS] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

#define CONTROL_PERIOD 16u

/* a MIDI channel's controllers: its own in MULTI; in OMNI ON or OFF one instrument, one set */
static uint32_t chan_of(uint32_t ch) { return midi_mode == S7_MULTI ? ch % S7_CHANNELS : 0u; }

void s7_program(uint32_t prog) { prog_now = prog % S7_PROGRAMS; }
uint32_t s7_current_program(void) { return prog_now; }
void s7_midi_mode(uint32_t mode) { midi_mode = mode <= S7_MULTI ? mode : S7_OMNI_OFF; }
uint32_t s7_program_of(uint32_t ch)
{
    return midi_mode == S7_MULTI ? (prog_now & 16u) + ch % S7_CHANNELS : prog_now;   /* (P1-16, or P17-32) */
}
static void bend_set(uint32_t c)
{
    bend_ratio[c] = fm_exp2f((float)bend_raw[c] / 8192.0f * (float)bend_range / 12.0f);
}
void s7_bend_range(uint32_t semis)
{
    uint32_t c;
    bend_range = semis > 12u ? 12u : semis;
    for (c = 0; c < S7_CHANNELS; c++)
        bend_set(c);
}
void s7_bend(uint32_t ch, int32_t v)
{
    uint32_t c = chan_of(ch);
    bend_raw[c] = (int16_t)v;
    bend_set(c);
}
void s7_modwheel(uint32_t ch, uint32_t v) { wheel[chan_of(ch)] = (uint8_t)(v > 127u ? 127u : v); }
void s7_pressure(uint32_t ch, uint32_t v) { press[chan_of(ch)] = (uint8_t)(v > 127u ? 127u : v); }

/* the word a voice reads at a position that keeps counting up through its loop (Axs7 voice.rs index()) */
static inline int32_t index_of(uint32_t mode, uint32_t len, uint32_t ls, uint32_t le, int32_t v)
{
    uint32_t u;
    if (v < 0)
        return 0;
    u = (uint32_t)v;
    if (u >= le && mode == S7_LOOP)
        return (int32_t)(ls + (u - ls) % (le - ls));
    if (u >= le && mode == S7_ALT) {
        uint32_t span = le - 1u - ls, k = (u - ls) % (2u * span);
        return (int32_t)(k <= span ? ls + k : ls + 2u * span - k);
    }
    return u < len ? (int32_t)u : -1;
}

int32_t s7_index(const s7_loop_t *l, int32_t v) { return index_of(l->mode, l->len, l->loop_start, l->loop_end, v); }

static inline float read_at(const voice_t *x, int32_t i)
{
    int32_t j = index_of(x->mode, x->len, x->loop_start, x->loop_end, i);
    return j < 0 ? 0.0f : (float)s7_w12(x->data, (uint32_t)j);
}

/* a voice at the sample's START, with its points and scan mode (once: a loop played through to END, as a drum
 * trigger) */
static void voice_points(voice_t *x, const s7_slot_t *s, uint32_t slot, int once)
{
    const uint8_t *p = s->p;
    uint32_t end = s7_word(p, S7_P_SEND), start = s7_word(p, S7_P_SSTRT), ls = s7_word(p, S7_P_SLOOP), le, mode;
    if (end > s->words)
        end = s->words;
    if (start + 1u > end)
        start = end ? end - 1u : 0u;
    le = end;                     /* a loop runs from LOOP to END */
    if (ls > le)
        ls = le;
    mode = p[S7_P_RPMODE];
    if ((mode != S7_LOOP && mode != S7_ALT && mode != S7_DRUM) || (once && mode != S7_DRUM))
        mode = S7_ONESHOT;
    if ((mode == S7_LOOP || mode == S7_ALT) && le < ls + 2u)
        mode = S7_ONESHOT;        /* a loop needs two words to go back and forth between */
    x->mode = (uint8_t)mode;
    x->data = s->data;
    x->len = end;
    x->loop_start = ls;
    x->loop_end = le;
    x->period = mode == S7_LOOP ? le - ls : mode == S7_ALT ? 2u * (le - 1u - ls) : 0u;
    x->pos = start;
    x->frac = 0;
    x->slot = (uint8_t)slot;
    x->src = s;
    x->released = 0;
    x->age = 0;
    x->ctl = 0;
    x->win_i = -0x7FFFFFFF;
    x->started = ++note_count;
}

static void voice_start(voice_t *x, uint32_t slot, uint32_t prog, uint32_t ch, uint32_t key, uint32_t vel)
{
    const s7_slot_t *s = &s7_slot[slot];
    const uint8_t *p = s->p;
    uint32_t tr = p[S7_P_TRANSP(prog)];
    int32_t semis, f;
    float rate;
    voice_points(x, s, slot, 0);
    x->drum = 0;
    x->ch = (uint8_t)ch;
    x->key = (uint8_t)key;
    x->vel = (uint8_t)vel;
    /* pitch: the key from the root (or the root's own pitch), the program's transpose, the fine tune */
    semis = (tr & 0x80u) ? 0 : (int32_t)key - (int32_t)p[S7_P_RECNP];
    semis += (int32_t)(tr & 0x7Fu) - ((tr & 0x40u) ? 128 : 0);
    rate = s7_rate(p[S7_P_RECBW]);
    x->base_step = rate / fs_now * fm_exp2f(((float)semis + (float)(int8_t)p[S7_P_SFNTUN] / 16.0f) / 12.0f);
    if (x->base_step > 60000.0f)
        x->base_step = 60000.0f;
    /* level and cutoff: from velocity */
    x->level = fm_db2lin(s7_level_db(p[S7_P_LDNSS], p[S7_P_VELLDN], vel));
    f = (int32_t)p[S7_P_FLTFRQ] + ((int32_t)p[S7_P_VELFIL] * ((int32_t)vel - 64) + (vel >= 64u ? 32 : -32)) / 64;
    x->fc = (const float (*)[3])flt[f < 0 ? 0 : f > 99 ? 99 : f];
    x->ic1[0] = x->ic1[1] = x->ic1[2] = x->ic2[0] = x->ic2[1] = x->ic2[2] = 0.0f;
    /* vibrato: a sine from key on, its own depth coming in a step at a time */
    x->lfo_phase = 0.0f;
    x->lfo_inc = p[S7_P_LFSP] ? (1.65f + 0.0634f * (float)p[S7_P_LFSP]) * (float)CONTROL_PERIOD / fs_now : 0.0f;
    x->lfo_full = (uint8_t)(8u * p[S7_P_LFDP] / 101u > 7u ? 7u : 8u * p[S7_P_LFDP] / 101u);
    x->lfo_delay = ctl_lfo_step / (100u - (p[S7_P_LFDL] > 99u ? 99u : p[S7_P_LFDL]));
    x->env = 0.0f;
    x->stage = ENV_ATTACK;
}

/* semitones either way for a number of vibrato steps: ±(2.7 + 6.4 n) cents */
static inline float vib_depth(uint32_t steps)
{
    return steps ? 0.027f + 0.064f * (float)(steps > 7u ? 7u : steps) : 0.0f;
}

static void voice_control(voice_t *x)
{
    const s7_slot_t *s = x->src;
    uint32_t lfo = x->lfo_delay ? x->age / x->lfo_delay : 7u, prs;
    float depth, st;
    uint32_t si;
    if (lfo > x->lfo_full)
        lfo = x->lfo_full;
    prs = (press[x->ch] / 16u) * s->pressure / 99u;
    depth = vib_depth(lfo) + vib_depth(wheel[x->ch] / 16u) + vib_depth(prs);
    st = x->drum ? x->base_step : x->base_step * bend_ratio[x->ch];   /* (a drum: no bend) */
    if (x->lfo_inc > 0.0f) {      /* SPEED 0 stops the LFO: no vibrato at all, not even the wheel's */
        if (depth > 0.0f)
            st *= fm_exp2f(depth * fm_sin_turns(x->lfo_phase) / 12.0f);
        x->lfo_phase += x->lfo_inc;
        if (x->lfo_phase >= 1.0f)
            x->lfo_phase -= 1.0f;
    }
    si = (uint32_t)st;
    x->step_i = si;
    st = (st - (float)si) * 4294967296.0f;
    x->step_f = st >= 4294967040.0f ? 0xFFFFFF00u : (uint32_t)st;
}

static void voice_render(voice_t *x, float *out, uint32_t n)
{
    uint32_t k;
    float y, t, c1, c2, c3, v, amp;
    const float (*fc)[3] = x->fc;
    for (k = 0; k < n; k++) {
        int32_t i = (int32_t)x->pos;
        if (x->ctl == 0) {
            voice_control(x);
            x->ctl = x->drum ? 0xFFFFFFFFu : CONTROL_PERIOD;   /* (a drum's pitch never moves) */
        }
        x->ctl--;
        x->age++;
        if (i != x->win_i) {      /* the window moves: one new word, or all four after a jump */
            if (i == x->win_i + 1) {
                x->win[0] = x->win[1];
                x->win[1] = x->win[2];
                x->win[2] = x->win[3];
                x->win[3] = read_at(x, i + 2);
            } else {
                x->win[0] = read_at(x, i - 1);
                x->win[1] = read_at(x, i);
                x->win[2] = read_at(x, i + 1);
                x->win[3] = read_at(x, i + 2);
            }
            x->win_i = i;
            if (index_of(x->mode, x->len, x->loop_start, x->loop_end, i) < 0) {
                x->stage = ENV_OFF; /* the end of a sample without loop */
                return;
            }
        }
        /* 4-point Hermite between win[1] and win[2] */
        t = (float)(x->frac >> 8) * (1.0f / 16777216.0f);
        c1 = 0.5f * (x->win[2] - x->win[0]);
        c2 = x->win[0] - 2.5f * x->win[1] + 2.0f * x->win[2] - 0.5f * x->win[3];
        c3 = 0.5f * (x->win[3] - x->win[0]) + 1.5f * (x->win[1] - x->win[2]);
        y = ((c3 * t + c2) * t + c1) * t + x->win[1];
        /* the 6-pole lowpass: three state variable sections (trapezoidal, Axs7 filter.rs); a drum's goes without */
        if (!x->drum) {
            uint32_t s;
            for (s = 0; s < 3u; s++) {
                float v3 = y - x->ic2[s];
                float v1 = fc[s][0] * x->ic1[s] + fc[s][1] * v3;
                float v2 = x->ic2[s] + fc[s][1] * x->ic1[s] + fc[s][2] * v3;
                x->ic1[s] = 2.0f * v1 - x->ic1[s];
                x->ic2[s] = 2.0f * v2 - x->ic2[s];
                y = v2;
            }
        }
        /* the level: a 2 ms ramp against clicks, held, then the exponential release */
        if (x->stage == ENV_ATTACK) {
            x->env += att_inc;
            if (x->env >= 1.0f) {
                x->env = 1.0f;
                x->stage = ENV_HOLD;
            }
        } else if (x->stage == ENV_RELEASE) {
            x->env *= x->rel;
            if (x->env < 1e-4f) {
                x->stage = ENV_OFF;
                return;
            }
        }
        amp = x->env * x->level;
        v = y * amp;
        out[k] += v;
        /* on: the position moves by the step, kept within the loop's first period */
        {
            uint32_t f = x->frac + x->step_f;
            x->pos += x->step_i + (f < x->frac);
            x->frac = f;
        }
        if (x->period && x->pos >= x->loop_start + x->period) {
            x->pos -= x->period;
            x->win_i -= (int32_t)x->period;
        }
    }
}

/* a voice for a new note: the first free one, else the quietest released, else the oldest */
static voice_t *voice_free(void)
{
    voice_t *best = 0;
    uint32_t i;
    for (i = 0; i < S7_VOICES; i++)
        if (vc[i].stage == ENV_OFF)
            return &vc[i];
    for (i = 0; i < S7_VOICES; i++)
        if (vc[i].stage == ENV_RELEASE && (!best || vc[i].env < best->env))
            best = &vc[i];
    if (best)
        return best;
    best = &vc[0];
    for (i = 1; i < S7_VOICES; i++)
        if (vc[i].started - best->started > 0x80000000u)   /* older (wraps) */
            best = &vc[i];
    return best;
}

void s7_note_on(uint32_t ch, uint32_t key, uint32_t vel)
{
    uint32_t sl, prog = s7_program_of(ch);
    if (vel == 0u) {
        s7_note_off(ch, key, 64u);
        return;
    }
    for (sl = 0; sl < S7_SLOTS; sl++) {
        const s7_slot_t *s = &s7_slot[sl];
        if (!s7_slot_in_program(s, prog) || key < s->p[S7_P_KEYL(prog)] || key > s->p[S7_P_KEYH(prog)])
            continue;
        voice_start(voice_free(), sl, prog, chan_of(ch), key, vel);
    }
}

/* a voice let go: VEL>RELEASE follows the note off's velocity, from 64: longer below, shorter above (a drum
 * trigger plays to its end) */
static void voice_release(voice_t *x, uint32_t off_vel)
{
    const uint8_t *p;
    int32_t d, r;
    if (x->stage == ENV_OFF || x->released || x->mode == S7_DRUM)
        return;
    p = x->src->p;
    d = (int32_t)p[S7_P_VELRLS] * (64 - (int32_t)off_vel);
    r = (int32_t)p[S7_P_RLEASE] + (d + (d >= 0 ? 32 : -32)) / 64;
    x->released = 1;
    x->rel = rel_g[r < 0 ? 0 : r > 99 ? 99 : r];
    x->stage = ENV_RELEASE;
}

void s7_note_off(uint32_t ch, uint32_t key, uint32_t off_vel)
{
    uint32_t i, c = chan_of(ch);
    for (i = 0; i < S7_VOICES; i++)
        if (vc[i].key == key && vc[i].ch == c)
            voice_release(&vc[i], off_vel);
}

void s7_all_off(void)
{
    uint32_t i;
    for (i = 0; i < S7_VOICES; i++)
        voice_release(&vc[i], 64u);
}

void s7_channel_off(uint32_t ch)
{
    uint32_t i, c = chan_of(ch);
    for (i = 0; i < S7_VOICES; i++)
        if (vc[i].ch == c)
            voice_release(&vc[i], 64u);
}

void s7_kill(void)
{
    uint32_t i;
    for (i = 0; i < S7_VOICES; i++)
        vc[i].stage = ENV_OFF;
}

void s7_kill_slot(uint32_t slot)
{
    uint32_t i;
    for (i = 0; i < S7_VOICES; i++)
        if (vc[i].slot == slot)
            vc[i].stage = ENV_OFF;
    for (i = 0; i < S7_DRUM_VOICES; i++)   /* (a pad may play a slot) */
        if (dv[i].slot == slot)
            dv[i].stage = ENV_OFF;
}

uint32_t s7_voices_active(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < S7_VOICES; i++)
        n += vc[i].stage != ENV_OFF;
    return n;
}

/* the mix: each slot's gains, left and right (s7_mix) */
static float mix_l[S7_SLOTS] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
static float mix_r[S7_SLOTS] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

void s7_mix(uint32_t slot, float gl, float gr)
{
    if (slot < S7_SLOTS) {
        mix_l[slot] = gl;
        mix_r[slot] = gr;
    }
}

/* the voices, each as its slot's level and pan put it (a voice a stretch of 64 at a time, then into the two) */
void s7_render(float *l, float *r, uint32_t n)
{
    float tmp[64];
    uint32_t i, k, at, m;
    for (i = 0; i < S7_VOICES; i++) {
        voice_t *x = &vc[i];
        float gl = mix_l[x->slot % S7_SLOTS], gr = mix_r[x->slot % S7_SLOTS];
        for (at = 0; at < n && x->stage != ENV_OFF; at += m) {
            m = n - at < 64u ? n - at : 64u;
            for (k = 0; k < m; k++)
                tmp[k] = 0.0f;
            voice_render(x, tmp, m);
            for (k = 0; k < m; k++) {
                l[at + k] += tmp[k] * gl;
                r[at + k] += tmp[k] * gr;
            }
        }
    }
}

/* ------------------------------------------------------------------------------------- the drum machine --- */
/* The HR-16's voices (hr16/): the sampler's, reading a sound in the same format, played once from START to END at
 * its own pitch moved by semis, without the filter, the bend or vibrato, and panned. Level and pan: the caller's */
void s7_drum_start(uint32_t v, const s7_slot_t *s, uint32_t slot, float semis, float gl, float gr)
{
    voice_t *x;
    if (v >= S7_DRUM_VOICES || !s || !s->data || !s->words)
        return;
    x = &dv[v];
    voice_points(x, s, slot, 1);
    x->drum = 1;
    x->key = 0;
    x->vel = 0;
    x->base_step = s7_rate(s->p[S7_P_RECBW]) / fs_now *
                   fm_exp2f((semis + (float)(int8_t)s->p[S7_P_SFNTUN] / 16.0f) / 12.0f);
    if (x->base_step > 60000.0f)
        x->base_step = 60000.0f;
    x->level = 1.0f;
    x->gl = gl;
    x->gr = gr;
    x->lfo_inc = 0.0f;
    x->lfo_full = 0;
    x->lfo_delay = 0;
    x->env = 1.0f;                /* (a drum hits at once: no ramp) */
    x->stage = ENV_HOLD;
}

void s7_drum_choke(uint32_t v)
{
    if (v < S7_DRUM_VOICES && dv[v].stage != ENV_OFF && !dv[v].released) {
        dv[v].released = 1;
        dv[v].rel = rel_g[0];     /* RELEASE 0: gone in about 20 ms */
        dv[v].stage = ENV_RELEASE;
    }
}

int s7_drum_active(uint32_t v) { return v < S7_DRUM_VOICES && dv[v].stage != ENV_OFF; }
int s7_drum_released(uint32_t v) { return v < S7_DRUM_VOICES && dv[v].released; }
uint32_t s7_drum_age(uint32_t v) { return v < S7_DRUM_VOICES ? note_count - dv[v].started : 0u; }

void s7_drum_kill(void)
{
    uint32_t i;
    for (i = 0; i < S7_DRUM_VOICES; i++)
        dv[i].stage = ENV_OFF;
}

void s7_drum_render(float *l, float *r, uint32_t n)
{
    float tmp[64];
    uint32_t i, k, at, m;
    for (i = 0; i < S7_DRUM_VOICES; i++) {
        voice_t *x = &dv[i];
        for (at = 0; at < n && x->stage != ENV_OFF; at += m) {
            m = n - at < 64u ? n - at : 64u;
            for (k = 0; k < m; k++)
                tmp[k] = 0.0f;
            voice_render(x, tmp, m);
            for (k = 0; k < m; k++) {
                l[at + k] += tmp[k] * x->gl;
                r[at + k] += tmp[k] * x->gr;
            }
        }
    }
}
