/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the MMT-8's memory dump (app/mmt8/mmtdump.c) against the MMT-8's SysEx document (documentation/manuals.md):
 * the 7-in-8 packing as its table has it, a dump made here by hand from the document taken in, ours sent and taken in
 * again the same, the header's other bytes kept, a dump too big for the sequencer taken in as far as it goes. Built and
 * run by test/run.sh */
#include <stdio.h>
#include <string.h>
#include "mmtdump.h"

static int fails, checks;
#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        checks++;                                                                                                     \
        if (!(cond)) {                                                                                                \
            fails++;                                                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                      \
            printf("\n");                                                                                             \
        }                                                                                                             \
    } while (0)

void sq_lock(void) {}
void sq_unlock(void) {}

static uint8_t scratch[SQ_SCRATCH], img[MD_MAX + 64], img2[MD_MAX + 64], rxbuf[MD_MAX + 64];

static uint8_t *take(uint32_t *max)
{
    *max = sizeof rxbuf;
    return rxbuf;
}

/* a dump through the receiver: F0 00 00 0E 00, the packed bytes, F7. The bytes received */
static uint32_t through_rx(const uint8_t *d, uint32_t n)
{
    md_rx_t rx = {0};
    uint32_t k, m = md_packed_len(n);
    const uint8_t pre[5] = {0xF0, 0x00, 0x00, 0x0E, 0x00};
    for (k = 0; k < 5u; k++)
        md_rx_byte(&rx, pre[k], take);
    for (k = 0; k < m; k++)
        md_rx_byte(&rx, md_packed_byte(d, n, k), take);
    md_rx_byte(&rx, 0xF7, take);
    return rx.st == MD_RX_DONE && !rx.over ? rx.n : 0xFFFFFFFFu;
}

static void test_packing(void)
{
    /* the document's table: A7..A1 / A0 B7..B2 / ...: 0xFF then six zeros, then 0x01 0x80 */
    const uint8_t a[7] = {0xFF, 0, 0, 0, 0, 0, 0x01}, b[2] = {0x01, 0x80};
    uint8_t want_a[8] = {0x7F, 0x40, 0, 0, 0, 0, 0, 0x01}, got[8];
    uint32_t k, n, bad = 0;
    for (k = 0; k < 8u; k++)
        got[k] = md_packed_byte(a, 7, k);
    CHECK(md_packed_len(7) == 8u && !memcmp(got, want_a, 8), "seven bytes in eight: %02X %02X .. %02X", got[0],
          got[1], got[7]);
    CHECK(md_packed_len(2) == 3u && md_packed_byte(b, 2, 0) == 0x00u && md_packed_byte(b, 2, 1) == 0x60u &&
              md_packed_byte(b, 2, 2) == 0x00u,
          "two bytes in three, the last padded: %02X %02X %02X", md_packed_byte(b, 2, 0), md_packed_byte(b, 2, 1),
          md_packed_byte(b, 2, 2));
    for (n = 1; n < 40u; n++) {   /* every length, through the receiver */
        for (k = 0; k < n; k++)
            img[k] = (uint8_t)(k * 37u + n * 11u + 5u);
        if (through_rx(img, n) != n || memcmp(rxbuf, img, n))
            bad++;
    }
    CHECK(bad == 0u, "packed and unpacked again, 1-39 bytes: %u wrong", bad);
}

/* ---- a dump made by hand, as the document has it ---- */
static uint32_t at;
static void b1(uint32_t v) { img[at++] = (uint8_t)v; }
static void le(uint32_t v) { b1(v & 255u), b1(v >> 8); }
static void eot(uint32_t clocks) { b1(0x80), le(clocks), b1(0), b1(0x80), b1(0), b1(0); }

/* part 00, 2 beats: track 1 (channel 3) a C3 + E3 chord at clock 0 (the E3 a 5-byte packet), program 5 at 96 and a
 * bend at 120, both on channel 2; track 2 a SysEx packet (not kept); song 00, tempo 100: part 00 all tracks, then part
 * 00 track 1 only. Bytes C8-CE (not to be altered) a pattern */
