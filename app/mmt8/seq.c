/* SM-87: the MMT-8's memory, see seq.h */
#include <stddef.h>
#include "seq.h"

sq_mem_t sq __attribute__((section(".pool")));

static void copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    if (d < s)
        for (i = 0; i < n; i++)
            d[i] = s[i];
    else
        for (i = n; i > 0; i--)
            d[i - 1u] = s[i - 1u];
}

void sq_part_name_default(uint32_t p)
{
    static const char E[SQ_NAME] = "EMPTY PART    ";
    uint32_t i;
    for (i = 0; i < SQ_NAME; i++)
        sq.part[p].name[i] = E[i];
}

static void clear_parts_songs(void);

void sq_clear(void)
{
    uint32_t i;
    sq.magic = SQ_MAGIC;
    sq.version = SQ_VERSION;
    sq.used = 0;
    clear_parts_songs();
    for (i = 0; i < SQ_BLOBS; i++)
        sq.blob[i].off = sq.blob[i].len = 0;
    for (i = 0; i < SQ_TRACKS; i++)
        sq.offset[i] = 0;
    for (i = 0; i < SQ_MMT8_HDR; i++)
        sq.mmt8[i] = 0;
    for (i = 0; i < SQ_PATTERNS; i++)
        sq.pattern[i].beats = 0;
}

static void clear_parts_songs(void)
{
    static const char N[SQ_NAME] = "NO SONG NAME  ";
    uint32_t i, k;
    for (i = 0; i < SQ_PARTS; i++) {
        sq.part[i].beats = 0;
        for (k = 0; k < SQ_TRACKS; k++)
            sq.part[i].chan[k] = SQ_UNCHANGED;
        sq_part_name_default(i);
    }
    for (i = 0; i < SQ_SONGS; i++) {
        sq.song[i].tempo = 120;
        for (k = 0; k < SQ_NAME; k++)
            sq.song[i].name[k] = N[k];
    }
}

void sq_clear_mmt8(void)
{
    uint32_t at = 0, last = 0, i, k;
    sq_lock();
    /* the drum patterns to the arena's start, in the order they are in (each moves down: copied forward) */
    for (;;) {
        sq_blob_t *b = 0;
        for (i = SQ_PAT(0); i < SQ_PAT(SQ_PATTERNS); i++)
            if (sq.blob[i].len && sq.blob[i].off >= last && (!b || sq.blob[i].off < b->off))
                b = &sq.blob[i];
        if (!b)
            break;
        last = (uint32_t)b->off + 1u;
        for (k = 0; k < b->len; k++)
            sq.arena[at + k] = sq.arena[b->off + k];
        b->off = (uint16_t)at;
        at += b->len;
    }
    for (i = 0; i < SQ_PAT(0); i++)
        sq.blob[i].off = sq.blob[i].len = 0;
    sq.used = (uint16_t)at;
    clear_parts_songs();
    for (i = 0; i < SQ_MMT8_HDR; i++)
        sq.mmt8[i] = 0;
    sq_unlock();
}

/* the tables as each version had them: to the parts' and songs' blobs (1), then the offsets (2), the MMT-8 header
 * (3); 4 put the drum patterns' blobs after the songs', and their lengths at the end */
#define SQ_BLOBS_V3 (SQ_PARTS * SQ_TRACKS + SQ_SONGS)
#define SQ_TABLES_V1 (offsetof(sq_mem_t, blob) + SQ_BLOBS_V3 * sizeof(sq_blob_t))
uint32_t sq_image_tables(uint32_t version)
{
    switch (version) {
    case SQ_VERSION:
        return (uint32_t)offsetof(sq_mem_t, arena);
    case 3u:
        return (uint32_t)(SQ_TABLES_V1 + SQ_TRACKS + SQ_MMT8_HDR);
    case 2u:
        return (uint32_t)(SQ_TABLES_V1 + SQ_TRACKS);
    case 1u:
        return (uint32_t)SQ_TABLES_V1;
    default:
        return 0;
    }
}

