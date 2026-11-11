/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the HR-16's memory dump, see hrdump.h */
#include "hrdump.h"

const uint8_t HD_PRE[HD_PREFIX] = {0xF0, 0x00, 0x00, 0x0E, HD_ID, HD_CMD_DUMP};
const uint8_t HD_PAD[HD_DRUMS] = {0, 1, 2, 6, 3, 7, 14, 15, 8, 9, 10, 11, 5, 4, 12, 13};

#define DRUM_RIDE 12u

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static void put_be16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
static void put_le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }

static uint32_t from_bcd(uint32_t v)
{
    return (v & 15u) + (v >> 4 & 15u) * 10u + (v >> 8 & 15u) * 100u + (v >> 12 & 15u) * 1000u;
}

static uint32_t to_bcd(uint32_t v) { return v % 10u | (v / 10u % 10u) << 4 | (v / 100u % 10u) << 8 | (v / 1000u) << 12; }

int hd_has_header(void) { return sq.hr16[0xFE] == 0x27u && sq.hr16[0xFF] == 0xB5u; }

/* ------------------------------------------------------------------------------------------------ sounds --- */
/* its 49 sounds by kind (the HR-16's voice list): 00-09 kicks, 10-17 snares (a sidestick and a brush hit among them),
 * 18-22 toms, 23-27 hats (25, 26 open), 28-30 cymbals (two rides, a crash), 31-48 percussion, 46 the hand claps */
enum { K_KICK, K_SNARE, K_TOM, K_CLHAT, K_OPHAT, K_RIDE, K_CRASH, K_CLAP, K_PERC, K_N };

static uint32_t kind_of(uint32_t n)
{
    if (n < 10u)
        return K_KICK;
    if (n < 18u)
        return K_SNARE;
    if (n < 23u)
        return K_TOM;
    if (n == 25u || n == 26u)
        return K_OPHAT;
    if (n < 28u)
        return K_CLHAT;
    if (n < 30u)
        return K_RIDE;
    if (n == 30u)
        return K_CRASH;
    return n == 46u ? K_CLAP : K_PERC;
}

/* the kit's names for a kind (a sound's name starting so), in the order tried */
static const char *const KIND_NAME[K_N][2] = {{"KICK", 0},  {"SNARE", 0},          {"TOM", 0},
                                               {"CLHAT", 0}, {"OPHAT", 0},          {"RIDE", "CYMBL"},
                                               {"CRASH", "CYMBL"}, {"CLAP", 0},     {0, 0}};
/* the sound ours of a kind goes as, when the drum's own isn't of it */
static const uint8_t KIND_SENT[K_N] = {0, 10, 18, 23, 26, 28, 30, 46, 31};
/* each drum's sound in a dump made up (no HR-16's came yet): 24" power kick, 8x14" wood snare, closed hi-hat A, half
 * open hi-hat, open hi-hat, hand claps, high conga, low conga, the toms high to low, ride, crash, medium cowbell,
 * tambourine */
static const uint8_t DRUM_SOUND[HD_DRUMS] = {0, 10, 23, 25, 26, 46, 32, 33, 18, 20, 21, 19, 28, 30, 42, 45};

static int starts(const char *s, const char *pre)
{
    while (*pre)
        if (*s++ != *pre++)
            return 0;
    return 1;
}

uint32_t hd_sound_in(uint32_t hr_sound, uint32_t pad)
{
    uint32_t kind = hr_sound < HD_SOUNDS ? kind_of(hr_sound) : K_PERC, k, s;
    char b[HR_NAME + 1];
    for (k = 0; k < 2u && KIND_NAME[kind][k]; k++)
        for (s = 0; s < hr_kit_sounds(); s++) {
            hr_sound_name(s, b);
            if (starts(b, KIND_NAME[kind][k]))
                return s;
        }
    return hr_std[pad % HR_PADS].sound;   /* the kit has none of it: the pad's as set globally */
}

uint32_t hd_sound_out(uint32_t sound, uint32_t drum)
{
    uint32_t base = DRUM_SOUND[drum % HD_DRUMS], kind = K_N, k;
    char b[HR_NAME + 1];
    if (hd_has_header() && sq.hr16_voice[drum % HD_DRUMS] < HD_SOUNDS)
        base = sq.hr16_voice[drum % HD_DRUMS];
    if (sound < hr_kit_sounds()) {
        hr_sound_name(sound, b);
        if (starts(b, "CYMBL"))   /* (the kit's one cymbal: the ride's on the RIDE, else a crash) */
            kind = drum == DRUM_RIDE ? K_RIDE : K_CRASH;
        else
            for (k = 0; k < K_PERC && kind == K_N; k++)
                if (starts(b, KIND_NAME[k][0]))
                    kind = k;
    }
    return kind == K_N || kind_of(base) == kind ? base : KIND_SENT[kind];
}

