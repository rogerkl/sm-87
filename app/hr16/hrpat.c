/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the HR-16's patterns and their player, see hr.h. A pattern is a blob of the sequencer's memory (seq.h
 * SQ_PAT), its hits in time order and by pad: tick (2 bytes, little-endian), pad, velocity. */
#include "hr.h"
#include "../mmt8/player.h"

const uint8_t HR_GRID[HR_GRIDS] = {96, 48, 32, 24, 16, 12, 8, 6, 1};
const char *const HR_GRID_TXT[HR_GRIDS] = {"1/4", "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/64", "OFF"};

uint32_t hr_beats(uint32_t p) { return sq.pattern[p].beats ? sq.pattern[p].beats : HR_DEFAULT_BEATS; }
uint32_t hr_hits(uint32_t p) { return sq_len(SQ_PAT(p)) / SQ_HIT_LEN; }

const uint8_t *hr_hit_at(uint32_t p, uint32_t i)
{
    return i < hr_hits(p) ? sq_data(SQ_PAT(p)) + SQ_HIT_LEN * i : 0;
}

static void copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
}

/* the first hit at (tick, pad) or after it */
static uint32_t find(uint32_t p, uint32_t tick, uint32_t pad)
{
    const uint8_t *d = sq_data(SQ_PAT(p));
    uint32_t len = sq_len(SQ_PAT(p)), a = 0;
    while (a < len && (hr_hit_tick(d + a) < tick || (hr_hit_tick(d + a) == tick && d[a + 2u] < pad)))
        a += SQ_HIT_LEN;
    return a;
}

int hr_add(uint32_t p, uint32_t tick, uint32_t pad, uint32_t vel, uint8_t *scratch)
{
    uint32_t len = sq_len(SQ_PAT(p)), a = find(p, tick, pad);
    const uint8_t *d = sq_data(SQ_PAT(p));
    if (p >= SQ_PATTERNS || pad >= HR_PADS || !vel)
        return -1;
    if (a < len && hr_hit_tick(d + a) == tick && d[a + 2u] == pad) {
        sq.arena[sq.blob[SQ_PAT(p)].off + a + 3u] = (uint8_t)vel;   /* (in place: one byte) */
        return 0;
    }
    if (len + SQ_HIT_LEN > SQ_SCRATCH)
        return -1;
    copy(scratch, d, a);
    scratch[a] = (uint8_t)tick;
    scratch[a + 1u] = (uint8_t)(tick >> 8);
    scratch[a + 2u] = (uint8_t)pad;
    scratch[a + 3u] = (uint8_t)vel;
    copy(scratch + a + SQ_HIT_LEN, d + a, len - a);
    return sq_replace(SQ_PAT(p), scratch, len + SQ_HIT_LEN);
}

int hr_remove(uint32_t p, uint32_t tick, uint32_t pad, uint8_t *scratch)
{
    uint32_t len = sq_len(SQ_PAT(p)), a = find(p, tick, pad);
    const uint8_t *d = sq_data(SQ_PAT(p));
    if (a >= len || hr_hit_tick(d + a) != tick || d[a + 2u] != pad)
        return 0;
    copy(scratch, d, a);
    copy(scratch + a, d + a + SQ_HIT_LEN, len - a - SQ_HIT_LEN);
    return sq_replace(SQ_PAT(p), scratch, len - SQ_HIT_LEN);
}

/* the hits kept: of another pad than drop_pad (HR_PADS: none dropped), before ticks */
static int keep(uint32_t p, uint32_t drop_pad, uint32_t ticks, uint8_t *scratch)
{
    const uint8_t *d = sq_data(SQ_PAT(p));
    uint32_t len = sq_len(SQ_PAT(p)), a, n = 0;
    for (a = 0; a < len; a += SQ_HIT_LEN)
        if (d[a + 2u] != drop_pad && hr_hit_tick(d + a) < ticks) {
            copy(scratch + n, d + a, SQ_HIT_LEN);
            n += SQ_HIT_LEN;
        }
    return n == len ? 0 : sq_replace(SQ_PAT(p), scratch, n);
}