int sq_image_upgrade(uint8_t *scratch)
{
    uint32_t v = sq.version, i, s;
    uint8_t *b = (uint8_t *)&sq;
    if (v >= SQ_VERSION || v == 0u)
        return 0;
    /* the offsets and the header (as far as it had them) after the drum patterns' blobs, which start empty */
    copy(b + offsetof(sq_mem_t, offset), b + SQ_TABLES_V1, sq_image_tables(v) - (uint32_t)SQ_TABLES_V1);
    for (i = SQ_BLOBS_V3; i < SQ_BLOBS; i++)
        sq.blob[i].off = sq.blob[i].len = 0;
    if (v < 2u)
        for (i = 0; i < SQ_TRACKS; i++)
            sq.offset[i] = 0;
    if (v < 3u)
        for (i = 0; i < SQ_MMT8_HDR; i++)
            sq.mmt8[i] = 0;
    for (i = 0; i < SQ_PATTERNS; i++)
        sq.pattern[i].beats = 0;
    sq.version = SQ_VERSION;
    /* the songs' steps: part and tracks, now with no drum pattern */
    for (s = 0; s < SQ_SONGS; s++) {
        uint32_t n = sq_len(SQ_SONG(s)) / 2u;
        const uint8_t *d = sq_data(SQ_SONG(s));
        if (!n)
            continue;
        for (i = 0; i < n; i++) {
            scratch[SQ_STEP * i] = d[2u * i];
            scratch[SQ_STEP * i + 1u] = d[2u * i + 1u];
            scratch[SQ_STEP * i + 2u] = SQ_NO_DRUMS;
        }
        if (sq_replace(SQ_SONG(s), scratch, SQ_STEP * n) != 0)
            return -1;
    }
    return 0;
}

int sq_valid(void)
{
    uint32_t i, sum = 0;
    if (sq.magic != SQ_MAGIC || sq.version != SQ_VERSION)
        return 0;
    for (i = 0; i < SQ_BLOBS; i++) {
        if ((uint32_t)sq.blob[i].off + sq.blob[i].len > sq.used)
            return 0;
        sum += sq.blob[i].len;
    }
    for (i = 0; i < SQ_PARTS; i++)
        if (sq.part[i].beats > SQ_MAX_BEATS)
            return 0;
    for (i = 0; i < SQ_TRACKS; i++)
        if (sq.offset[i] > SQ_OFFSET_MAX || sq.offset[i] < -SQ_OFFSET_MAX)
            return 0;
    for (i = 0; i < SQ_SONGS; i++)
        if (sq_len(SQ_SONG(i)) % SQ_STEP)
            return 0;
    for (i = 0; i < SQ_PATTERNS; i++)
        if (sq_len(SQ_PAT(i)) % SQ_HIT_LEN || sq.pattern[i].beats > SQ_MAX_BEATS)
            return 0;
    return sum == sq.used;        /* every byte someone's, none twice */
}

uint32_t sq_free(void) { return SQ_ARENA - 1u - sq.used; }

int sq_replace(uint32_t blob, const uint8_t *data, uint32_t len)
{
    sq_blob_t *b = &sq.blob[blob];
    uint32_t i, end = (uint32_t)b->off + b->len;
    int32_t delta = (int32_t)len - (int32_t)b->len;
    if (delta > 0 && (uint32_t)delta > sq_free())
        return -1;
    sq_lock();
    if (!b->len)                  /* a new blob: at the arena's end */
        b->off = sq.used, end = sq.used;
    /* the blobs after it move by delta */
    copy(sq.arena + end + delta, sq.arena + end, sq.used - end);
    for (i = 0; i < SQ_BLOBS; i++)
        if (i != blob && sq.blob[i].len && sq.blob[i].off >= end)
            sq.blob[i].off = (uint16_t)(sq.blob[i].off + delta);
    copy(sq.arena + b->off, data, len);
    b->len = (uint16_t)len;
    if (!len)
        b->off = 0;
    sq.used = (uint16_t)(sq.used + delta);
    sq_unlock();
    return 0;
}

uint32_t sq_part_used(uint32_t p)
{
    uint32_t t, m = 0;
    for (t = 0; t < SQ_TRACKS; t++)
        if (sq.blob[SQ_TRK(p, t)].len)
            m |= 1u << t;
    return m;
}

int sq_part_empty(uint32_t p) { return !sq.part[p].beats && !sq_part_used(p); }

