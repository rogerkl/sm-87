/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the HR-16's memory dump, see hrdump.h */
#include "hrdump.h"

const uint8_t HD_PRE[HD_PREFIX] = {0xF0, 0x00, 0x00, 0x0E, HD_ID, HD_CMD_DUMP};
const uint8_t HD_PAD[HD_DRUMS] = {0, 1, 2, 6, 3, 7, 14, 15, 8, 9, 10, 11, 5, 4, 12, 13};

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
/* its drums, in its order (HD_PAD: our pads) */
enum { KICK, SNARE, CLS, MID, OPN, CLAPS, PERC3, PERC4, TOM1, TOM2, TOM3, TOM4, RIDE, CRASH, PERC1, PERC2, NO = 0xFF };
/* each of its sounds' drum: the one whose sound it is as an HR-16 sets its drums up (KICK the 24" power kick, PERC 3
 * the tambourine ..), else the drum of its kind (any kick the KICK's, a tom by its size, the closed hats CLS HAT's,
 * a conga the conga's); NO, a sound no drum has (a cowbell, the drum sticks). Ours plays it as its drum's pad: the
 * HR-16's 49 (V1.09, documentation/hr16-sysex.md) */
static const uint8_t HOME[HD_SOUNDS] = {
    KICK,  KICK,  KICK,  KICK,  KICK,  KICK,  KICK,  KICK,  KICK,  KICK,    /* 00-09 the kicks */
    SNARE, SNARE, SNARE, SNARE, SNARE, SNARE, SNARE, SNARE,                 /* 10-17 snares, rimshot, sidestick .. */
    TOM1,  TOM3,  TOM1,  TOM2,  TOM4,                                       /* 18-22 10", 16", 10", 14", electronic */
    CLS,   CLS,   MID,   OPN,   CLS,                                        /* 23-27 closed A, B, half open, open .. */
    RIDE,  RIDE,  CRASH,                                                    /* 28-30 ride, its bell, crash */
    PERC1, PERC2, PERC2,                                                    /* 31-33 timbale, high and low conga slap */
    NO,    NO,    NO,    NO,    NO,    NO,    NO,    NO,    NO,    NO,      /* 34-43 blocks .. cowbells */
    PERC4, PERC3, CLAPS, NO,    NO};                                        /* 44-48 triangle, tambourine, claps, .. */
/* the HR-16:B's 49 (V2.00: other sounds in another order, HR16_FORMAT.md) on the same drums, by the HR-16's kinds */
static const uint8_t HOME_B[HD_SOUNDS] = {
    KICK,  KICK,  KICK,  KICK,  KICK,  KICK,  KICK,                         /* 00-06 the kicks */
    SNARE, SNARE, SNARE, SNARE, SNARE, SNARE, SNARE, SNARE, SNARE, SNARE,   /* 07-16 snares */
    SNARE, SNARE,                                                           /* 17-18 sidesticks */
    CLS,   MID,   OPN,   CLS,   MID,                                        /* 19-23 closed, mid, open, R&B, rock */
    TOM1,  TOM2,  TOM4,  CRASH, RIDE,                                       /* 24-28 three toms, crash, ride */
    PERC2, PERC2, NO,    NO,    NO,    PERC3, CLAPS,                        /* 29-35 congas, bongo, cabasas, tambourine,
                                                                             * claps */
    NO,    NO,    NO,    NO,    NO,    NO,    NO,    NO,    NO,    NO,      /* 36-45 agogo .. glass break */
    NO,    NO,    NO};                                                      /* 46-48 jam blocks */
/* each drum's sound as an HR-16 sets it up, a new pattern's (V1.09 at 153DH; the B's at 6BC5H) */
static const uint8_t DEF[HD_DRUMS] = {0, 11, 23, 25, 26, 46, 45, 44, 20, 21, 19, 22, 28, 30, 31, 33};
static const uint8_t DEF_B[HD_DRUMS] = {0, 8, 19, 20, 21, 35, 39, 45, 24, 25, 25, 26, 28, 27, 41, 43};

int hd_is_b(const uint8_t *hdr) { return hdr[0xE7] == 'B'; }
static int b_now(void) { return hd_has_header() && hd_is_b(sq.hr16); }