/* the pitch: 32 steps (bits 0-4, 0 = -16) over an octave and a half, 9/16 of a semitone each */
int32_t hd_tune_in(uint32_t bits)
{
    int32_t v = ((int32_t)(bits & 31u) - 16) * 9;
    return (v >= 0 ? v + 8 : v - 8) / 16;
}

uint32_t hd_tune_out(int32_t semis)
{
    int32_t v = semis * 16, st = (v >= 0 ? v + 4 : v - 4) / 9;
    st = st < -16 ? -16 : st > 15 ? 15 : st;
    return (uint32_t)(st + 16);
}

/* its quantize values (0-9) as their clocks, and as HR_GRID's index (1/6 has none: 1/8) */
static const uint8_t Q_CLOCKS[10] = {96, 64, 48, 32, 24, 16, 12, 8, 6, 1};
static const uint8_t Q_GRID[10] = {0, 1, 1, 2, 3, 4, 5, 6, 7, 8};

/* ---------------------------------------------------------------------------------------------- import --- */
/* a pattern's or song's place in the image from its pointer at ptr (0: none there), with room for hdr bytes */
static uint32_t hd_at(const uint8_t *img, uint32_t len, uint32_t ptr, uint32_t hdr)
{
    uint32_t a = be16(img + ptr);
    if (!(a & 0xFF00u) || a < HD_BASE + HD_HDR)
        return 0;
    a -= HD_BASE;
    return a + hdr <= len ? a : 0u;
}

/* a hit (tick, pad, velocity) into the hits so far (n bytes, in order): its place by tick then pad; the pad's there
 * already, its velocity replaced. The bytes then */
static uint32_t hit_in(uint8_t *h, uint32_t n, uint32_t tick, uint32_t pad, uint32_t vel)
{
    uint32_t a = n, k;
    while (a >= SQ_HIT_LEN) {
        const uint8_t *e = h + a - SQ_HIT_LEN;
        uint32_t t = e[0] | (uint32_t)e[1] << 8;
        if (t < tick || (t == tick && e[2] < pad))
            break;
        if (t == tick && e[2] == pad) {
            h[a - 1u] = (uint8_t)vel;
            return n;
        }
        a -= SQ_HIT_LEN;
    }
    for (k = n; k > a; k--)
        h[k + SQ_HIT_LEN - 1u] = h[k - 1u];
    h[a] = (uint8_t)tick;
    h[a + 1u] = (uint8_t)(tick >> 8);
    h[a + 2u] = (uint8_t)pad;
    h[a + 3u] = (uint8_t)vel;
    return n + SQ_HIT_LEN;
}

/* pattern p from the image at 'at': its hits, length and sounds. 0, 1 empty (no beats), -1 not readable, -2 no room */
static int hd_pattern_in(const uint8_t *img, uint32_t len, uint32_t at, uint32_t p, uint8_t *scratch)
{
    uint32_t size = le16(img + at), end = at + size, k, t = 0, beats, ticks, n = 0, d;
    hr_pad_t pads[HR_PADS];
    if (size < HD_PAT_HDR + 1u || end > len)
        return -1;
    for (k = at + HD_PAT_HDR; k < end && img[k] != 0xFFu; k++)   /* its clocks: the waits */
        if (img[k] & 0x80u)
            t += img[k] & 0x7Fu;
    if (k >= end)
        return -1;                /* no end */
    beats = (t + SQ_PPQN - 1u) / SQ_PPQN;
    if (!beats)
        beats = from_bcd(le16(img + at + 2u));
    if (!beats)
        return 1;                 /* (0 beats: an empty pattern) */
    if (beats > SQ_MAX_BEATS)
        return -1;
    ticks = beats * SQ_PPQN;
    for (k = at + HD_PAT_HDR, t = 0; img[k] != 0xFFu; k++) {
        uint32_t b = img[k];
        if (b & 0x80u) {
            t += b & 0x7Fu;
            continue;
        }
        if (n + SQ_HIT_LEN > SQ_SCRATCH)
            return -2;
        n = hit_in(scratch, n, t % ticks, HD_PAD[b & 15u], (b >> 4 & 7u) * 16u + 15u);
    }
    if (sq_replace(SQ_PAT(p), scratch, n) != 0)
        return -2;
    sq.pattern[p].beats = (uint16_t)beats;
    for (k = 0; k < HR_PADS; k++)
        pads[k] = hr_std[k];
    for (d = 0; d < HD_DRUMS; d++) {   /* its VOICE, TUNE and MIX: the pattern's own sounds */
        const uint8_t *v = img + at + 4u + 3u * d;
        hr_pad_t *x = &pads[HD_PAD[d]];
        uint32_t pan = v[2] >> 5;
        x->sound = (uint8_t)hd_sound_in(v[0], HD_PAD[d]);
        x->level = (uint8_t)((v[1] & 0x7Fu) > 99u ? 99u : v[1] & 0x7Fu);
        x->pan = (int8_t)((pan > 6u ? 6 : (int32_t)pan) - 3);
        x->tune = (int8_t)hd_tune_in(v[2]);
    }
    return hr_sounds_put(p, pads) == 0 ? 0 : -2;
}