/* a part's first use: EMPTY PART becomes NO PART NAME (the MMT-8's), a name given stays */
static void part_used_name(uint32_t p)
{
    static const char E[SQ_NAME] = "EMPTY PART    ", N[SQ_NAME] = "NO PART NAME  ";
    uint32_t i;
    for (i = 0; i < SQ_NAME; i++)
        if (sq.part[p].name[i] != E[i])
            return;
    for (i = 0; i < SQ_NAME; i++)
        sq.part[p].name[i] = N[i];
}

/* ------------------------------------------------------------------------------------------- a take --- */
static void put_ev(uint8_t *e, uint32_t tick, uint32_t st, uint32_t d1, uint32_t d2)
{
    e[0] = (uint8_t)tick;
    e[1] = (uint8_t)(tick >> 8);
    e[2] = (uint8_t)st;
    e[3] = (uint8_t)d1;
    e[4] = (uint8_t)d2;
}

/* events of a track sorted by tick, stable (insertion sort over offsets: a track is short, and mostly in order) */
static uint32_t sort_events(uint8_t *buf, uint32_t len, uint8_t *tmp)
{
    uint32_t a, n = 0;
    /* in place: walk, and move each event back past the later ones before it */
    for (a = 0; a < len;) {
        uint32_t l = sq_ev_len(buf + a), t = sq_ev_tick(buf + a), b = a, prev = 0, p;
        /* find where it belongs: after the last event with tick <= t */
        for (p = 0; p < a;) {
            uint32_t pl = sq_ev_len(buf + p);
            if (sq_ev_tick(buf + p) > t) {
                b = p;
                break;
            }
            p += pl;
        }
        (void)prev;
        if (b != a) {
            uint32_t i;
            for (i = 0; i < l; i++)
                tmp[i] = buf[a + i];
            copy(buf + b + l, buf + b, a - b);
            for (i = 0; i < l; i++)
                buf[b + i] = tmp[i];
        }
        a += l;
        n++;
    }
    return n;
}

int sq_commit(uint32_t p, uint32_t t, const sq_raw_t *take, uint32_t n, uint32_t beats, uint8_t *scratch)
{
    uint32_t i, j, len = 0, ticks = beats * SQ_PPQN;
    uint8_t tmp[SQ_NOTE_LEN];
    if (!ticks)
        return -1;
    for (i = 0; i < n; i++) {
        const sq_raw_t *r = &take[i];
        uint32_t st = r->st, tick = r->tick;
        if (tick >= ticks)
            continue;             /* past the end (a first take's length is rounded to the beat) */
        if ((st & 0xF0u) == 0x90u && r->d2) {
            /* a note: its off is the next note off of the same key and channel, else the take's end */
            uint32_t off = ticks, dur;
            for (j = i + 1u; j < n; j++) {
                const sq_raw_t *o = &take[j];
                if (o->d1 == r->d1 && (o->st & 0x0Fu) == (st & 0x0Fu) &&
                    ((o->st & 0xF0u) == 0x80u || ((o->st & 0xF0u) == 0x90u && !o->d2))) {
                    off = o->tick;
                    break;
                }
            }
            if (off > ticks)
                off = ticks;
            dur = off > tick ? off - tick : 1u;
            if (len + SQ_NOTE_LEN > SQ_SCRATCH)
                return -1;
            put_ev(scratch + len, tick, st, r->d1, r->d2);
            scratch[len + 5u] = (uint8_t)dur;
            scratch[len + 6u] = (uint8_t)(dur >> 8);
            len += SQ_NOTE_LEN;
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            continue;             /* note offs: in the notes' durations */
        } else {
            if (len + SQ_EV_LEN > SQ_SCRATCH)
                return -1;
            put_ev(scratch + len, tick, st, r->d1, r->d2);
            len += SQ_EV_LEN;
        }
    }
    sort_events(scratch, len, tmp);
    if (sq_replace(SQ_TRK(p, t), scratch, len) != 0)
        return -1;
    if (!sq.part[p].beats)
        sq.part[p].beats = (uint16_t)beats;
    part_used_name(p);
    return 0;
}