static uint32_t hand_dump(void)
{
    uint32_t part, song, t2, t1, k;
    memset(img, 0, MD_HDR);
    at = MD_HDR;
    part = at;
    img[0] = (uint8_t)((part + MD_BASE) >> 8), img[1] = (uint8_t)(part + MD_BASE);   /* big-endian */
    for (k = 0; k < 7u; k++)
        img[0xC8 + k] = (uint8_t)(0xA0 + k);
    le(0);                        /* the length: later */
    for (k = 0; k < 8u; k++)
        le(0);                    /* the tracks' offsets: later */
    le(0x0003);                   /* 2 beats: BCD 3 */
    for (k = 0; k < 8u; k++)
        b1(k == 7u ? 3u : 0u);    /* channels, track 8 .. 1: track 1 on 3 */
    memcpy(img + at, "VERSE ONE     ", 14);
    at += 14;
    for (k = 0; k < 6u; k++) {    /* tracks 8 .. 3: empty */
        img[part + 2 + 2 * k] = (uint8_t)(at - part), img[part + 3 + 2 * k] = (uint8_t)((at - part) >> 8);
        eot(192);
    }
    t2 = at;                      /* track 2: a SysEx at clock 10 */
    img[part + 14] = (uint8_t)(t2 - part), img[part + 15] = (uint8_t)((t2 - part) >> 8);
    b1(0x80 | 125), le(10), b1(0x80 | 0x43), b1(0x80 | 0x10), b1(0), b1(0x80);
    eot(192);
    t1 = at;                      /* track 1 */
    img[part + 16] = (uint8_t)(t1 - part), img[part + 17] = (uint8_t)((t1 - part) >> 8);
    b1(0x80 | 60), le(0), b1(100), b1(0), b1(0), b1(48);    /* C3, velocity 100, 48 clocks (msb, lsb) */
    b1(64), b1(90), b1(0), b1(0), b1(48);                   /* E3 on the same clock: 5 bytes */
    b1(0x80 | 122), le(96), b1(0x80 | 5), b1(1), b1(0), b1(0);   /* program 5, channel 2 */
    b1(0x80 | 124), le(120), b1(0x80), b1(1), b1(0x00), b1(0x50);   /* bend: lsb 0, msb 0x50 */
    eot(192);
    img[part] = (uint8_t)(at - part), img[part + 1] = (uint8_t)((at - part) >> 8);
    song = at;
    img[0x102] = (uint8_t)((song + MD_BASE) >> 8), img[0x103] = (uint8_t)(song + MD_BASE);
    le(0x11 + 4 + 1);
    b1(100);
    memcpy(img + at, "MY SONG       ", 14);
    at += 14;
    b1(0), b1(0xFF), b1(0), b1(0x01), b1(0xFF);
    img[0xCF] = (uint8_t)(at + MD_BASE), img[0xD0] = (uint8_t)((at + MD_BASE) >> 8);
    img[0xD3] = (uint8_t)(MD_END - at - MD_BASE), img[0xD4] = (uint8_t)((MD_END - at - MD_BASE) >> 8);
    return at;
}