int hr_erase(uint32_t p, uint32_t pad, uint8_t *scratch)
{
    if (pad < HR_PADS)
        return keep(p, pad, SQ_MAX_BEATS * SQ_PPQN, scratch);   /* (every tick a pattern has) */
    sq.pattern[p].beats = 0;
    return sq_replace(SQ_PAT(p), 0, 0);
}

int hr_set_length(uint32_t p, uint32_t beats, uint8_t *scratch)
{
    int r;
    if (beats < 1u || beats > SQ_MAX_BEATS)
        return -1;
    r = keep(p, HR_PADS, beats * SQ_PPQN, scratch);
    if (r == 0)
        sq.pattern[p].beats = (uint16_t)beats;
    return r;
}

int hr_copy(uint32_t a, uint32_t b, uint8_t *scratch)
{
    uint32_t na = sq_len(SQ_PAT(a)), nb = sq_len(SQ_PAT(b)), off, k, beats;
    const uint8_t *d;
    if (!na && !sq.pattern[a].beats)
        return SQ_E_NONE;
    if (!nb) {                    /* b without hits: a copy of a, its sounds too (the HR-16's) */
        uint32_t snd = hr_sounds_own(a) ? SQ_SOUNDS_LEN : 0u;
        if (a == b)
            return 0;
        if (na + snd > sq_free() + sq_len(SQ_PSND(b)))
            return SQ_E_ROOM;     /* (all or nothing) */
        sq.pattern[b].beats = sq.pattern[a].beats;
        copy(scratch, sq_data(SQ_PAT(a)), na);
        if (sq_replace(SQ_PAT(b), scratch, na) != 0 || hr_sounds_copy(a, b) != 0)
            return SQ_E_ROOM;
        return 0;
    }
    beats = hr_beats(a) + hr_beats(b);
    if (beats > SQ_MAX_BEATS)
        return SQ_E_LONG;
    if (na + nb > SQ_SCRATCH)
        return SQ_E_ROOM;
    off = hr_beats(b) * SQ_PPQN;
    copy(scratch, sq_data(SQ_PAT(b)), nb);
    d = sq_data(SQ_PAT(a));
    for (k = 0; k < na; k += SQ_HIT_LEN) {   /* a's after b's: later, so still in order */
        uint32_t t = hr_hit_tick(d + k) + off;
        scratch[nb + k] = (uint8_t)t;
        scratch[nb + k + 1u] = (uint8_t)(t >> 8);
        scratch[nb + k + 2u] = d[k + 2u];
        scratch[nb + k + 3u] = d[k + 3u];
    }
    if (sq_replace(SQ_PAT(b), scratch, na + nb) != 0)
        return SQ_E_ROOM;
    sq.pattern[b].beats = (uint16_t)beats;
    return 0;
}

/* a pair of grid places' second one, from the pair's start: halfway at 50%, later swung */
static uint32_t swung(uint32_t pair, uint32_t swing)
{
    swing = swing < 50u ? 50u : swing > HR_SWING_MAX ? HR_SWING_MAX : swing;
    return (pair * swing + 50u) / 100u;
}

uint32_t hr_quantize(uint32_t tick, uint32_t grid, uint32_t swing)
{
    uint32_t pair = 2u * grid, base, r, sw;
    if (grid <= 1u)
        return tick;
    base = tick / pair * pair;
    r = tick - base;
    sw = swung(pair, swing);
    return base + (2u * r < sw ? 0u : 2u * r < sw + pair ? sw : pair);
}

/* ---------------------------------------------------------------------------------------------- step edit --- */
/* the grid's first place after tick, and its last before it (tick > 0: 0 is one) */
static uint32_t grid_next(uint32_t tick, uint32_t grid, uint32_t swing)
{
    uint32_t pair = 2u * grid, base, sw;
    if (grid <= 1u)
        return tick + 1u;
    base = tick / pair * pair;
    sw = swung(pair, swing);
    return base + sw > tick ? base + sw : base + pair;
}

static uint32_t grid_prev(uint32_t tick, uint32_t grid, uint32_t swing)
{
    uint32_t pair = 2u * grid, base, sw;
    if (grid <= 1u)
        return tick - 1u;
    base = (tick - 1u) / pair * pair;
    sw = swung(pair, swing);
    return tick - 1u >= base + sw ? base + sw : base;
}