/* ------------------------------------------------------------------------------------------- songs --- */
uint32_t sq_song_steps(uint32_t s) { return sq_len(SQ_SONG(s)) / SQ_STEP; }

void sq_song_step(uint32_t s, uint32_t i, uint8_t *part, uint8_t *mask)
{
    const uint8_t *d = sq_data(SQ_SONG(s)) + SQ_STEP * i;
    *part = d[0];
    *mask = d[1];
}

uint32_t sq_song_drums(uint32_t s, uint32_t i)
{
    return i < sq_song_steps(s) ? sq_data(SQ_SONG(s))[SQ_STEP * i + 2u] : SQ_NO_DRUMS;
}

void sq_song_set_drums(uint32_t s, uint32_t i, uint32_t pattern)
{
    if (i < sq_song_steps(s))
        sq.arena[sq.blob[SQ_SONG(s)].off + SQ_STEP * i + 2u] = (uint8_t)(pattern < SQ_PATTERNS ? pattern : SQ_NO_DRUMS);
}

int sq_song_set(uint32_t s, uint32_t i, uint8_t part, uint8_t mask, uint8_t *scratch)
{
    uint32_t n = sq_song_steps(s), k;
    const uint8_t *d = sq_data(SQ_SONG(s));
    if (i > n || i >= SQ_STEPS)
        return -1;
    for (k = 0; k < SQ_STEP * n; k++)
        scratch[k] = d[k];
    if (part == 0xFFu)
        n = i;                    /* the song ends here */
    else {
        scratch[SQ_STEP * i] = part;
        scratch[SQ_STEP * i + 1u] = mask;
        if (i == n) {             /* a new step: the drums of the one before it */
            scratch[SQ_STEP * i + 2u] = i ? scratch[SQ_STEP * i - 1u] : SQ_NO_DRUMS;
            n++;
        }
    }
    return sq_replace(SQ_SONG(s), scratch, SQ_STEP * n);
}

/* ------------------------------------------------------------------------------------------- edits --- */
/* a track rebuilt through fn (which may change an event or drop it: 0), then sorted and put back */
typedef int (*ev_fn)(uint8_t *e, void *ctx);

static int rebuild(uint32_t blob, ev_fn fn, void *ctx, uint8_t *scratch)
{
    const uint8_t *d = sq_data(blob);
    uint32_t len = sq_len(blob), a, out = 0;
    uint8_t tmp[SQ_NOTE_LEN];
    if (len > SQ_SCRATCH)
        return -1;
    for (a = 0; a < len;) {
        uint32_t l = sq_ev_len(d + a), i;
        for (i = 0; i < l; i++)
            scratch[out + i] = d[a + i];
        if (fn(scratch + out, ctx))
            out += l;
        a += l;
    }
    sort_events(scratch, out, tmp);
    return sq_replace(blob, scratch, out);
}

static int fn_length(uint8_t *e, void *ctx)
{
    uint32_t end = *(uint32_t *)ctx, t = sq_ev_tick(e);
    if (t >= end)
        return 0;
    if (sq_ev_len(e) == SQ_NOTE_LEN && t + sq_ev_dur(e) > end) {
        uint32_t dur = end - t;
        e[5] = (uint8_t)dur;
        e[6] = (uint8_t)(dur >> 8);
    }
    return 1;
}

int sq_set_length(uint32_t p, uint32_t beats, uint8_t *scratch)
{
    uint32_t t, end = beats * SQ_PPQN;
    if (beats < 1u || beats > SQ_MAX_BEATS)
        return -1;
    for (t = 0; t < SQ_TRACKS; t++)
        if (rebuild(SQ_TRK(p, t), fn_length, &end, scratch) != 0)
            return -1;
    sq.part[p].beats = (uint16_t)beats;
    part_used_name(p);
    return 0;
}

static uint32_t to_grid(uint32_t t, uint32_t g) { return (t + g / 2u) / g * g; }

