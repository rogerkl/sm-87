/* SM-87: the S700's sample dump and parameter messages, see sds.h */
#include "sds.h"

sds_dump_t sds_buf[SDS_BUFS];
volatile uint32_t sds_refused;
volatile uint32_t sds_acks;
volatile uint8_t sds_enabled = 1;

/* ------------------------------------------------------------------------------------------ receiving --- */
enum { RX_IDLE, RX_HEADER, RX_BLOCKS, RX_SKIP };
static struct {
    uint8_t st;
    uint8_t hdr[SDS_HEADER];
    uint8_t blk[SDS_BLOCK];
    uint32_t n;                   /* bytes of the header or the block so far */
    sds_dump_t *d;
} rx;

static uint32_t rx_seq;

static uint32_t tb(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 7 | (uint32_t)b[2] << 14; }

void sds_init(uint8_t *mem0, uint8_t *mem1)
{
    sds_buf[0].data = mem0;
    sds_buf[1].data = mem1;
    sds_buf[0].state = sds_buf[1].state = SDS_FREE;
    rx.st = RX_IDLE;
    rx.d = 0;
}

int sds_hold(void)
{
    uint32_t i;
    for (i = 0; i < SDS_BUFS; i++)
        if (sds_buf[i].state != SDS_FREE)
            return 0;
    for (i = 0; i < SDS_BUFS; i++)
        sds_buf[i].state = SDS_HELD;
    return 1;
}

void sds_release(void)
{
    uint32_t i;
    for (i = 0; i < SDS_BUFS; i++)
        if (sds_buf[i].state == SDS_HELD)
            sds_buf[i].state = SDS_FREE;
}

static void rx_drop(void)         /* a dump that did not finish: its buffer free again */
{
    if (rx.d && rx.d->state == SDS_FILLING)
        rx.d->state = SDS_FREE;
    rx.d = 0;
}

static void rx_start(void)
{
    sds_dump_t *d = 0;
    uint32_t i;
    if (!sds_enabled) {
        sds_refused++;
        rx.st = RX_SKIP;
        return;
    }
    for (i = 0; i < SDS_BUFS && !d; i++)
        if (sds_buf[i].state == SDS_FREE && sds_buf[i].data)
            d = &sds_buf[i];
    if (!d) {
        sds_refused++;
        rx.st = RX_SKIP;
        return;
    }
    d->slot = rx.hdr[3];
    d->bits = rx.hdr[5];
    d->period_ns = tb(rx.hdr + 6);
    d->total = tb(rx.hdr + 9);
    d->loop_start = tb(rx.hdr + 12);
    d->loop_end = tb(rx.hdr + 15);
    d->alternating = rx.hdr[18] == 1u;
    d->words = d->blocks = d->bad_blocks = 0;
    d->state = SDS_FILLING;
    rx.d = d;
    rx.st = RX_BLOCKS;
    rx.n = 0;
    sds_acks++;                   /* the header's */
}

static void rx_block(void)
{
    sds_dump_t *d = rx.d;
    uint32_t i, chk = 0, max = d->total < S7_MAXWORDS ? d->total : S7_MAXWORDS, at = d->blocks * SDS_WORDS_PER_BLOCK;
    for (i = 1; i <= 2u * SDS_WORDS_PER_BLOCK; i++)
        chk ^= rx.blk[i];
    if (chk != rx.blk[SDS_BLOCK - 1u] || rx.blk[0] != (d->blocks & 0x7Fu))
        d->bad_blocks++;
    for (i = 0; i < SDS_WORDS_PER_BLOCK && at + i < max; i++) {
        /* a word left-justified in 14 bits, 7 in each byte: its top 12 bits, whatever the bits per word */
        uint32_t w14 = (uint32_t)rx.blk[1u + 2u * i] << 7 | rx.blk[2u + 2u * i];
        s7_put_w12(d->data, at + i, w14 >> 2);
    }
    d->blocks++;
    d->words = at + i;
}

void sds_rx_byte(uint8_t b)
{
    if (b == 0xF0u) {
        rx_drop();
        rx.st = RX_HEADER;
        rx.n = 0;
        rx.hdr[rx.n++] = b;
        return;
    }
    if (b == 0xF7u) {
        if (rx.st == RX_BLOCKS && rx.d) {
            if (rx.d->words) {
                rx.d->seq = ++rx_seq;
                rx.d->state = SDS_READY;   /* fewer blocks than announced is fine: senders announce 200 more */
            } else
                rx.d->state = SDS_FREE;
            rx.d = 0;
        } else {
            rx_drop();
        }
        rx.st = RX_IDLE;
        return;
    }
    if (b & 0x80u) {              /* (the USB interrupt ends a frame at another status; realtime is left out) */
        rx_drop();
        rx.st = RX_IDLE;
        return;
    }
    switch (rx.st) {
    case RX_HEADER:
        rx.hdr[rx.n++] = b;
        if (rx.n == 3u && (rx.hdr[1] != 0x7Eu || rx.hdr[2] != 0x01u))
            rx.st = RX_SKIP;      /* not a sample dump */
        else if (rx.n == SDS_HEADER)
            rx_start();
        break;
    case RX_BLOCKS:
        rx.blk[rx.n++] = b;
        if (rx.n == SDS_BLOCK) {
            if (rx.d->blocks < 546u)
                rx_block();
            rx.n = 0;
            sds_acks++;           /* (a bad one too: it is kept, its words as they came) */
        }
        break;
    default:
        break;
    }
}

