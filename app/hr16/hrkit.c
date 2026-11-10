/* SM-87: the HR-16's kit, pads and voices, see hr.h. The pads' voices: one a pad (a hit cuts its last), two for a pad
 * of poly 2 (the HR-16's crash), the hats one between them (a group: a hit cuts the others of its group); a voice
 * for a hit: a free one, else the oldest fading, else the oldest. */
#include "hr.h"
#include "fastmath.h"

#if defined(__has_include)
#if __has_include("rom/hr_kit.h")
#include "rom/hr_kit.h"
#endif
#endif
#ifndef HR_KIT_N
#define HR_KIT_N 0u               /* a build without a kit: the pads play the sampler's slots, or nothing */
#define HR_KIT_PADS 0u
static const hr_kit_sound_t HR_KIT[1];
static const hr_kit_pad_t HR_KIT_PAD[1];
#endif

hr_pad_t hr_pad[HR_PADS];
volatile uint32_t hr_lit;

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
        hr_pad_t *p = &hr_pad[i];
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