/* the sound drum d's pad has: the kit's set-up's, else (none there) the global set-up's in std */
static uint32_t drum_sound(uint32_t d, const hr_pad_t *std)
{
    uint32_t s = hr_kit_pad_sound(HD_PAD[d]);
    return s != HR_NO_SOUND ? s : std[HD_PAD[d]].sound;
}

uint32_t hd_sound_in(uint32_t hr_sound, int b, const hr_pad_t *std)
{
    uint32_t d = hr_sound < HD_SOUNDS ? (b ? HOME_B : HOME)[hr_sound] : NO;
    return d == NO ? HR_NO_SOUND : drum_sound(d, std);
}

uint32_t hd_sound_out(const hr_pad_t *x, uint32_t drum)
{
    int b = b_now();
    uint32_t e = drum % HD_DRUMS, k, v = sq.hr16_voice[e];
    if (x->hr16 && x->hr16 <= HD_SOUNDS)
        return x->hr16 - 1u;      /* (as it came) */
    if (x->sound != HR_NO_SOUND && drum_sound(e, hr_std) != x->sound)
        for (k = 0; k < HD_DRUMS; k++)   /* the drum whose pad has it (its own first) */
            if (drum_sound(k, hr_std) == x->sound) {
                e = k;
                v = sq.hr16_voice[e];
                break;
            }
    return hd_has_header() && v < HD_SOUNDS && (b ? HOME_B : HOME)[v] == e ? v : (b ? DEF_B : DEF)[e];
}

/* the pitch: 32 steps (bits 0-4, 0 = -16) over an octave and a half, 9/16 of a semitone each: as sixteenths */
int32_t hd_tune_in(uint32_t bits) { return ((int32_t)(bits & 31u) - 16) * 9; }

uint32_t hd_tune_out(int32_t sixteenths)
{
    int32_t st = (sixteenths >= 0 ? sixteenths + 4 : sixteenths - 4) / 9;
    st = st < -16 ? -16 : st > 15 ? 15 : st;
    return (uint32_t)(st + 16);
}

void hd_tune_split(int32_t sixteenths, hr_pad_t *x)
{
    int32_t a = sixteenths < 0 ? -sixteenths : sixteenths, t = (a + 8) / 16;   /* the nearest semitone, FINE the rest */
    x->tune = (int8_t)(sixteenths < 0 ? -t : t);
    x->fine = (int8_t)(sixteenths - x->tune * 16);
}

/* its quantize values (0-9) as their clocks, and as HR_GRID's index (1/6 has none: 1/8) */
static const uint8_t Q_CLOCKS[10] = {96, 64, 48, 32, 24, 16, 12, 8, 6, 1};
static const uint8_t Q_GRID[10] = {0, 1, 1, 2, 3, 4, 5, 6, 7, 8};

/* ---------------------------------------------------------------------------------------------- import --- */
/* a pattern's or song's place in the image from its pointer at ptr (0: none there), with room for hdr bytes */
uint32_t hd_want(const uint8_t *img, uint32_t n)
{
    uint32_t a;
    if (n < 0x2CFu)
        return 0;
    a = le16(img + 0x2CDu);
    return a >= HD_BASE + HD_HDR && a <= HD_END ? a - HD_BASE : 0u;
}

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

/* a dump's taking in: an HR-16:B's, the global set-up as it was before (its sounds mapped through it) */
static int b_in;
static hr_pad_t std_in[HR_PADS];

/* a drum's VOICE, TUNE and MIX (v: its sound, level and output, pan and pitch) as pad x's */
static void voice_in(const uint8_t *v, hr_pad_t *x)
{
    uint32_t pan = v[2] >> 5;
    x->sound = (uint8_t)hd_sound_in(v[0], b_in, std_in);
    x->hr16 = (uint8_t)(v[0] < HD_SOUNDS ? v[0] + 1u : 0u);
    x->level = (uint8_t)((v[1] & 0x7Fu) > 99u ? 99u : v[1] & 0x7Fu);
    x->pan = (int8_t)((pan > 6u ? 6 : (int32_t)pan) - 3);
    hd_tune_split(hd_tune_in(v[2]), x);
}

