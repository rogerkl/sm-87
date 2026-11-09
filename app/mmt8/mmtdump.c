/* SM-87: the MMT-8's memory dump, see mmtdump.h */
#include "mmtdump.h"

/* MMT-8 event numbers past the controllers' 0-121 */
#define MD_PRGM 122u
#define MD_AFTR 123u
#define MD_BEND 124u
#define MD_SYSX 125u

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static void put_be16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
static void put_le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }

static uint32_t from_bcd(uint32_t v)
{
    return (v & 15u) + (v >> 4 & 15u) * 10u + (v >> 8 & 15u) * 100u + (v >> 12 & 15u) * 1000u;
}

static uint32_t to_bcd(uint32_t v) { return v % 10u | (v / 10u % 10u) << 4 | (v / 100u % 10u) << 8 | (v / 1000u) << 12; }

int md_has_header(void) { return sq.mmt8[0xCF] || sq.mmt8[0xD0]; }

/* --------------------------------------------------------------------------------------------- packing --- */
uint32_t md_packed_len(uint32_t n) { return (n * 8u + 6u) / 7u; }

uint8_t md_packed_byte(const uint8_t *img, uint32_t n, uint32_t k)
{
    uint32_t bit = k * 7u, i = bit / 8u, sh = bit % 8u;   /* its 7 bits start at bit sh of byte i (from the top) */
    uint32_t w = (uint32_t)(i < n ? img[i] : 0u) << 8 | (i + 1u < n ? img[i + 1u] : 0u);
    return (uint8_t)(w >> (9u - sh) & 0x7Fu);
}

void md_rx_byte(md_rx_t *rx, uint8_t b, uint8_t *(*take_buf)(uint32_t *max))
{
    static const uint8_t PRE[MD_PREFIX] = {0xF0, 0x00, 0x00, 0x0E, 0x00};
    if (rx->st == MD_RX_DONE)
        return;                   /* the last one not taken in yet: nothing until it is */
    if (b == 0xF0u) {
        if (rx->st == MD_RX_DATA) {
            rx->over = 1;         /* cut short: given up (its buffer back in the main loop) */
            rx->st = MD_RX_DONE;
            return;
        }
        rx->st = MD_RX_PREFIX;
        rx->pre = 1;
        return;
    }
    switch (rx->st) {
    case MD_RX_PREFIX:
        if (b != PRE[rx->pre]) {
            rx->st = b == 0xF7u ? MD_RX_IDLE : MD_RX_SKIP;
        } else if (++rx->pre == MD_PREFIX) {
            rx->buf = take_buf(&rx->max);
            rx->acc = rx->n = 0;
            rx->bits = rx->over = 0;
            rx->st = rx->buf ? MD_RX_DATA : MD_RX_SKIP;
        }
        break;
    case MD_RX_DATA:
        if (b & 0x80u) {          /* F7: whole; another status: cut short */
            rx->over |= (uint8_t)(b != 0xF7u);
            rx->st = MD_RX_DONE;
            break;
        }
        rx->acc = (rx->acc << 7 | b) & 0x7FFFu;
        rx->bits = (uint8_t)(rx->bits + 7u);
        if (rx->bits >= 8u) {
            rx->bits = (uint8_t)(rx->bits - 8u);
            if (rx->n < rx->max)
                rx->buf[rx->n++] = (uint8_t)(rx->acc >> rx->bits);
            else
                rx->over = 1;
        }
        break;
    default:
        if (b == 0xF7u)
            rx->st = MD_RX_IDLE;
        break;
    }
}

/* ---------------------------------------------------------------------------------------------- import --- */
/* a part's or song's place in the image from its pointer at ptr (0: none there), with room for its header */
static uint32_t md_at(const uint8_t *img, uint32_t len, uint32_t ptr, uint32_t hdr)
{
    uint32_t a = be16(img + ptr);
    if (!(a & 0xFF00u) || a < MD_BASE + MD_HDR)
        return 0;
    a -= MD_BASE;
    return a + hdr <= len ? a : 0u;
}

static void put_event(uint8_t *o, uint32_t tick, uint32_t st, uint32_t d1, uint32_t d2)
{
    o[0] = (uint8_t)tick;
    o[1] = (uint8_t)(tick >> 8);
    o[2] = (uint8_t)st;
    o[3] = (uint8_t)d1;
    o[4] = (uint8_t)d2;
}