static void test_import(void)
{
    md_report_t r;
    uint32_t n = hand_dump();
    const uint8_t *d;
    uint8_t part, mask;
    sq_clear();
    sq.offset[3] = -7;            /* ours: kept */
    CHECK(md_import(img, n, scratch, &r) == 0 && r.parts == 1u && r.songs == 1u && r.lost_parts == 0u, "taken in: %u parts, %u songs",
          r.parts, r.songs);
    CHECK(sq.part[0].beats == 2u && sq.part[0].chan[0] == 3u && sq.part[0].chan[1] == 0u &&
              !memcmp(sq.part[0].name, "VERSE ONE", 9),
          "part 00: %u beats, track 1 on %u", sq.part[0].beats, sq.part[0].chan[0]);
    CHECK(sq_events(0, 0) == 4u && sq_len(SQ_TRK(0, 0)) == 7u + 7u + 5u + 5u, "track 1: %u events",
          sq_events(0, 0));
    d = sq_event(0, 0, 0);
    CHECK(d && d[2] == 0x90u && d[3] == 60u && d[4] == 100u && sq_ev_tick(d) == 0u && sq_ev_dur(d) == 48u,
          "the C3: %02X %u %u at %u, %u long", d ? d[2] : 0, d ? d[3] : 0, d ? d[4] : 0, d ? sq_ev_tick(d) : 0,
          d ? sq_ev_dur(d) : 0);
    d = sq_event(0, 0, 1);
    CHECK(d && d[3] == 64u && d[4] == 90u && sq_ev_tick(d) == 0u, "the E3 on the same clock (a 5-byte packet)");
    d = sq_event(0, 0, 2);
    CHECK(d && d[2] == 0xC1u && d[3] == 5u && sq_ev_tick(d) == 96u, "program 5 on channel 2 at 96");
    d = sq_event(0, 0, 3);
    CHECK(d && d[2] == 0xE1u && d[3] == 0u && d[4] == 0x50u && sq_ev_tick(d) == 120u, "the bend at 120");
    CHECK(sq_events(0, 1) == 0u && r.skipped == 1u, "the SysEx not kept (%u)", r.skipped);
    sq_song_step(0, 1, &part, &mask);
    CHECK(sq_song_steps(0) == 2u && part == 0u && mask == 1u && sq.song[0].tempo == 100u &&
              !memcmp(sq.song[0].name, "MY SONG", 7),
          "song 00: %u steps, tempo %u", sq_song_steps(0), sq.song[0].tempo);
    CHECK(sq.offset[3] == -7 && sq.mmt8[0xC8] == 0xA0u && md_has_header() && sq_valid(),
          "the song offsets ours, the dump's header kept");
}

static void test_export(void)
{
    md_report_t r;
    int32_t n = md_export(img2, MD_MAX);
    uint32_t part = MD_HDR, t1, k, same = 1;
    uint8_t mem[SQ_ARENA];
    sq_mem_t was;
    CHECK(n > 0, "sent: %d bytes", n);
    if (n <= 0)
        return;
    CHECK(img2[0] == 0x06u && img2[1] == 0x00u && img2[2] == 0 && img2[3] == 0, "part 00 at 600H, part 01 none");
    CHECK(img2[part + 0x12] == 0x03u && img2[part + 0x13] == 0u && img2[part + 0x1B] == 3u,
          "its beats in BCD, + 1: %02X; track 1's channel (the header's last)", img2[part + 0x12]);
    t1 = part + (img2[part + 16] | (uint32_t)img2[part + 17] << 8);
    CHECK(img2[t1] == (0x80u | 60u) && img2[t1 + 1] == 0 && img2[t1 + 3] == 100u && img2[t1 + 5] == 0 &&
              img2[t1 + 6] == 48u && img2[t1 + 7] == 64u && img2[t1 + 8] == 90u,
          "track 1: a 7-byte packet, then the chord's 5-byte one");
    CHECK(!memcmp(img2 + t1 + 12, (const uint8_t[]){0x80 | 122, 96, 0, 0x85, 1, 0, 0}, 7), "the program change");
    for (k = 0; k < 7u; k++)
        same &= img2[0xC8 + k] == 0xA0u + k;
    CHECK(same, "the bytes not to be altered: as the MMT-8's dump had them");
    CHECK((img2[0xCF] | (uint32_t)img2[0xD0] << 8) == (uint32_t)n + MD_BASE &&
              (img2[0xD3] | (uint32_t)img2[0xD4] << 8) == MD_END - (uint32_t)n - MD_BASE,
          "the free memory: from %04X", img2[0xCF] | (uint32_t)img2[0xD0] << 8);
    /* ours again, the same: our memory as it was */
    memcpy(&was, &sq, sizeof was - SQ_ARENA);
    memcpy(mem, sq.arena, sq.used);
    CHECK(md_import(img2, (uint32_t)n, scratch, &r) == 0 && r.parts == 1u && r.songs == 1u, "taken in again");
    CHECK(sq_len(SQ_TRK(0, 0)) == was.blob[SQ_TRK(0, 0)].len &&
              !memcmp(sq_data(SQ_TRK(0, 0)), mem + was.blob[SQ_TRK(0, 0)].off, sq_len(SQ_TRK(0, 0))) &&
              !memcmp(sq.part, was.part, sizeof sq.part) && !memcmp(sq.song, was.song, sizeof sq.song),
          "the same parts, tracks and songs");
    CHECK(md_export(img2, 0x240) == -1, "too big for the room (0x240 bytes): refused");
}

