/* SM-87: the HR-16's kit, pads, a pattern's own sounds and the voices, see hr.h. The pads' voices: one a pad (a hit
 * cuts its last), two for a pad of poly 2 (the HR-16's crash), the hats one between them (a group: a hit cuts the
 * others of its group); a voice for a hit: a free one, else the oldest fading, else the oldest. */
#include "hr.h"
#include "fastmath.h"

#if defined(__has_include)
#if __has_include("hr_kit.h")
#include "hr_kit.h"               /* the build's (fm1.toml gen: tools/mkkit.py from resources/kit) */
#endif
#endif
#ifndef HR_KIT_N
#define HR_KIT_N 0u               /* none (the unit tests: hr_kit_use): the pads play the sampler's slots, or nothing */
#define HR_KIT_PADS 0u
static const hr_kit_sound_t HR_KIT[1];
static const hr_kit_pad_t HR_KIT_PAD[1];
#endif

hr_pad_t hr_pad[HR_PADS];
hr_pad_t hr_std[HR_PADS];
volatile uint32_t hr_lit;
volatile hr_snd_t hr_snd = {0xFFu, 0u, 0u};

static const hr_kit_sound_t *kit = HR_KIT;
static uint32_t kit_n = HR_KIT_N;
static const hr_kit_pad_t *kit_pad = HR_KIT_PAD;
static uint32_t kit_pads = HR_KIT_PADS;

void hr_kit_use(const hr_kit_sound_t *s, uint32_t n, const hr_kit_pad_t *pads, uint32_t np)
{
    kit = s;
    kit_n = n;
    kit_pad = pads;
    kit_pads = np;
}

uint32_t hr_kit_sounds(void) { return kit_n; }
uint32_t hr_sounds(void) { return kit_n + S7_SLOTS; }

void hr_sound_name(uint32_t sound, char *b)
{
    uint32_t i = 0;
    if (sound < kit_n) {
        for (; i < HR_NAME && kit[sound].name[i]; i++)
            b[i] = kit[sound].name[i];
    } else if (sound < kit_n + S7_SLOTS) {
        uint32_t s = sound - kit_n + 1u;
        b[0] = 'S', b[1] = 'L', b[2] = 'O', b[3] = 'T', b[4] = (char)('0' + s / 10u), b[5] = (char)('0' + s % 10u);
        i = 6;
    } else {
        for (; i < 5u; i++)
            b[i] = '-';
    }
    b[i] = 0;
}

const char *hr_pad_name(uint32_t pad)
{
    static char t[HR_PADS][7];
    if (pad < kit_pads && kit_pad[pad].name)
        return kit_pad[pad].name;
    pad %= HR_PADS;
    t[pad][0] = 'P', t[pad][1] = 'A', t[pad][2] = 'D', t[pad][3] = ' ';
    t[pad][4] = (char)('0' + (pad + 1u) / 10u), t[pad][5] = (char)('0' + (pad + 1u) % 10u), t[pad][6] = 0;
    return t[pad];
}

/* the notes a pad beyond the kit's takes: General MIDI's drums, the first one no pad before it has */
static const uint8_t GM_NOTES[] = {36, 38, 42, 46, 49, 51, 37, 39, 41, 43, 45, 47, 48, 50, 54, 56, 44, 40, 52, 53};

void hr_pad_default(uint32_t pad)
{
    hr_pad_t *p;
    uint32_t i, k;
    if (pad >= HR_PADS)
        return;
    p = &hr_pad[pad];
    if (pad < kit_pads) {
        *p = kit_pad[pad].pad;
        if (p->sound != HR_NO_SOUND && p->sound >= kit_n + S7_SLOTS)
            p->sound = HR_NO_SOUND;
        return;
    }
    p->sound = HR_NO_SOUND;
    p->tune = 0;
    p->level = 85;
    p->pan = 0;
    p->group = 0;
    p->poly = 1;
    p->rsv = 0;
    p->note = 35;
    for (k = 0; k < sizeof GM_NOTES; k++) {
        for (i = 0; i < pad && hr_pad[i].note != GM_NOTES[k]; i++)
            ;
        if (i == pad) {
            p->note = GM_NOTES[k];
            break;
        }
    }
}

void hr_pads_rekit(uint32_t was)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++) {
        hr_pad_t *p = &hr_std[i];
        if (p->sound == HR_NO_SOUND)
            continue;
        if (p->sound >= was)      /* a slot: after the kit as it is now */
            p->sound = p->sound - was < S7_SLOTS ? (uint8_t)(kit_n + p->sound - was) : HR_NO_SOUND;
        else if (p->sound >= kit_n)
            p->sound = HR_NO_SOUND;   /* (a sound the kit no longer has) */
        if (p->tune > HR_TUNE_MAX || p->tune < -HR_TUNE_MAX)
            p->tune = 0;
        if (p->level > 99u)
            p->level = 99u;
        if (p->pan > HR_PAN_MAX || p->pan < -HR_PAN_MAX)
            p->pan = 0;
        if (p->poly < 1u || p->poly > 2u)
            p->poly = 1;
        p->note &= 0x7Fu;
    }
}