/* a track's packets from at to its end-of-track (before end), as ours into out (0: only read) cut to ticks (0: none
 * known yet). Returns the bytes made (-1: no end-of-track, -2: more than a track holds); its end's clocks and the
 * place after it in *clocks and *next */
static int32_t md_track_in(const uint8_t *img, uint32_t at, uint32_t end, uint8_t *out, uint32_t ticks,
                           uint32_t *clocks, uint32_t *next, uint32_t *skipped)
{
    uint32_t t = 0, n = 0;
    while (at < end) {
        uint32_t num, v, ch, a1, a2, b = img[at];
        if (b & 0x80u) {          /* a new clock */
            if (at + 7u > end)
                return -1;
            t = le16(img + at + 1u);
            num = b & 0x7Fu, v = img[at + 3u], ch = img[at + 4u], a1 = img[at + 5u], a2 = img[at + 6u];
            at += 7u;
            if (!num && !v && ch == 0x80u) {   /* the end of the track: 80 clocks 00 80 00 00 */
                *clocks = t;
                *next = at;
                return (int32_t)n;
            }
        } else {
            if (at + 5u > end)
                return -1;
            num = b, v = img[at + 1u], ch = img[at + 2u], a1 = img[at + 3u], a2 = img[at + 4u];
            at += 5u;
        }
        if (ticks && t >= ticks)
            continue;             /* past the part's end */
        ch &= 15u;
        if (!(v & 0x80u)) {       /* a note: its duration the most significant byte first */
            uint32_t dur = (a1 & 0x7Fu) << 8 | a2;
            if (dur < 1u)
                dur = 1u;
            if (ticks && t + dur > ticks)
                dur = ticks - t;
            if (n + SQ_NOTE_LEN > SQ_SCRATCH)
                return -2;
            if (out) {
                put_event(out + n, t, 0x90u | ch, num & 0x7Fu, v ? v : 1u);
                out[n + 5u] = (uint8_t)dur;
                out[n + 6u] = (uint8_t)(dur >> 8);
            }
            n += SQ_NOTE_LEN;
        } else {
            uint32_t st, d1, d2;
            v &= 0x7Fu;
            if (num < MD_PRGM)
                st = 0xB0u, d1 = num, d2 = v;
            else if (num == MD_PRGM)
                st = 0xC0u, d1 = v, d2 = 0;
            else if (num == MD_AFTR)
                st = 0xD0u, d1 = v, d2 = 0;
            else if (num == MD_BEND)
                st = 0xE0u, d1 = a1 & 0x7Fu, d2 = a2 & 0x7Fu;
            else {
                (*skipped)++;     /* SysEx (125) and what else: not kept */
                continue;
            }
            if (n + SQ_EV_LEN > SQ_SCRATCH)
                return -2;
            if (out)
                put_event(out + n, t, st | ch, d1, d2);
            n += SQ_EV_LEN;
        }
    }
    return -1;
}

/* the name, printable (an MMT-8's are ASCII) */
static void md_name(char *d, const uint8_t *s)
{
    uint32_t i;
    for (i = 0; i < SQ_NAME; i++)
        d[i] = s[i] >= 0x20u && s[i] < 0x7Fu ? (char)s[i] : ' ';
}

/* part p from the image at 'at': its tracks (8 .. 1, one after another from its header; their offsets say the same),
 * its length (its tracks' ends: the BCD beats the cross-check), channels and name. 0, -1 not readable, -2 no room */