/* five parts of 2,000 chord notes (a 5-byte packet each on the MMT-8, 7 bytes ours): the fifth no longer fits */
static void test_too_big(void)
{
    md_report_t r;
    uint32_t p, k, n;
    memset(img, 0, MD_HDR);
    at = MD_HDR;
    for (p = 0; p < 5u; p++) {
        uint32_t part = at;
        img[2 * p] = (uint8_t)((part + MD_BASE) >> 8), img[2 * p + 1] = (uint8_t)(part + MD_BASE);
        at += MD_PART_HDR;
        img[part + 0x12] = 0x02;  /* 1 beat */
        memcpy(img + part + 0x1C, "CHORDS        ", 14);
        for (k = 0; k < 7u; k++) {
            img[part + 2 + 2 * k] = (uint8_t)(at - part), img[part + 3 + 2 * k] = (uint8_t)((at - part) >> 8);
            eot(96);
        }
        img[part + 16] = (uint8_t)(at - part), img[part + 17] = (uint8_t)((at - part) >> 8);
        b1(0x80 | 0), le(0), b1(1), b1(0), b1(0), b1(10);
        for (k = 1; k < 2000u; k++)
            b1(k & 127u), b1(1), b1(0), b1(0), b1(10);
        eot(96);
        img[part] = (uint8_t)(at - part), img[part + 1] = (uint8_t)((at - part) >> 8);
    }
    n = at;
    sq_clear();
    CHECK(n <= MD_MAX, "the dump fits an MMT-8: %u bytes", n);
    CHECK(md_import(img, n, scratch, &r) == 0 && r.parts == 4u && r.lost_parts == 1u && r.first_lost == 4 &&
              r.full && !r.cut && !r.bad,
          "four taken in, the fifth not (no room: full %u cut %u bad %u): %u, %u lost, first %d", r.full, r.cut, r.bad,
          r.parts, r.lost_parts, r.first_lost);
    CHECK(sq_part_empty(4) && sq_valid() && sq_events(3, 0) == 2000u, "the memory holds together");
}

/* two parts and a song, its header saying its length: whole, then cut short (bytes lost at the end, or one dropped in
 * the middle: all after it shifted), then damaged (as long as it says, a part's end-of-track gone) */
static uint32_t two_parts(void)
{
    uint32_t p, k, song;
    memset(img, 0, MD_HDR);
    at = MD_HDR;
    for (p = 0; p < 2u; p++) {
        uint32_t part = at;
        img[2 * p] = (uint8_t)((part + MD_BASE) >> 8), img[2 * p + 1] = (uint8_t)(part + MD_BASE);
        at += MD_PART_HDR;
        img[part + 0x12] = 0x02;  /* 1 beat */
        for (k = 0; k < 7u; k++) {
            img[part + 2 + 2 * k] = (uint8_t)(at - part), img[part + 3 + 2 * k] = (uint8_t)((at - part) >> 8);
            eot(96);
        }
        img[part + 16] = (uint8_t)(at - part), img[part + 17] = (uint8_t)((at - part) >> 8);
        for (k = 0; k < 20u; k++)
            b1(0x80 | (40u + k)), le(k * 4u), b1(100), b1(0), b1(0), b1(2);
        eot(96);
        img[part] = (uint8_t)(at - part), img[part + 1] = (uint8_t)((at - part) >> 8);
    }
    song = at;
    img[0x102] = (uint8_t)((song + MD_BASE) >> 8), img[0x103] = (uint8_t)(song + MD_BASE);
    le(0x11 + 4 + 1);
    b1(120);
    memcpy(img + at, "BOTH          ", 14);
    at += 14;
    b1(0), b1(1), b1(1), b1(1), b1(0xFF);
    img[0xCF] = (uint8_t)(at + MD_BASE), img[0xD0] = (uint8_t)((at + MD_BASE) >> 8);
    return at;
}