static int fn_quantize(uint8_t *e, void *ctx)
{
    const uint32_t *q = ctx;      /* grid, the part's ticks, the mode */
    uint32_t t = sq_ev_tick(e), g = q[0], end, nt;
    if (sq_ev_len(e) != SQ_NOTE_LEN)
        return 1;
    end = t + sq_ev_dur(e);
    nt = q[2] == SQ_Q_END ? t : to_grid(t, g);
    if (nt >= q[1])
        nt -= g;                  /* rounded past the end: back onto the last line */
    if (q[2] == SQ_Q_KEEP)
        end = nt + sq_ev_dur(e);
    else if (q[2] == SQ_Q_BOTH || q[2] == SQ_Q_END)
        end = to_grid(end, g);
    if (end > q[1])
        end = q[1];
    if (end <= nt)
        end = nt + (q[2] == SQ_Q_BOTH || q[2] == SQ_Q_END ? g : 1u);
    e[0] = (uint8_t)nt;
    e[1] = (uint8_t)(nt >> 8);
    e[5] = (uint8_t)(end - nt);
    e[6] = (uint8_t)((end - nt) >> 8);
    return 1;
}

int sq_quantize(uint32_t p, uint32_t mask, uint32_t grid, uint32_t mode, uint8_t *scratch)
{
    uint32_t t, q[3] = {grid, sq_part_ticks(p), mode};
    if (!grid)
        return -1;
    for (t = 0; t < SQ_TRACKS; t++)
        if ((mask >> t & 1u) && rebuild(SQ_TRK(p, t), fn_quantize, q, scratch) != 0)
            return -1;
    return 0;
}

static int fn_transpose(uint8_t *e, void *ctx)
{
    int32_t k;
    if (sq_ev_len(e) == SQ_NOTE_LEN) {
        k = (int32_t)e[3] + *(int32_t *)ctx;
        while (k > 127)
            k -= 12;              /* out of range: folded back by octaves, as the MMT-8 */
        while (k < 0)
            k += 12;
        e[3] = (uint8_t)k;
    }
    return 1;
}

int sq_transpose(uint32_t p, uint32_t mask, int32_t semis, uint8_t *scratch)
{
    uint32_t t;
    for (t = 0; t < SQ_TRACKS; t++)
        if ((mask >> t & 1u) && rebuild(SQ_TRK(p, t), fn_transpose, &semis, scratch) != 0)
            return -1;
    return 0;
}

int sq_erase(uint32_t p, uint32_t mask)
{
    uint32_t t;
    for (t = 0; t < SQ_TRACKS; t++)
        if (mask >> t & 1u)
            (void)sq_replace(SQ_TRK(p, t), 0, 0);
    if (!sq_part_used(p)) {       /* the whole part: EMPTY PART again (its channels stay, as the MMT-8's) */
        sq.part[p].beats = 0;
        sq_part_name_default(p);
    }
    return 0;
}

int sq_merge(uint32_t p, uint32_t a, uint32_t b, uint8_t *scratch)
{
    uint32_t la = sq_len(SQ_TRK(p, a)), lb = sq_len(SQ_TRK(p, b)), i;
    const uint8_t *da = sq_data(SQ_TRK(p, a)), *db = sq_data(SQ_TRK(p, b));
    uint8_t ca = sq.part[p].chan[a], cb = sq.part[p].chan[b], tmp[SQ_NOTE_LEN];
    if (la + lb > SQ_SCRATCH)
        return -1;
    /* both channel assignments baked into the data, and b UNCHANGED (MMT-8 MERGE) */
    for (i = 0; i < lb;) {
        uint32_t l = sq_ev_len(db + i), k;
        for (k = 0; k < l; k++)
            scratch[i + k] = db[i + k];
        if (cb)
            scratch[i + 2u] = (uint8_t)((scratch[i + 2u] & 0xF0u) | (cb - 1u));
        i += l;
    }
    for (i = 0; i < la;) {
        uint32_t l = sq_ev_len(da + i), k;
        for (k = 0; k < l; k++)
            scratch[lb + i + k] = da[i + k];
        if (ca)
            scratch[lb + i + 2u] = (uint8_t)((scratch[lb + i + 2u] & 0xF0u) | (ca - 1u));
        i += l;
    }
    sort_events(scratch, la + lb, tmp);
    if (sq_replace(SQ_TRK(p, b), scratch, la + lb) != 0)
        return -1;
    sq.part[p].chan[b] = SQ_UNCHANGED;
    return 0;
}