/* a pad's VOICE, TUNE and MIX as a drum's, into v */
static void voice_out(const hr_pad_t *x, uint32_t d, uint8_t *v)
{
    v[0] = (uint8_t)hd_sound_out(x, d);
    v[1] = (uint8_t)(x->level > 99u ? 99u : x->level);   /* (output 1) */
    v[2] = (uint8_t)((uint32_t)(x->pan + HR_PAN_MAX) << 5 | hd_tune_out(x->tune * 16 + x->fine));
}

/* pattern p from the image at 'at': its hits, length and sounds. 0, 1 empty (no beats), -1 not readable, -2 no room,
 * -3 past the len bytes that came */
static int hd_pattern_in(const uint8_t *img, uint32_t len, uint32_t at, uint32_t p, uint8_t *scratch)
{
    uint32_t size = le16(img + at), end = at + size, k, t = 0, beats, ticks, n = 0, d;
    hr_pad_t pads[HR_PADS];
    if (size < HD_PAT_HDR + 1u)
        return -1;
    if (end > len)
        return -3;
    for (k = at + HD_PAT_HDR; k < end && img[k] != 0xFFu; k++)   /* its clocks: the waits */
        if (img[k] & 0x80u)
            t += img[k] & 0x7Fu;
    if (k >= end)
        return -1;                /* no end */
    beats = (t + SQ_PPQN - 1u) / SQ_PPQN;
    if (!beats) {                 /* (no waits: its length as said, the beats + 1 in BCD, as the HR-16's dumps have
                                   * it: 16 beats 0017H; 0 or 1, none) */
        beats = from_bcd(le16(img + at + 2u));
        beats = beats ? beats - 1u : 0u;
    }
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
    for (d = 0; d < HD_DRUMS; d++)   /* its VOICE, TUNE and MIX: the pattern's own sounds */
        voice_in(img + at + 4u + 3u * d, &pads[HD_PAD[d]]);
    return hr_sounds_put(p, pads) == 0 ? 0 : -2;
}