uint32_t sds_rx_progress(void) { return rx.st == RX_BLOCKS && rx.d ? rx.d->words : 0u; }

uint32_t sds_length(const sds_dump_t *d)
{
    uint32_t len = d->words;
    if (d->loop_end && d->loop_end < len)
        len = d->loop_end;
    return len;
}

uint32_t sds_rate_code(uint32_t period_ns)
{
    uint32_t c, best = 0;
    float bestd = 1e30f;
    if (period_ns < 15259u || period_ns > 500000u)
        return 0;
    for (c = 36; c <= 76u; c++) {
        float p = 1.0e9f / s7_rate(c), d = (float)period_ns - p;
        d = d < 0.0f ? -d / p : d / p;
        if (d < bestd) {
            bestd = d;
            best = c;
        }
    }
    return best;
}

void sds_apply(const sds_dump_t *d, uint8_t *p)
{
    uint32_t len = sds_length(d), ls = d->loop_start, code = sds_rate_code(d->period_ns);
    int one_shot = ls + 5u > len || (d->total >= 5u && ls + 5u > d->total);
    if (ls >= len)
        ls = len ? len - 1u : 0u;
    p[S7_P_RPMODE] = one_shot ? S7_ONESHOT : d->alternating ? S7_ALT : S7_LOOP;
    s7_set_word(p, S7_P_SSTRT, 0);
    s7_set_word(p, S7_P_SEND, len);
    s7_set_word(p, S7_P_LEN1, len ? len - 1u : 0u);
    s7_set_word(p, S7_P_SLOOP, ls);
    s7_set_word(p, S7_P_LEN2, len > ls ? len - ls - 1u : 0u);
    p[S7_P_RECNP] = 60;
    if (code)
        p[S7_P_RECBW] = (uint8_t)code;
}

/* ------------------------------------------------------------------------------------------- sending --- */
static void put3(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v & 0x7Fu);
    b[1] = (uint8_t)(v >> 7 & 0x7Fu);
    b[2] = (uint8_t)(v >> 14 & 0x7Fu);
}

uint32_t sds_tx_header(sds_tx_t *t, const s7_slot_t *s, uint32_t slot, uint8_t h[SDS_HEADER])
{
    const uint8_t *p = s->p;
    uint32_t start = s7_word(p, S7_P_SSTRT), end = s7_word(p, S7_P_SEND), lp = s7_word(p, S7_P_SLOOP), n, ls;
    uint32_t mode = p[S7_P_RPMODE], code = p[S7_P_RECBW];
    if (end > s->words)
        end = s->words;
    if (start >= end)
        start = end ? end - 1u : 0u;
    n = end - start;              /* the sampler sends START to END */
    ls = (mode == S7_LOOP || mode == S7_ALT) && lp >= start && lp + 2u <= end ? lp - start : n;
    t->s = s;
    t->start = start;
    t->n = n;
    h[0] = 0xF0;
    h[1] = 0x7E;
    h[2] = 0x01;
    h[3] = (uint8_t)(slot & 0x7Fu);
    h[4] = 0;
    h[5] = 12;
    put3(h + 6, (uint32_t)(1.0e9f / s7_rate(code < 36u ? 36u : code > 76u ? 76u : code) + 0.5f));
    put3(h + 9, n);
    put3(h + 12, ls);
    put3(h + 15, n);
    h[18] = mode == S7_ALT ? 1u : 0u;
    return (n + SDS_WORDS_PER_BLOCK - 1u) / SDS_WORDS_PER_BLOCK;
}

void sds_tx_block(const sds_tx_t *t, uint32_t b, uint8_t blk[SDS_BLOCK])
{
    uint32_t i, chk = 0;
    blk[0] = (uint8_t)(b & 0x7Fu);
    for (i = 0; i < SDS_WORDS_PER_BLOCK; i++) {
        uint32_t at = b * SDS_WORDS_PER_BLOCK + i;
        uint32_t w = at < t->n ? (uint32_t)(s7_w12(t->s->data, t->start + at) + 2048) : 0u;   /* padded with zero
                                                                                                * bytes, as the X7000 */
        blk[1u + 2u * i] = (uint8_t)(w >> 5 & 0x7Fu);
        blk[2u + 2u * i] = (uint8_t)((w & 0x1Fu) << 2);
        chk ^= blk[1u + 2u * i] ^ blk[2u + 2u * i];
    }
    blk[SDS_BLOCK - 1u] = (uint8_t)chk;
}