int sq_copy_tracks(uint32_t a, uint32_t b, uint32_t mask)
{
    uint32_t t;
    if (a == b)
        return 0;
    for (t = 0; t < SQ_TRACKS; t++)
        if (mask >> t & 1u) {
            /* the source moves while the arena does: copy through its own offset, which sq_replace keeps */
            uint32_t len = sq_len(SQ_TRK(a, t));
            uint32_t need = len > sq_len(SQ_TRK(b, t)) ? len - sq_len(SQ_TRK(b, t)) : 0u;
            if (need > sq_free())
                return -1;
            if (sq_replace(SQ_TRK(b, t), 0, 0) != 0)
                return -1;
            if (len && sq_replace(SQ_TRK(b, t), sq_data(SQ_TRK(a, t)), len) != 0)
                return -1;
            sq.part[b].chan[t] = sq.part[a].chan[t];
        }
    if (!sq.part[b].beats)
        sq.part[b].beats = sq.part[a].beats;
    return 0;
}

/* ------------------------------------------------------------------------------ filters, LENGTH top --- */
int sq_filter_match(const uint8_t *e, uint32_t filter)
{
    uint32_t st = e[2] & 0xF0u;
    switch (filter) {
    case SQ_F_ALL:
        return 1;
    case SQ_F_NOTES:
        return st == 0x90u;
    case SQ_F_BEND:
        return st == 0xE0u;
    case SQ_F_CTRL:
        return st == 0xB0u;
    case SQ_F_PROG:
        return st == 0xC0u;
    case SQ_F_TOUCH:
        return st == 0xD0u || st == 0xA0u;
    default:
        return filter < SQ_F_N && (e[2] & 0x0Fu) == filter - SQ_F_CHAN;
    }
}

static int fn_keep_unmatched(uint8_t *e, void *ctx) { return !sq_filter_match(e, *(uint32_t *)ctx); }

int sq_erase_filter(uint32_t p, uint32_t mask, uint32_t filter, uint8_t *scratch)
{
    uint32_t t;
    if (filter == SQ_F_ALL)
        return sq_erase(p, mask);
    for (t = 0; t < SQ_TRACKS; t++)    /* (a track only gets shorter: never out of room) */
        if ((mask >> t & 1u) && rebuild(SQ_TRK(p, t), fn_keep_unmatched, &filter, scratch) != 0)
            return -1;
    return 0;
}

static int fn_shift(uint8_t *e, void *ctx)
{
    int32_t t = (int32_t)sq_ev_tick(e) + *(int32_t *)ctx;
    if (t < 0)
        return 0;
    e[0] = (uint8_t)t;
    e[1] = (uint8_t)((uint32_t)t >> 8);
    return 1;
}

int sq_set_length_top(uint32_t p, uint32_t beats, uint8_t *scratch)
{
    uint32_t t;
    int32_t shift = ((int32_t)beats - (int32_t)sq.part[p].beats) * (int32_t)SQ_PPQN;
    if (beats < 1u || beats > SQ_MAX_BEATS)
        return -1;
    for (t = 0; t < SQ_TRACKS; t++)
        if (rebuild(SQ_TRK(p, t), fn_shift, &shift, scratch) != 0)
            return -1;
    sq.part[p].beats = (uint16_t)beats;
    part_used_name(p);
    return 0;
}

/* ----------------------------------------------------------------------------------------- copies --- */
static uint32_t filtered_len(uint32_t blob, uint32_t filter)
{
    const uint8_t *d = sq_data(blob);
    uint32_t len = sq_len(blob), a, n = 0;
    for (a = 0; a < len; a += sq_ev_len(d + a))
        if (sq_filter_match(d + a, filter))
            n += sq_ev_len(d + a);
    return n;
}