int hd_import(const uint8_t *img, uint32_t len, uint8_t *scratch, hd_report_t *r)
{
    static uint8_t count[HD_DRUMS][HD_SOUNDS];
    uint32_t p, s, d, i;
    r->patterns = r->lost = r->songs = 0;
    r->first_lost = -1;
    r->quant = r->swing = -1;
    if (len < HD_HDR)
        return -1;
    for (p = 0; p < SQ_PATTERNS; p++) {   /* (all of them replaced, as on the HR-16) */
        sq.pattern[p].beats = 0;
        (void)sq_replace(SQ_PAT(p), 0, 0);
        (void)sq_replace(SQ_PSND(p), 0, 0);
    }
    for (i = 0; i < HD_HDR; i++)
        sq.hr16[i] = img[i];
    /* the sound most of its patterns have on each drum: what ours sends for a pad of that kind */
    for (d = 0; d < HD_DRUMS; d++)
        for (i = 0; i < HD_SOUNDS; i++)
            count[d][i] = 0;
    for (p = 0; p < SQ_PATTERNS; p++) {
        uint32_t at = hd_at(img, len, 0x200u + 2u * p, HD_PAT_HDR);
        for (d = 0; at && d < HD_DRUMS; d++)
            if (img[at + 4u + 3u * d] < HD_SOUNDS && count[d][img[at + 4u + 3u * d]] < 0xFFu)
                count[d][img[at + 4u + 3u * d]]++;
    }
    for (d = 0; d < HD_DRUMS; d++) {
        uint32_t best = DRUM_SOUND[d];
        for (i = 0; i < HD_SOUNDS; i++)
            if (count[d][i] > count[d][best])
                best = i;
        sq.hr16_voice[d] = (uint8_t)best;
    }
    for (p = 0; p < SQ_PATTERNS; p++) {
        uint32_t at = hd_at(img, len, 0x200u + 2u * p, HD_PAT_HDR);
        int e;
        if (!at)
            continue;
        e = hd_pattern_in(img, len, at, p, scratch);
        if (e == 0) {
            r->patterns++;
        } else if (e < 0) {       /* what of it went in, out again */
            sq.pattern[p].beats = 0;
            (void)sq_replace(SQ_PAT(p), 0, 0);
            (void)sq_replace(SQ_PSND(p), 0, 0);
            r->lost++;
            if (r->first_lost < 0)
                r->first_lost = (int16_t)p;
        }
    }
    for (s = 0; s < 100u; s++) {  /* its songs: counted (one with a step) */
        uint32_t at = hd_at(img, len, 0x302u + 2u * s, 3u);
        if (at && img[at + 2u] != 0xFFu)
            r->songs++;
    }
    if (img[0x2DAu] < 10u)
        r->quant = (int8_t)Q_GRID[img[0x2DAu]];
    if (img[0x2D5u] <= 24u)
        r->swing = (int8_t)(50u + img[0x2D5u]);
    return 0;
}

/* ---------------------------------------------------------------------------------------------- export --- */
/* clocks to wait: 126 at most a byte */
static int wait_out(uint8_t *img, uint32_t *pos, uint32_t max, uint32_t clocks)
{
    while (clocks) {
        uint32_t c = clocks > 126u ? 126u : clocks;
        if (*pos + 1u > max)
            return -1;
        img[(*pos)++] = (uint8_t)(0x80u | c);
        clocks -= c;
    }
    return 0;
}