static int md_part_in(const uint8_t *img, uint32_t len, uint32_t at, uint32_t p, uint8_t *scratch, uint32_t *skipped)
{
    uint32_t size = le16(img + at), end = at + size, pos, j, clocks = 0, beats, c, next, skip = 0;
    if (size < MD_PART_HDR || end > len)
        return -1;
    /* first their ends: the part's length (the longest, to the beat) */
    for (j = 0, pos = at + MD_PART_HDR; j < SQ_TRACKS; j++, pos = next) {
        if (md_track_in(img, pos, end, 0, 0, &c, &next, &skip) < 0)
            return -1;
        if (c > clocks)
            clocks = c;
    }
    beats = (clocks + SQ_PPQN - 1u) / SQ_PPQN;
    if (!beats) {
        beats = from_bcd(le16(img + at + 0x12u));
        beats = beats ? beats - 1u : 0u;  /* (the BCD is the beats + 1) */
    }
    if (!beats)
        return 0;                 /* no length: empty */
    if (beats > SQ_MAX_BEATS)
        return -1;
    for (j = 0, pos = at + MD_PART_HDR; j < SQ_TRACKS; j++, pos = next) {
        int32_t n = md_track_in(img, pos, end, scratch, beats * SQ_PPQN, &c, &next, skipped);
        if (n < 0)
            return n == -2 ? -2 : -1;
        if (sq_replace(SQ_TRK(p, 7u - j), scratch, (uint32_t)n) != 0)
            return -2;
    }
    sq.part[p].beats = (uint16_t)beats;
    for (j = 0; j < SQ_TRACKS; j++)   /* the channels, track 8 first */
        sq.part[p].chan[7u - j] = img[at + 0x14u + j] <= 16u ? img[at + 0x14u + j] : 0u;
    md_name(sq.part[p].name, img + at + 0x1Cu);
    return 0;
}