static void test_cut_short(void)
{
    md_report_t r;
    uint32_t n = two_parts(), part1 = (uint32_t)(img[2] << 8 | img[3]) - MD_BASE;
    CHECK(md_want(img, 0xD0) == 0u && md_want(img, n) == n, "its length from its header: %u (%u)", md_want(img, n), n);
    sq_clear();
    CHECK(md_import(img, n, scratch, &r) == 0 && r.parts == 2u && r.songs == 1u && !r.cut && !r.bad && !r.full &&
              r.got == n && r.want == n,
          "whole: %u parts, %u songs, %u of %u bytes", r.parts, r.songs, r.got, r.want);
    sq_clear();
    (void)md_import(img, part1 + 40u, scratch, &r);   /* the end lost: part 01 and the song */
    CHECK(r.cut && !r.bad && !r.full && r.parts == 1u && r.songs == 0u && r.lost_parts == 1u && r.lost_songs == 1u &&
              r.first_lost == 1 && r.want == n && sq_valid() && sq_part_empty(1),
          "the end lost: cut (%u bad %u full %u), %u parts, %u songs, first lost %d", r.cut, r.bad, r.full, r.parts,
          r.songs, r.first_lost);
    memcpy(img2, img, n);         /* a byte dropped inside part 01: it no longer reads, the song past the end */
    memmove(img2 + part1 + 60u, img2 + part1 + 61u, n - part1 - 61u);
    sq_clear();
    (void)md_import(img2, n - 1u, scratch, &r);
    CHECK(r.cut && !r.bad && r.parts == 1u && r.songs == 0u && r.first_lost == 1,
          "a byte dropped: cut (%u bad %u), %u parts, %u songs, first lost %d", r.cut, r.bad, r.parts, r.songs,
          r.first_lost);
    memcpy(img2, img, n);         /* whole, but part 01's last end-of-track gone: damaged */
    img2[part1 + (img[part1] | (uint32_t)img[part1 + 1] << 8) - 7u] = 0x00;
    sq_clear();
    (void)md_import(img2, n, scratch, &r);
    CHECK(r.bad && !r.cut && !r.full && r.parts == 1u && r.songs == 1u && r.first_lost == 1,
          "damaged: bad (%u cut %u), %u parts, %u songs, first lost %d", r.bad, r.cut, r.parts, r.songs, r.first_lost);
    memcpy(img2, img, n);         /* whole, but in part 01's track 1 the 6th note's clock 2, after 16: back in time */
    img2[part1 + (img[part1 + 16] | (uint32_t)img[part1 + 17] << 8) + 7u * 5u + 1u] = 2u;
    sq_clear();
    (void)md_import(img2, n, scratch, &r);
    CHECK(r.bad && !r.cut && !r.full && r.parts == 1u && r.songs == 1u && r.first_lost == 1 && sq_part_empty(1) &&
              sq_valid(),
          "a clock back in time: bad (%u cut %u), %u parts, %u songs, first lost %d", r.bad, r.cut, r.parts, r.songs,
          r.first_lost);
    memcpy(img2, img, n);         /* a header saying nothing (zeros): the end lost still told, by part 01 past it */
    img2[0xCF] = img2[0xD0] = 0;
    sq_clear();
    (void)md_import(img2, part1 + 40u, scratch, &r);
    CHECK(r.cut && r.want == 0u && r.parts == 1u, "no length said, the end lost: cut (%u), %u parts", r.cut, r.parts);
}

int main(void)
{
    test_packing();
    test_import();
    test_export();
    test_too_big();
    test_cut_short();
    printf("mmtdump: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