/* ---------------------------------------------------------------------------- a pattern's own sounds --- */
/* a sound as a pattern keeps it: the kit's by number (0..127), a slot as 0x80 + its number, none 0xFF */
static uint8_t sound_code(uint32_t s)
{
    return s < kit_n ? (uint8_t)s : s < kit_n + S7_SLOTS ? (uint8_t)(0x80u + s - kit_n) : HR_NO_SOUND;
}

static uint8_t sound_from(uint32_t c)
{
    if (c < 0x80u)
        return c < kit_n ? (uint8_t)c : HR_NO_SOUND;
    return c - 0x80u < S7_SLOTS ? (uint8_t)(kit_n + c - 0x80u) : HR_NO_SOUND;
}

static int8_t in_range(int8_t v, int32_t max) { return v > max || v < -max ? 0 : v; }

/* pad i's sound, tuning, level and pan, as pattern sounds d keep them, into x */
static void take(hr_pad_t *x, const uint8_t *d, uint32_t i)
{
    d += 4u * i;
    x->sound = sound_from(d[0]);
    x->tune = in_range((int8_t)d[1], HR_TUNE_MAX);
    x->level = d[2] > 99u ? 99u : d[2];
    x->pan = in_range((int8_t)d[3], HR_PAN_MAX);
}

int hr_sounds_own(uint32_t p) { return p < SQ_PATTERNS && sq_len(SQ_PSND(p)) == SQ_SOUNDS_LEN; }

void hr_sounds_apply(uint32_t p)
{
    int own = hr_sounds_own(p);
    const uint8_t *d = own ? sq_data(SQ_PSND(p)) : 0;
    uint32_t i;
    for (i = 0; i < HR_PADS; i++) {
        hr_pad_t *x = &hr_pad[i];
        if (own) {
            take(x, d, i);
        } else {
            x->sound = hr_std[i].sound;
            x->tune = hr_std[i].tune;
            x->level = hr_std[i].level;
            x->pan = hr_std[i].pan;
        }
    }
    hr_snd.p = p < SQ_PATTERNS ? (uint8_t)p : 0xFFu;
    hr_snd.own = (uint8_t)own;
    hr_snd.edited = 0;
}

/* the pads' sounds differ from the pattern's own */
static int differs(uint32_t p)
{
    const uint8_t *d = sq_data(SQ_PSND(p));
    uint32_t i;
    hr_pad_t x;
    for (i = 0; i < HR_PADS; i++) {
        const hr_pad_t *y = &hr_pad[i];
        take(&x, d, i);
        if (x.sound != y->sound || x.tune != y->tune || x.level != y->level || x.pan != y->pan)
            return 1;
    }
    return 0;
}

void hr_pad_changed(uint32_t pad)
{
    hr_pad_t *p, *g;
    if (pad >= HR_PADS)
        return;
    p = &hr_pad[pad];
    g = &hr_std[pad];
    g->note = p->note;
    g->group = p->group;
    g->poly = p->poly;
    if (hr_snd.own && hr_sounds_own(hr_snd.p)) {
        hr_snd.edited = (uint8_t)differs(hr_snd.p);
        return;
    }
    g->sound = p->sound;
    g->tune = p->tune;
    g->level = p->level;
    g->pan = p->pan;
}

/* the pads take p's sounds again if they have them (its own changed) */
static void again(uint32_t p)
{
    sq_lock();
    if (hr_snd.p == p)
        hr_sounds_apply(p);
    sq_unlock();
}

int hr_sounds_put(uint32_t p, const hr_pad_t *pads)
{
    uint8_t b[SQ_SOUNDS_LEN];
    uint32_t i;
    if (p >= SQ_PATTERNS)
        return -1;
    for (i = 0; i < HR_PADS; i++) {
        const hr_pad_t *x = &pads[i];
        b[4u * i] = sound_code(x->sound);
        b[4u * i + 1u] = (uint8_t)x->tune;
        b[4u * i + 2u] = x->level;
        b[4u * i + 3u] = (uint8_t)x->pan;
    }
    return sq_replace(SQ_PSND(p), b, SQ_SOUNDS_LEN);
}

void hr_sounds_get(uint32_t p, hr_pad_t *pads)
{
    int own = hr_sounds_own(p);
    uint32_t i;
    for (i = 0; i < HR_PADS; i++) {
        pads[i] = hr_std[i];
        if (own)
            take(&pads[i], sq_data(SQ_PSND(p)), i);
    }
}

int hr_sounds_store(uint32_t p)
{
    if (hr_sounds_put(p, hr_pad) != 0)
        return -1;
    again(p);                     /* (the pads as they were: edits kept are stored now) */
    return 0;
}

int hr_sounds_drop(uint32_t p)
{
    if (p >= SQ_PATTERNS)
        return -1;
    if (sq_len(SQ_PSND(p)) && sq_replace(SQ_PSND(p), 0, 0) != 0)
        return -1;
    again(p);
    return 0;
}