int sq_append_part(uint32_t a, uint32_t b, uint32_t filter, uint8_t *scratch)
{
    uint32_t t, need = 0, off, beats, k;
    int empty = sq_part_empty(b);
    if (sq_part_empty(a))
        return SQ_E_NONE;
    off = empty ? 0u : sq_part_ticks(b);
    beats = (empty ? 0u : sq.part[b].beats) + sq.part[a].beats;
    if (beats > SQ_MAX_BEATS)
        return SQ_E_LONG;
    for (t = 0; t < SQ_TRACKS; t++) {      /* the room first: every track or none */
        uint32_t n = filtered_len(SQ_TRK(a, t), filter);
        if (sq_len(SQ_TRK(b, t)) + n > SQ_SCRATCH)
            return SQ_E_ROOM;
        need += n;
    }
    if (need > sq_free())
        return SQ_E_ROOM;
    for (t = 0; t < SQ_TRACKS; t++) {
        uint32_t la = sq_len(SQ_TRK(a, t)), lb = sq_len(SQ_TRK(b, t)), i, out = lb;
        const uint8_t *da = sq_data(SQ_TRK(a, t));
        copy(scratch, sq_data(SQ_TRK(b, t)), lb);
        for (i = 0; i < la; i += sq_ev_len(da + i)) {   /* after b's last tick: still in order */
            uint32_t l = sq_ev_len(da + i), tick = sq_ev_tick(da + i) + off;
            if (!sq_filter_match(da + i, filter))
                continue;
            copy(scratch + out, da + i, l);
            scratch[out] = (uint8_t)tick;
            scratch[out + 1u] = (uint8_t)(tick >> 8);
            out += l;
        }
        if (out != lb && sq_replace(SQ_TRK(b, t), scratch, out) != 0)
            return SQ_E_ROOM;      /* (not after the check above) */
    }
    if (empty) {
        for (k = 0; k < SQ_TRACKS; k++)
            sq.part[b].chan[k] = sq.part[a].chan[k];
        for (k = 0; k < SQ_NAME; k++)
            sq.part[b].name[k] = sq.part[a].name[k];
    }
    sq.part[b].beats = (uint16_t)beats;
    part_used_name(b);
    return 0;
}

int sq_copy_track(uint32_t a, uint32_t ta, uint32_t b, uint32_t tb, uint32_t filter, uint8_t *scratch)
{
    uint32_t beats = sq.part[b].beats ? sq.part[b].beats : sq.part[a].beats, end = beats * SQ_PPQN;
    uint32_t la = sq_len(SQ_TRK(a, ta)), lb = sq_len(SQ_TRK(b, tb)), i, out = 0;
    const uint8_t *da = sq_data(SQ_TRK(a, ta));
    if (!beats)
        return SQ_E_NONE;
    for (i = 0; i < la; i += sq_ev_len(da + i)) {
        uint32_t l = sq_ev_len(da + i), tick = sq_ev_tick(da + i);
        if (!sq_filter_match(da + i, filter) || tick >= end)
            continue;              /* past b's end: cut */
        if (out + l > SQ_SCRATCH)
            return SQ_E_ROOM;
        copy(scratch + out, da + i, l);
        if (l == SQ_NOTE_LEN && tick + sq_ev_dur(da + i) > end) {
            scratch[out + 5u] = (uint8_t)(end - tick);
            scratch[out + 6u] = (uint8_t)((end - tick) >> 8);
        }
        out += l;
    }
    if (out > lb && out - lb > sq_free())
        return SQ_E_ROOM;
    if (sq_replace(SQ_TRK(b, tb), scratch, out) != 0)
        return SQ_E_ROOM;
    if (!lb)
        sq.part[b].chan[tb] = sq.part[a].chan[ta];
    sq.part[b].beats = (uint16_t)beats;
    part_used_name(b);
    return 0;
}

/* ------------------------------------------------------------------------------------- song edits --- */
int sq_song_append(uint32_t a, uint32_t b, uint8_t *scratch)
{
    uint32_t na = sq_song_steps(a), nb = sq_song_steps(b);
    if (!na)
        return SQ_E_NONE;
    if (na + nb > SQ_STEPS)
        return SQ_E_LONG;
    copy(scratch, sq_data(SQ_SONG(b)), SQ_STEP * nb);
    copy(scratch + SQ_STEP * nb, sq_data(SQ_SONG(a)), SQ_STEP * na);
    return sq_replace(SQ_SONG(b), scratch, SQ_STEP * (na + nb)) != 0 ? SQ_E_ROOM : 0;
}

void sq_song_erase(uint32_t s)
{
    static const char N[SQ_NAME] = "NO SONG NAME  ";
    uint32_t k;
    (void)sq_replace(SQ_SONG(s), 0, 0);
    for (k = 0; k < SQ_NAME; k++)
        sq.song[s].name[k] = N[k];
}