int md_import(const uint8_t *img, uint32_t len, uint8_t *scratch, md_report_t *r)
{
    int8_t off[SQ_TRACKS];
    uint32_t p, s, i;
    r->parts = r->songs = r->lost_parts = 0;
    r->first_lost = -1;
    r->skipped = 0;
    if (len < MD_HDR)
        return -1;
    for (i = 0; i < SQ_TRACKS; i++)
        off[i] = sq.offset[i];
    sq_clear();                   /* (the song offsets are ours: kept) */
    for (i = 0; i < SQ_TRACKS; i++)
        sq.offset[i] = off[i];
    for (i = 0; i < MD_HDR; i++)
        sq.mmt8[i] = img[i];
    for (p = 0; p < SQ_PARTS; p++) {
        uint32_t at = md_at(img, len, 2u * p, MD_PART_HDR);
        int e;
        if (!at)
            continue;
        e = md_part_in(img, len, at, p, scratch, &r->skipped);
        if (e == 0 && sq.part[p].beats) {
            r->parts++;
            continue;
        }
        if (e < 0) {              /* what of it went in, out again */
            sq_erase(p, 0xFFu);
            r->lost_parts++;
            if (r->first_lost < 0)
                r->first_lost = (int16_t)p;
        }
    }
    for (s = 0; s < SQ_SONGS; s++) {
        uint32_t at = md_at(img, len, 0x102u + 2u * s, MD_SONG_HDR), end, n = 0, k;
        if (!at)
            continue;
        end = at + le16(img + at);
        if (end > len)
            end = len;
        for (k = at + MD_SONG_HDR; k + 1u < end && img[k] < SQ_PARTS && n < SQ_STEPS; k += 2u, n++) {
            scratch[2u * n] = img[k];
            scratch[2u * n + 1u] = img[k + 1u];
        }
        sq.song[s].tempo = img[at + 2u] >= 30u && img[at + 2u] <= 250u ? img[at + 2u] : 120u;
        md_name(sq.song[s].name, img + at + 3u);
        if (n && sq_replace(SQ_SONG(s), scratch, 2u * n) == 0)
            r->songs++;
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------------- export --- */
/* one of our events as an MMT-8 packet's last five bytes: number, amount (flag), channel, two more. 0: it has none
 * (poly pressure, controllers 122-127) */
static int md_event_out(const uint8_t *e, uint8_t *o)
{
    uint32_t st = e[2] & 0xF0u, ch = e[2] & 0x0Fu;
    o[2] = (uint8_t)ch;
    o[3] = o[4] = 0;
    switch (st) {
    case 0x90: {
        uint32_t dur = sq_ev_dur(e) > 0x7FFFu ? 0x7FFFu : sq_ev_dur(e);
        o[0] = e[3] & 0x7Fu, o[1] = e[4] & 0x7Fu ? e[4] & 0x7Fu : 1u;
        o[3] = (uint8_t)(dur >> 8), o[4] = (uint8_t)dur;
        return 1;
    }
    case 0xB0:
        if (e[3] >= MD_PRGM)
            return 0;
        o[0] = e[3], o[1] = (uint8_t)(0x80u | e[4]);
        return 1;
    case 0xC0:
        o[0] = MD_PRGM, o[1] = (uint8_t)(0x80u | e[3]);
        return 1;
    case 0xD0:
        o[0] = MD_AFTR, o[1] = (uint8_t)(0x80u | e[3]);
        return 1;
    case 0xE0:
        o[0] = MD_BEND, o[1] = 0x80u, o[3] = e[3] & 0x7Fu, o[4] = e[4] & 0x7Fu;
        return 1;
    default:
        return 0;
    }
}

int32_t md_export(uint8_t *img, uint32_t max)
{
    uint32_t pos = MD_HDR, p, s, i, j;
    if (max > MD_MAX)
        max = MD_MAX;
    if (max < MD_HDR)
        return -1;
    for (i = 0; i < MD_HDR; i++)  /* the header of the MMT-8's own dump, its pointers made again */
        img[i] = sq.mmt8[i];
    for (i = 0; i < 2u * SQ_PARTS; i++)
        img[i] = 0;
    for (i = 0; i < 2u * SQ_SONGS; i++)
        img[0x102u + i] = 0;
    for (p = 0; p < SQ_PARTS; p++) {
        uint32_t at = pos, beats = sq.part[p].beats;
        if (!beats)
            continue;             /* an empty part: no data, its pointer 0 */
        if (pos + MD_PART_HDR > max)
            return -1;
        put_be16(img + 2u * p, at + MD_BASE);
        pos += MD_PART_HDR;
        for (j = 0; j < SQ_TRACKS; j++) {   /* track 8 first */
            uint32_t t = 7u - j, len = sq_len(SQ_TRK(p, t)), a, last = 0xFFFFFFFFu;
            const uint8_t *d = sq_data(SQ_TRK(p, t));
            put_le16(img + at + 2u + 2u * j, pos - at);
            for (a = 0; a < len; a += sq_ev_len(d + a)) {
                uint8_t o[5];
                uint32_t tick = sq_ev_tick(d + a);
                if (!md_event_out(d + a, o))
                    continue;
                if (tick != last) {          /* a new clock: the 7-byte packet with it */
                    if (pos + 7u > max)
                        return -1;
                    img[pos] = (uint8_t)(0x80u | o[0]);
                    put_le16(img + pos + 1u, tick);
                    for (i = 1; i < 5u; i++)
                        img[pos + 2u + i] = o[i];
                    pos += 7u;
                    last = tick;
                } else {
                    if (pos + 5u > max)
                        return -1;
                    for (i = 0; i < 5u; i++)
                        img[pos + i] = o[i];
                    pos += 5u;
                }
            }
            if (pos + 7u > max)
                return -1;
            img[pos] = 0x80u;     /* the end of the track */
            put_le16(img + pos + 1u, beats * SQ_PPQN);
            img[pos + 3u] = 0;
            img[pos + 4u] = 0x80u;
            img[pos + 5u] = img[pos + 6u] = 0;
            pos += 7u;
        }
        put_le16(img + at, pos - at);
        put_le16(img + at + 0x12u, to_bcd(beats + 1u));   /* (the beats + 1, as the MMT-8 has them) */
        for (j = 0; j < SQ_TRACKS; j++)
            img[at + 0x14u + j] = sq.part[p].chan[7u - j];
        for (i = 0; i < SQ_NAME; i++)
            img[at + 0x1Cu + i] = (uint8_t)sq.part[p].name[i];
    }
    for (s = 0; s < SQ_SONGS; s++) {
        uint32_t n = sq_song_steps(s), at = pos, size = MD_SONG_HDR + 2u * n + 1u;
        const uint8_t *d = sq_data(SQ_SONG(s));
        if (!n)
            continue;
        if (pos + size > max)
            return -1;
        put_be16(img + 0x102u + 2u * s, at + MD_BASE);
        put_le16(img + at, size);
        img[at + 2u] = (uint8_t)sq.song[s].tempo;
        for (i = 0; i < SQ_NAME; i++)
            img[at + 3u + i] = (uint8_t)sq.song[s].name[i];
        for (i = 0; i < 2u * n; i++)
            img[at + MD_SONG_HDR + i] = d[i];
        img[at + size - 1u] = 0xFFu;
        pos += size;
    }
    put_le16(img + 0xCFu, pos + MD_BASE);            /* the free memory: where it starts, how long */
    put_le16(img + 0xD3u, MD_END - (pos + MD_BASE));
    return (int32_t)pos;
}