uint32_t hr_step_next(uint32_t p, uint32_t tick, uint32_t grid, uint32_t swing)
{
    uint32_t ticks = hr_beats(p) * SQ_PPQN, n = grid_next(tick, grid, swing), a = find(p, tick + 1u, 0);
    if (a < sq_len(SQ_PAT(p)) && hr_hit_tick(sq_data(SQ_PAT(p)) + a) < n)
        n = hr_hit_tick(sq_data(SQ_PAT(p)) + a);   /* a hit before the next grid place */
    return n >= ticks ? 0u : n;
}

uint32_t hr_step_prev(uint32_t p, uint32_t tick, uint32_t grid, uint32_t swing)
{
    uint32_t ticks = hr_beats(p) * SQ_PPQN, n, a;
    if (tick == 0u || tick > ticks)
        tick = ticks;             /* from the start: round to the end */
    n = grid_prev(tick, grid, swing);
    a = find(p, tick, 0);         /* the hit before it, if later than n */
    if (a > 0u && hr_hit_tick(sq_data(SQ_PAT(p)) + a - SQ_HIT_LEN) > n)
        n = hr_hit_tick(sq_data(SQ_PAT(p)) + a - SQ_HIT_LEN);
    return n;
}

uint32_t hr_hits_at(uint32_t p, uint32_t tick, uint32_t *first)
{
    const uint8_t *d = sq_data(SQ_PAT(p));
    uint32_t len = sq_len(SQ_PAT(p)), a = find(p, tick, 0), n = 0;
    *first = a / SQ_HIT_LEN;
    while (a < len && hr_hit_tick(d + a) == tick)
        a += SQ_HIT_LEN, n++;
    return n;
}

int hr_move(uint32_t p, uint32_t tick, uint32_t pad, uint32_t to_tick, uint32_t to_pad, uint8_t *scratch)
{
    uint32_t a = find(p, tick, pad), vel;
    const uint8_t *d = sq_data(SQ_PAT(p));
    if (a >= sq_len(SQ_PAT(p)) || hr_hit_tick(d + a) != tick || d[a + 2u] != pad)
        return 0;                 /* (no such hit) */
    if (to_tick == tick && to_pad == pad)
        return 0;
    vel = d[a + 3u];
    if (hr_remove(p, tick, pad, scratch) != 0)
        return -1;
    return hr_add(p, to_tick, to_pad, vel, scratch);
}

/* ------------------------------------------------------------------------------------------------ OFFSET --- */
int hr_offset(uint32_t p, uint32_t pad, int32_t clocks, uint8_t *scratch)
{
    const uint8_t *d = sq_data(SQ_PAT(p));
    uint32_t len = sq_len(SQ_PAT(p)), ticks = hr_beats(p) * SQ_PPQN, a, i;
    if (!len || !clocks)
        return 0;
    if (len > SQ_SCRATCH)
        return -1;
    for (a = 0; a < len; a += SQ_HIT_LEN) {   /* the hits moved (round at the ends), then in order again */
        int32_t t = (int32_t)hr_hit_tick(d + a);
        if (pad >= HR_PADS || d[a + 2u] == pad) {
            t = (t + clocks) % (int32_t)ticks;
            if (t < 0)
                t += (int32_t)ticks;
        }
        scratch[a] = (uint8_t)t;
        scratch[a + 1u] = (uint8_t)((uint32_t)t >> 8);
        scratch[a + 2u] = d[a + 2u];
        scratch[a + 3u] = d[a + 3u];
    }
    for (a = SQ_HIT_LEN; a < len; a += SQ_HIT_LEN) {   /* (all but sorted: a pad's hits moved past the others near) */
        uint8_t h[SQ_HIT_LEN];
        uint32_t key = hr_hit_tick(scratch + a) << 8 | scratch[a + 2u];
        for (i = 0; i < SQ_HIT_LEN; i++)
            h[i] = scratch[a + i];
        for (i = a; i >= SQ_HIT_LEN && (hr_hit_tick(scratch + i - SQ_HIT_LEN) << 8 | scratch[i - SQ_HIT_LEN + 2u]) > key;
             i -= SQ_HIT_LEN)
            copy(scratch + i, scratch + i - SQ_HIT_LEN, SQ_HIT_LEN);
        copy(scratch + i, h, SQ_HIT_LEN);
    }
    return sq_replace(SQ_PAT(p), scratch, len);   /* (a pad's hits all move: none lands on another of its own) */
}