int sq_song_insert(uint32_t s, uint32_t i, uint8_t part, uint8_t mask, uint8_t drums, uint8_t *scratch)
{
    uint32_t n = sq_song_steps(s);
    const uint8_t *d = sq_data(SQ_SONG(s));
    if (i > n)
        return SQ_E_NONE;
    if (n >= SQ_STEPS)
        return SQ_E_LONG;
    copy(scratch, d, SQ_STEP * i);
    scratch[SQ_STEP * i] = part;
    scratch[SQ_STEP * i + 1u] = mask;
    scratch[SQ_STEP * i + 2u] = drums;
    copy(scratch + SQ_STEP * (i + 1u), d + SQ_STEP * i, SQ_STEP * (n - i));
    return sq_replace(SQ_SONG(s), scratch, SQ_STEP * (n + 1u)) != 0 ? SQ_E_ROOM : 0;
}

int sq_song_delete(uint32_t s, uint32_t i, uint8_t *scratch)
{
    uint32_t n = sq_song_steps(s);
    const uint8_t *d = sq_data(SQ_SONG(s));
    if (i >= n)
        return SQ_E_NONE;
    copy(scratch, d, SQ_STEP * i);
    copy(scratch + SQ_STEP * i, d + SQ_STEP * (i + 1u), SQ_STEP * (n - i - 1u));
    return sq_replace(SQ_SONG(s), scratch, SQ_STEP * (n - 1u));
}

/* ------------------------------------------------------------------------------------------- EDIT --- */
/* event i's offset in the blob, or the blob's length */
static uint32_t ev_at(uint32_t blob, uint32_t i)
{
    const uint8_t *d = sq_data(blob);
    uint32_t len = sq_len(blob), a = 0;
    while (i && a < len) {
        a += sq_ev_len(d + a);
        i--;
    }
    return a < len ? a : len;
}

uint32_t sq_events(uint32_t p, uint32_t t)
{
    const uint8_t *d = sq_data(SQ_TRK(p, t));
    uint32_t len = sq_len(SQ_TRK(p, t)), a, n = 0;
    for (a = 0; a < len; a += sq_ev_len(d + a))
        n++;
    return n;
}

const uint8_t *sq_event(uint32_t p, uint32_t t, uint32_t i)
{
    uint32_t a = ev_at(SQ_TRK(p, t), i);
    return a < sq_len(SQ_TRK(p, t)) ? sq_data(SQ_TRK(p, t)) + a : 0;
}

int32_t sq_event_put(uint32_t p, uint32_t t, uint32_t i, const uint8_t *ev, uint8_t *scratch)
{
    uint32_t blob = SQ_TRK(p, t), len = sq_len(blob), at = ev_at(blob, i), lo, ln = sq_ev_len(ev), m, a, n = 0;
    uint32_t tick = sq_ev_tick(ev);
    const uint8_t *d = sq_data(blob);
    lo = at < len ? sq_ev_len(d + at) : 0u;
    m = len - lo;
    if (m + ln > SQ_SCRATCH)
        return -1;
    /* the track without it, then it in after the last event at its tick or before: no sort (a turn a time) */
    copy(scratch, d, at);
    copy(scratch + at, d + at + lo, len - at - lo);
    for (a = 0; a < m && sq_ev_tick(scratch + a) <= tick; a += sq_ev_len(scratch + a))
        n++;
    copy(scratch + a + ln, scratch + a, m - a);
    copy(scratch + a, ev, ln);
    if (sq_replace(blob, scratch, m + ln) != 0)
        return -1;
    return (int32_t)n;
}

int sq_event_delete(uint32_t p, uint32_t t, uint32_t i, uint8_t *scratch)
{
    uint32_t blob = SQ_TRK(p, t), len = sq_len(blob), at = ev_at(blob, i), l;
    const uint8_t *d = sq_data(blob);
    if (at >= len)
        return -1;
    l = sq_ev_len(d + at);
    copy(scratch, d, at);
    copy(scratch + at, d + at + l, len - at - l);
    return sq_replace(blob, scratch, len - l);
}