int hr_sounds_copy(uint32_t a, uint32_t b)
{
    uint8_t t[SQ_SOUNDS_LEN];
    uint32_t i;
    if (a == b || a >= SQ_PATTERNS || b >= SQ_PATTERNS)
        return 0;
    if (!hr_sounds_own(a))
        return hr_sounds_drop(b);
    for (i = 0; i < SQ_SOUNDS_LEN; i++)   /* (out of the arena: sq_replace moves it) */
        t[i] = sq_data(SQ_PSND(a))[i];
    if (sq_replace(SQ_PSND(b), t, SQ_SOUNDS_LEN) != 0)
        return -1;
    again(b);
    return 0;
}

/* ------------------------------------------------------------------------------------------- the voices --- */
static uint8_t vpad[S7_DRUM_VOICES];      /* the pad each voice plays */
static s7_slot_t vsnd[S7_DRUM_VOICES];    /* a kit sound as a slot, for the voice playing it */

/* the sound as the voices take it, its slot (0xFF: the kit's), velocity's say; 0 none */
static const s7_slot_t *sound_of(uint32_t v, uint32_t sound, uint32_t *slot, uint32_t *vel_amt)
{
    if (sound < kit_n) {
        s7_slot_t *s = &vsnd[v];
        const hr_kit_sound_t *k = &kit[sound];
        if (!k->data || !k->words)
            return 0;
        s->data = k->data;
        s->words = k->words;
        s->pressure = 0;
        s->p[S7_P_RPMODE] = S7_ONESHOT;
        s7_set_word(s->p, S7_P_SSTRT, 0);
        s7_set_word(s->p, S7_P_SEND, k->words);
        s7_set_word(s->p, S7_P_SLOOP, 0);
        s->p[S7_P_RECBW] = k->rate;
        s->p[S7_P_SFNTUN] = 0;
        *slot = 0xFFu;
        *vel_amt = k->vel;
        return s;
    }
    if (sound < kit_n + S7_SLOTS && s7_slot[sound - kit_n].data) {
        *slot = sound - kit_n;
        *vel_amt = s7_slot[*slot].p[S7_P_VELLDN];
        return &s7_slot[*slot];
    }
    return 0;
}

static uint32_t pick_voice(void)
{
    uint32_t v, best = S7_DRUM_VOICES;
    for (v = 0; v < S7_DRUM_VOICES; v++)
        if (!s7_drum_active(v))
            return v;
    for (v = 0; v < S7_DRUM_VOICES; v++)   /* the oldest fading */
        if (s7_drum_released(v) && (best == S7_DRUM_VOICES || s7_drum_age(v) > s7_drum_age(best)))
            best = v;
    if (best < S7_DRUM_VOICES)
        return best;
    for (v = best = 0; v < S7_DRUM_VOICES; v++)
        if (s7_drum_age(v) > s7_drum_age(best))
            best = v;
    return best;
}

void hr_hit(uint32_t pad, uint32_t vel)
{
    const hr_pad_t *pd;
    const s7_slot_t *s;
    uint32_t v, same = 0, oldest = S7_DRUM_VOICES, slot, amt, poly;
    float g, gl, gr;
    if (pad >= HR_PADS || !vel)
        return;
    pd = &hr_pad[pad];
    hr_lit |= 1u << pad;
    if (pd->sound >= kit_n + S7_SLOTS || (pd->sound >= kit_n && !s7_slot[pd->sound - kit_n].data))
        return;                   /* no sound: lit, nothing cut */
    for (v = 0; v < S7_DRUM_VOICES; v++) {   /* its own over its poly, and its group's others, cut */
        if (!s7_drum_active(v) || s7_drum_released(v))
            continue;
        if (vpad[v] == pad) {
            same++;
            if (oldest == S7_DRUM_VOICES || s7_drum_age(v) > s7_drum_age(oldest))
                oldest = v;
        } else if (pd->group && hr_pad[vpad[v]].group == pd->group) {
            s7_drum_choke(v);
        }
    }
    poly = pd->poly ? pd->poly : 1u;
    if (same >= poly)
        s7_drum_choke(oldest);
    v = pick_voice();
    if (!(s = sound_of(v, pd->sound, &slot, &amt)))
        return;
    g = fm_db2lin(s7_level_db(pd->level, amt, vel));
    gl = pd->pan > 0 ? g * (float)(HR_PAN_MAX - pd->pan) / (float)HR_PAN_MAX : g;
    gr = pd->pan < 0 ? g * (float)(HR_PAN_MAX + pd->pan) / (float)HR_PAN_MAX : g;
    s7_drum_start(v, s, slot, (float)pd->tune, gl, gr);
    vpad[v] = (uint8_t)pad;
}

void hr_note(uint32_t note, uint32_t vel)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++)
        if (hr_pad[i].note == note)
            hr_hit(i, vel);
}

int32_t hr_pad_of(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++)
        if (hr_pad[i].note == note)
            return (int32_t)i;
    return -1;
}

void hr_kill(void) { s7_drum_kill(); }
void hr_render(float *l, float *r, uint32_t n) { s7_drum_render(l, r, n); }

uint32_t hr_voices_active(void)
{
    uint32_t v, n = 0;
    for (v = 0; v < S7_DRUM_VOICES; v++)
        n += (uint32_t)s7_drum_active(v);
    return n;
}