/* ------------------------------------------------------------------------------------------- the player --- */
volatile hrp_t hrp = {.next = 0xFFu, .quant = 3u, .swing = 50u, .playing = 0xFFu};

static uint32_t cur;              /* the next hit to play, an offset in the pattern playing */
static uint8_t cur_p = 0xFFu;     /* the pattern it is in (0xFF: none played yet) */
static int32_t cur_t = -1;        /* the clock played last */
static uint8_t moved = 1;         /* the patterns changed: cur found again */

/* hits played ahead of their place (quantized later than played): not played again as the pattern gets there */
#define SKIPS 8u
static struct {
    uint32_t clock;
    uint8_t pad;
} skip[SKIPS];
static uint32_t n_skip;
static uint8_t pre_start;         /* a hit came between the start and its first tick: its skip kept */

/* FILL: the pads held repeating, the velocity each was hit at, the clock after which its repeats come */
static uint16_t fill_mask;
static uint8_t fill_vel[HR_PADS];
static uint32_t fill_after[HR_PADS];

/* FILL's rate: the quantize grid (off: 1/32) */
static uint32_t fill_grid(void)
{
    uint32_t g = HR_GRID[hrp.quant % HR_GRIDS];
    return g > 1u ? g : 12u;
}

#define RECQ 64u
static hr_rec_t recq[RECQ];
static volatile uint32_t recq_w, recq_r;

static void rec_put(uint32_t p, uint32_t tick, uint32_t pad, uint32_t vel, uint32_t kind)
{
    hr_rec_t *r;
    if (recq_w - recq_r >= RECQ)
        return;
    r = &recq[recq_w % RECQ];
    r->tick = (uint16_t)tick;
    r->pattern = (uint8_t)p;
    r->pad = (uint8_t)pad;
    r->vel = (uint8_t)vel;
    r->kind = (uint8_t)kind;
    recq_w = recq_w + 1u;
}

int hrp_take(hr_rec_t *r)
{
    if (recq_r == recq_w)
        return 0;
    *r = recq[recq_r % RECQ];
    recq_r = recq_r + 1u;
    return 1;
}

void hrp_resync(void) { moved = 1; }

static int skipped(uint32_t pad)
{
    uint32_t i = 0;
    int hit = 0;
    while (i < n_skip) {
        int32_t d = (int32_t)(sqp.clock - skip[i].clock);
        if (d > 0 || (d == 0 && skip[i].pad == pad && !hit)) {   /* past, or this one */
            hit |= d == 0;
            skip[i] = skip[--n_skip];
        } else {
            i++;
        }
    }
    return hit;
}

static int song_playing(void) { return sqp.state == SQP_PLAYING && sqp.song_mode; }