/* a header of our own (no HR-16's came yet): its settings as ours, the two bytes its manual gives */
static void header_made_up(uint8_t *img, const hd_settings_t *st)
{
    uint32_t i;
    for (i = 0; i < HD_HDR; i++)
        img[i] = 0;
    img[0xC8] = st->channel & 15u;
    img[0xC9] = img[0xCA] = 1;    /* drum triggers in and out */
    for (i = 0; i < HD_DRUMS; i++)
        img[0xCCu + i] = hr_std[HD_PAD[i]].note & 0x7Fu;
    img[0xDD] = 1;                /* program change selects a pattern */
    img[0xDF] = img[0xE0] = 1;    /* clock out, auto start */
    img[0xE4] = 5;                /* pad dynamics */
    img[0xE6] = 1;                /* SysEx on */
    img[0xFE] = 0x27;
    img[0xFF] = 0xB5;
    img[0x2D8] = (uint8_t)(st->tempo < 20u ? 20u : st->tempo > 255u ? 255u : st->tempo);
}

int32_t hd_export(uint8_t *img, uint32_t max, const hd_settings_t *st)
{
    uint32_t pos = HD_HDR, p, i, d, q, drum_of[HR_PADS];
    hr_pad_t pads[HR_PADS];
    if (max > HD_MAX)
        max = HD_MAX;
    if (max < HD_HDR + 3u)
        return -1;
    if (hd_has_header())
        for (i = 0; i < HD_HDR; i++)   /* the header of the HR-16's own dump, its pointers made again */
            img[i] = sq.hr16[i];
    else
        header_made_up(img, st);
    for (i = 0; i < 2u * SQ_PATTERNS; i++)
        img[0x200u + i] = 0;
    for (i = 0; i < 200u; i++)
        img[0x302u + i] = 0;
    for (d = 0; d < HD_DRUMS; d++)
        drum_of[HD_PAD[d]] = d;
    for (p = 0; p < SQ_PATTERNS; p++) {
        uint32_t at = pos, beats = hr_beats(p), ticks = beats * SQ_PPQN, last = 0, n = hr_hits(p);
        if (!n && !sq.pattern[p].beats && !hr_sounds_own(p))
            continue;             /* an empty pattern: no data, its pointer 0 */
        if (pos + HD_PAT_HDR > max)
            return -1;
        put_be16(img + 0x200u + 2u * p, at + HD_BASE);
        put_le16(img + at + 2u, to_bcd(beats));
        hr_sounds_get(p, pads);
        for (d = 0; d < HD_DRUMS; d++) {
            const hr_pad_t *x = &pads[HD_PAD[d]];
            img[at + 4u + 3u * d] = (uint8_t)hd_sound_out(x->sound, d);
            img[at + 5u + 3u * d] = (uint8_t)(x->level > 99u ? 99u : x->level);   /* (output 1) */
            img[at + 6u + 3u * d] = (uint8_t)((uint32_t)(x->pan + HR_PAN_MAX) << 5 | hd_tune_out(x->tune));
        }
        pos += HD_PAT_HDR;
        for (i = 0; i < n; i++) {
            const uint8_t *h = hr_hit_at(p, i);
            uint32_t t = hr_hit_tick(h), dyn = h[3] ? (h[3] - 1u) / 16u : 0u;
            if (t >= ticks)
                continue;
            if (wait_out(img, &pos, max, t - last) != 0 || pos + 1u > max)
                return -1;
            last = t;
            img[pos++] = (uint8_t)((dyn > 7u ? 7u : dyn) << 4 | drum_of[h[2] % HR_PADS]);
        }
        if (wait_out(img, &pos, max, ticks - last) != 0 || pos + 1u > max)
            return -1;
        img[pos++] = 0xFFu;
        put_le16(img + at, pos - at);
    }
    if (pos + 3u > max)           /* song 99, empty, at the end: an HR-16 has to have it */
        return -1;
    put_be16(img + 0x302u + 2u * 99u, pos + HD_BASE);
    img[pos] = 3;
    img[pos + 1u] = 0;
    img[pos + 2u] = 0xFFu;
    pos += 3u;
    put_le16(img + 0x2CDu, pos + HD_BASE);   /* the free memory: where it starts, how much */
    put_le16(img + 0x2D1u, HD_END - (pos + HD_BASE));
    img[0x2D5] = (uint8_t)(st->swing < 50u ? 0u : st->swing - 50u > 24u ? 24u : st->swing - 50u);
    for (q = 0; q < 10u && (Q_CLOCKS[q] != HR_GRID[st->quant % HR_GRIDS]); q++)
        ;
    q = q < 10u ? q : 4u;
    img[0x2D9] = Q_CLOCKS[q];
    img[0x2DA] = (uint8_t)q;
    return (int32_t)pos;
}