int hd_import(const uint8_t *img, uint32_t len, uint8_t *scratch, hd_report_t *r)
{
    static uint8_t count[HD_DRUMS][HD_SOUNDS];
    uint32_t p, s, d, i, short_;
    r->patterns = r->lost = r->songs = 0;
    r->first_lost = -1;
    r->quant = r->swing = r->prog = r->manual = -1;
    r->cut = r->bad = r->full = 0;
    r->got = len;
    r->want = 0;
    if (len < HD_HDR)
        return -1;
    r->want = hd_want(img, len);
    short_ = r->want && len < r->want;
    for (p = 0; p < SQ_PATTERNS; p++) {   /* (all of them replaced, as on the HR-16) */
        sq.pattern[p].beats = 0;
        (void)sq_replace(SQ_PAT(p), 0, 0);
        (void)sq_replace(SQ_PSND(p), 0, 0);
    }
    for (i = 0; i < HD_HDR; i++)
        sq.hr16[i] = img[i];
    b_in = hd_is_b(img);
    for (i = 0; i < HR_PADS; i++)
        std_in[i] = hr_std[i];
    if (img[0xE3u] <= 1u)
        r->manual = (int8_t)img[0xE3u];
    if (r->manual == 1)           /* MANUAL VOICE/TUNE/MIX on: every pattern plays the kit it has now, the global
                                   * set-up's */
        for (d = 0; d < HD_DRUMS; d++) {
            voice_in(img + 0x103u + 3u * d, &hr_std[HD_PAD[d]]);
            hr_pad[HD_PAD[d]] = hr_std[HD_PAD[d]];
        }
    for (d = 1; d < HD_DRUMS && img[0xCCu + d] == img[0xCCu]; d++)
        ;
    for (i = 0; d < HD_DRUMS && i < HD_DRUMS; i++) {   /* the drums' MIDI notes, the pads' (the global set-up's; all
                                                         * 16 on one, a dump made without them: not) */
        hr_std[HD_PAD[i]].note = img[0xCCu + i] & 0x7Fu;
        hr_pad[HD_PAD[i]].note = hr_std[HD_PAD[i]].note;
    }
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
        uint32_t best = (b_in ? DEF_B : DEF)[d];
        for (i = 0; i < HD_SOUNDS; i++)
            if (count[d][i] > count[d][best])
                best = i;
        sq.hr16_voice[d] = (uint8_t)best;
    }
    for (p = 0; p < SQ_PATTERNS; p++) {
        uint32_t at = hd_at(img, len, 0x200u + 2u * p, HD_PAT_HDR);
        int e;
        if (!at) {
            uint32_t a = be16(img + 0x200u + 2u * p);
            if (!(a & 0xFF00u) || a < HD_BASE + HD_HDR)
                continue;         /* none there */
            e = -3;               /* past the bytes that came */
        } else {
            e = hd_pattern_in(img, len, at, p, scratch);
        }
        if (e == 0) {
            r->patterns++;
        } else if (e < 0) {       /* what of it went in, out again */
            sq.pattern[p].beats = 0;
            (void)sq_replace(SQ_PAT(p), 0, 0);
            (void)sq_replace(SQ_PSND(p), 0, 0);
            r->lost++;
            if (r->first_lost < 0)
                r->first_lost = (int16_t)p;
            if (e == -2)
                r->full = 1;
            else if (e == -3)
                r->cut = 1;
            else
                r->bad = 1;
        }
    }
    /* as the MMT-8's: fewer bytes than its header says, what was not readable lost with the rest; as many, what lies
     * past them a damaged dump's */
    if (short_ && r->bad)
        r->cut = 1, r->bad = 0;
    else if (r->want && !short_ && r->cut)
        r->bad = 1, r->cut = 0;
    for (s = 0; s < 100u; s++) {  /* its songs: counted (one with a step) */
        uint32_t at = hd_at(img, len, 0x302u + 2u * s, 3u);
        if (at && img[at + 2u] != 0xFFu)
            r->songs++;
    }
    if (img[0x2DAu] < 10u)
        r->quant = (int8_t)Q_GRID[img[0x2DAu]];
    if (img[0x2D5u] <= 24u)
        r->swing = (int8_t)img[0x2D5u];
    if (img[0xDDu] <= 1u)
        r->prog = (int8_t)img[0xDDu];
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
    img[0xCB] = 34;               /* the click's note, an HR-16's (A#0) */
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
    img[0xDD] = st->prog ? 1u : 0u;   /* MIDI PROGRAM CHANGE: patterns chosen by program change */
    img[0xE3] = st->manual ? 1u : 0u; /* MANUAL VOICE/TUNE/MIX: on, the kit it has now (the global set-up) for all */
    if (st->manual)
        for (d = 0; d < HD_DRUMS; d++)
            voice_out(&hr_std[HD_PAD[d]], d, img + 0x103u + 3u * d);
    for (i = 0; i < HD_DRUMS; i++)    /* the drums' notes: the pads' (a dump's came to them) */
        img[0xCCu + i] = hr_std[HD_PAD[i]].note & 0x7Fu;
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
        put_le16(img + at + 2u, to_bcd(beats + 1u));   /* (the beats + 1, as an HR-16 writes them) */
        hr_sounds_get(p, pads);
        for (d = 0; d < HD_DRUMS; d++)
            voice_out(&pads[HD_PAD[d]], d, img + at + 4u + 3u * d);
        pos += HD_PAT_HDR;
        for (i = 0; i < n; i++) {
            const uint8_t *h = hr_hit_at(p, i);
            uint32_t t = hr_hit_tick(h), dyn = h[3] ? (h[3] - 1u) / 16u : 0u;
            if (t >= ticks)
                continue;
            if (wait_out(img, &pos, max, t - last) != 0 || pos + 2u > max)
                return -1;
            if (pos == at + HD_PAT_HDR)   /* (its events start with a wait, as an HR-16's do: 80H, none, for a hit on
                                           * the first clock) */
                img[pos++] = 0x80u;
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
    img[0x2D5] = (uint8_t)(st->swing > 24u ? 24u : st->swing);
    for (q = 0; q < 10u && (Q_CLOCKS[q] != HR_GRID[st->quant % HR_GRIDS]); q++)
        ;
    q = q < 10u ? q : 4u;
    img[0x2D9] = Q_CLOCKS[q];
    img[0x2DA] = (uint8_t)q;
    return (int32_t)pos;
}