void hrp_tick(void)
{
    uint32_t p, t, ticks, len, a;
    const uint8_t *d;
    int own = !song_playing(), quiet;
    if (sqp.started) {            /* from the top, or on from its beat */
        hrp.tick = sqp.started == 1u ? 0u : (uint16_t)(hrp.tick / SQ_PPQN * SQ_PPQN);
        sqp.started = 0;
        cur_p = 0xFFu;
        if (!pre_start)
            n_skip = 0;           /* (a run before's) */
        pre_start = 0;
    }
    if (own) {
        p = hrp.pattern;
        ticks = hr_beats(p) * SQ_PPQN;
        if (hrp.tick >= ticks)    /* (made shorter as it played) */
            hrp.tick = 0;
        t = hrp.tick;
        hrp.tick = (uint16_t)(t + 1u >= ticks ? 0u : t + 1u);
        quiet = sqp.state == SQP_PLAYING && hrp.mute;   /* (with the parts, muted: on in time, unheard) */
    } else {
        p = sq_song_drums(sqp.song, sqp.step);
        if (p >= SQ_PATTERNS) {
            hrp.playing = 0xFFu;
            cur_p = 0xFFu;
            return;
        }
        t = sqp.tick % (hr_beats(p) * SQ_PPQN);
        quiet = hrp.mute;
    }
    if (p != hr_snd.p)
        hr_sounds_apply(p);       /* another pattern: the pads take its sounds (edits not stored: gone) */
    hrp.playing = quiet ? 0xFFu : (uint8_t)p;
    hrp.at = (uint16_t)t;
    if (p != cur_p || (int32_t)t != cur_t + 1 || moved) {
        cur = find(p, t, 0);
        moved = 0;
    }
    cur_p = (uint8_t)p;
    cur_t = (int32_t)t;
    d = sq_data(SQ_PAT(p));
    len = sq_len(SQ_PAT(p));
    for (a = cur; a < len && hr_hit_tick(d + a) == t; a += SQ_HIT_LEN) {
        uint32_t pad = d[a + 2u];
        if (quiet)
            continue;
        if (own && hrp.rec && (hrp.erase >> pad & 1u))
            rec_put(p, t, pad, 0, HR_REC_ERASE);   /* held to erase: out as it comes by */
        else if (!skipped(pad))
            hr_emit(pad, d[a + 3u]);
    }
    cur = a;
    if (!hrp.fill)
        fill_mask = 0;
    if (fill_mask && !quiet && hr_quantize(t, fill_grid(), hrp.swing) == t) {   /* FILL: on each step of its grid */
        uint32_t i;
        for (i = 0; i < HR_PADS; i++) {
            if (!(fill_mask >> i & 1u) || (int32_t)(sqp.clock - fill_after[i]) <= 0)
                continue;
            hr_emit(i, fill_vel[i]);
            if (own && hrp_recording())
                rec_put(p, t, i, fill_vel[i], HR_REC_ADD);
        }
    }
    if (own && hrp.tick == 0u && hrp.next != 0xFFu && !hrp.rec) {   /* its end: the one queued */
        hrp.pattern = hrp.next;
        hrp.next = 0xFFu;
    }
}

int hrp_recording(void)
{
    return hrp.rec && (sqp.aux || (sqp.state == SQP_PLAYING && !sqp.song_mode && !hrp.mute));
}

void hrp_record(uint32_t pad, uint32_t vel)
{
    uint32_t p = hrp.pattern, ticks = hr_beats(p) * SQ_PPQN, pos, q, last;
    if (!hrp_recording() || pad >= HR_PADS || !vel)
        return;
    if (sqp.started && !pre_start) {   /* before the first tick: the skips of a run before gone now */
        n_skip = 0;
        pre_start = 1;
    }
    /* where it was played: the clock just played or the next, the nearer (before the first: the first) */
    last = cur_p == p && cur_t >= 0 && !sqp.started ? (uint32_t)cur_t : 0xFFFFFFFFu;
    pos = last == 0xFFFFFFFFu ? 0u : sqp_nearer_last() ? last : last + 1u;
    q = hr_quantize(pos, HR_GRID[hrp.quant % HR_GRIDS], hrp.swing);
    if (q + 1u > last + 1u && n_skip < SKIPS) {   /* ahead of the clock played (or none yet): heard now, not then */
        skip[n_skip].clock = sqp.clock + (q - (last + 1u));
        skip[n_skip].pad = (uint8_t)pad;
        n_skip++;
    }
    rec_put(p, q % ticks, pad, vel, HR_REC_ADD);
}

void hrp_fill_pad(uint32_t pad, uint32_t vel, int on)
{
    uint32_t last, pos, q;
    if (pad >= HR_PADS)
        return;
    if (!on || !vel) {
        fill_mask &= (uint16_t)~(1u << pad);
        return;
    }
    if (!hrp.fill)
        return;
    /* the hit itself is played (and recorded) as any: the repeats from the grid's next step after where it went */
    last = cur_p != 0xFFu && cur_t >= 0 && !sqp.started ? (uint32_t)cur_t : 0xFFFFFFFFu;
    pos = last == 0xFFFFFFFFu ? 0u : sqp_nearer_last() ? last : last + 1u;
    q = hr_quantize(pos, fill_grid(), hrp.swing);
    fill_after[pad] = sqp.clock + q - (last + 1u);   /* (the clock of q: it may be the last played's, or earlier) */
    fill_vel[pad] = (uint8_t)vel;
    fill_mask |= (uint16_t)(1u << pad);
}

void hrp_step_in(uint32_t pad, uint32_t vel)
{
    if (pad < HR_PADS && vel)
        rec_put(hrp.pattern, 0, pad, vel, HR_REC_STEP);
}