uint32_t sds_build(const s7_slot_t *s, uint32_t slot, void (*put)(const uint8_t *p, uint32_t n, void *ctx), void *ctx)
{
    uint8_t h[SDS_HEADER], blk[SDS_BLOCK], f7 = 0xF7;
    sds_tx_t t;
    uint32_t nb = sds_tx_header(&t, s, slot, h), b;
    put(h, SDS_HEADER, ctx);
    for (b = 0; b < nb; b++) {
        sds_tx_block(&t, b, blk);
        put(blk, SDS_BLOCK, ctx);
    }
    put(&f7, 1, ctx);
    return SDS_HEADER + nb * SDS_BLOCK + 1u;
}

/* ------------------------------------------------------------------------------------ Akai exclusive --- */
int akai_is(const uint8_t *p, uint32_t n, uint32_t channel, uint32_t fn)
{
    return n >= 6u && p[0] == 0x47u && p[1] == channel && p[2] == fn && p[3] == AKAI_ID;
}

void sprm_build(uint8_t out[SPRM_LEN], uint32_t channel, uint32_t slot, const uint8_t *params)
{
    uint32_t k, chk = 0;
    out[0] = 0xF0;
    out[1] = 0x47;
    out[2] = (uint8_t)channel;
    out[3] = 0x0A;
    out[4] = AKAI_ID;
    out[5] = (uint8_t)slot;
    out[6] = 0;
    for (k = 0; k < S7_NPARAM; k++) {
        out[7u + 2u * k] = params[k] & 0x7Fu;
        out[8u + 2u * k] = params[k] >> 7;
        chk ^= out[7u + 2u * k] ^ out[8u + 2u * k];
    }
    out[317] = (uint8_t)chk;
    out[318] = 0xF7;
}

int32_t sprm_check(const uint8_t *p, uint32_t n, uint32_t channel)
{
    uint32_t i, chk = 0;
    if (n < SPRM_LEN - 2u || !akai_is(p, n, channel, 0x0Au) || p[4] >= S7_SLOTS)
        return -1;
    for (i = 6; i < 6u + 2u * S7_NPARAM; i++)
        chk ^= p[i];
    return chk == p[6u + 2u * S7_NPARAM] ? (int32_t)p[4] : -1;
}

void sprm_take(const uint8_t *p, uint8_t *params)
{
    uint32_t k;
    for (k = 0; k < S7_NPARAM; k++) {
        uint32_t lo = p[6u + 2u * k], hi = p[7u + 2u * k];
        if (hi < 2u)              /* Akai: 2 or more in the second byte keeps the value */
            params[k] = (uint8_t)(lo | hi << 7);
    }
}

void ovs_build(uint8_t out[OVS_LEN], uint32_t channel, const s7_ovs_t *o)
{
    const uint8_t v[8] = {o->channel, o->mode, o->ex_channel, o->omni, o->bend_range, o->prog_change_off, o->chomax,
                          o->afsens};
    uint32_t k, chk = 0;
    out[0] = 0xF0;
    out[1] = 0x47;
    out[2] = (uint8_t)channel;
    out[3] = 0x08;
    out[4] = AKAI_ID;
    out[5] = 0;
    out[6] = 0;
    for (k = 0; k < 8u; k++) {
        out[7u + 2u * k] = v[k] & 0x7Fu;
        out[8u + 2u * k] = v[k] >> 7;
    }
    for (k = 23; k < 31u; k++)
        out[k] = 0;
    for (k = 7; k < 31u; k++)
        chk ^= out[k];
    out[31] = (uint8_t)chk;
    out[32] = 0xF7;
}

int ovs_parse(const uint8_t *p, uint32_t n, s7_ovs_t *o)
{
    uint8_t *f[8] = {&o->channel, &o->mode, &o->ex_channel, &o->omni, &o->bend_range, &o->prog_change_off,
                     &o->chomax, &o->afsens};
    uint32_t k, chk = 0;
    if (n < OVS_LEN - 2u)
        return -1;
    for (k = 6; k < 30u; k++)
        chk ^= p[k];
    if (chk != p[30])
        return -1;
    for (k = 0; k < 8u; k++)
        if (p[7u + 2u * k] < 2u)
            *f[k] = (uint8_t)(p[6u + 2u * k] | p[7u + 2u * k] << 7);
    return 0;
}
