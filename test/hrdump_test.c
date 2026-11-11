/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the HR-16's memory dump (app/hr16/hrdump.c) against its service manual's format (documentation/
 * hr16-sysex.md): its sounds by kind both ways, the tuning's steps, ours made and laid out as the manual has it, taken
 * in again the same (and sent again byte for byte), a dump made here by hand taken in (the manual's empty pattern, a
 * song counted, the quantize and swing, a hit at the very end), one too big for an HR-16, one too big for the
 * sequencer's memory taken in as far as it goes. Built and run by test/run.sh */
#include <stdio.h>
#include <string.h>
#include "hrdump.h"
#include "mmtdump.h"
#include "player.h"

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
void sqp_emit(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }
void sqp_click(int accent) { (void)accent; }
void sqp_midi_rt(uint32_t b) { (void)b; }
void sqp_on_tick(void) {}
void hr_emit(uint32_t pad, uint32_t vel) { (void)pad, (void)vel; }

static uint8_t scratch[SQ_SCRATCH], img[HD_MAX + 64], img2[HD_MAX + 64], rxbuf[HD_MAX + 64];

/* SM-87's kit as resources/kit has it: its five sounds, its six pads */
static const uint8_t DATA[3] = {0x80, 0x08, 0x00};
static const hr_kit_sound_t KIT[5] = {
    {DATA, 2, 48, 60, "KICK"}, {DATA, 2, 48, 60, "SNARE"}, {DATA, 2, 48, 60, "CLHAT"},
    {DATA, 2, 48, 60, "OPHAT"}, {DATA, 2, 48, 60, "CYMBL"},
};
static const hr_kit_pad_t KIT_PAD[6] = {
    {"KICK", {0, 0, 85, 0, 36, 0, 1, 0}},  {"SNARE", {1, 0, 85, 0, 38, 0, 1, 0}}, {"CLHAT", {2, 0, 85, 0, 42, 1, 1, 0}},
    {"OPHAT", {3, 0, 85, 0, 46, 1, 1, 0}}, {"CRASH", {4, 0, 85, 0, 49, 0, 2, 0}}, {"RIDE", {4, -3, 85, 0, 51, 0, 2, 0}},
};
#define SLOT(n) (5u + (n) - 1u)

static void setup(void)
{
    uint32_t i;
    sq_clear();
    hr_kit_use(KIT, 5, KIT_PAD, 6);
    for (i = 0; i < HR_PADS; i++) {
        hr_pad_default(i);
        hr_std[i] = hr_pad[i];
    }
}

static const hd_settings_t SET = {3, 54, 9, 120};   /* 1/16, 54%, channel 10, 120 BPM */

static uint8_t *take(uint32_t *max)
{
    *max = sizeof rxbuf;
    return rxbuf;
}

/* a dump through the receiver, with the HR-16's prefix: the bytes received */
static uint32_t through_rx(const uint8_t *d, uint32_t n)
{
    md_rx_t rx = {0};
    uint32_t k, m = md_packed_len(n);
    rx.prefix = HD_PRE;
    rx.plen = HD_PREFIX;
    for (k = 0; k < HD_PREFIX; k++)
        md_rx_byte(&rx, HD_PRE[k], take);
    for (k = 0; k < m; k++)
        md_rx_byte(&rx, md_packed_byte(d, n, k), take);
    md_rx_byte(&rx, 0xF7, take);
    return rx.st == MD_RX_DONE && !rx.over ? rx.n : 0xFFFFFFFFu;
}

static void test_mapping(void)
{
    int32_t s, bad = 0;
    setup();
    hr_std[8].sound = (uint8_t)SLOT(3);  /* TOM 1's pad: a slot, set globally */
    CHECK(hd_sound_in(0, 0) == 0u && hd_sound_in(7, 0) == 0u && hd_sound_in(15, 1) == 1u && hd_sound_in(16, 1) == 1u,
          "kicks the kit's KICK, a rimshot and a sidestick its SNARE: %u %u", hd_sound_in(15, 1), hd_sound_in(16, 1));
    CHECK(hd_sound_in(23, 2) == 2u && hd_sound_in(27, 2) == 2u && hd_sound_in(25, 6) == 3u && hd_sound_in(26, 3) == 3u,
          "closed hats CLHAT, half open and open OPHAT");
    CHECK(hd_sound_in(28, 5) == 4u && hd_sound_in(29, 5) == 4u && hd_sound_in(30, 4) == 4u,
          "the rides and the crash the kit's one cymbal");
    CHECK(hd_sound_in(18, 8) == SLOT(3) && hd_sound_in(42, 12) == HR_NO_SOUND && hd_sound_in(46, 7) == HR_NO_SOUND &&
              hd_sound_in(60, 8) == SLOT(3),
          "a tom, a cowbell, claps: the pad's own as set globally (%u %u)", hd_sound_in(18, 8), hd_sound_in(42, 12));
    CHECK(hd_sound_out(0, 0) == 0u && hd_sound_out(1, 1) == 10u && hd_sound_out(2, 2) == 23u &&
              hd_sound_out(3, 4) == 26u && hd_sound_out(3, 3) == 25u,
          "ours on their drums: a kick, a snare, the hats (half open on the MID HAT: %u)", hd_sound_out(3, 3));
    CHECK(hd_sound_out(4, 12) == 28u && hd_sound_out(4, 13) == 30u && hd_sound_out(4, 0) == 30u,
          "the cymbal a ride on the RIDE, else a crash: %u %u %u", hd_sound_out(4, 12), hd_sound_out(4, 13),
          hd_sound_out(4, 0));
    CHECK(hd_sound_out(1, 5) == 10u && hd_sound_out(0, 8) == 0u, "another kind than the drum's: the kind's");
    CHECK(hd_sound_out(SLOT(3), 8) == 18u && hd_sound_out(HR_NO_SOUND, 14) == 42u,
          "a slot, none: the drum's sound (%u %u)", hd_sound_out(SLOT(3), 8), hd_sound_out(HR_NO_SOUND, 14));
    CHECK(hd_tune_in(16) == 0 && hd_tune_in(0) == -9 && hd_tune_in(31) == 8 && hd_tune_in(18) == 1 &&
              hd_tune_in(0xE0u | 16u) == 0,
          "the pitch's steps as semitones: %d %d %d", hd_tune_in(0), hd_tune_in(31), hd_tune_in(18));
    CHECK(hd_tune_out(0) == 16u && hd_tune_out(1) == 18u && hd_tune_out(-24) == 0u && hd_tune_out(24) == 31u,
          "semitones as steps: %u %u %u", hd_tune_out(1), hd_tune_out(-24), hd_tune_out(24));
    for (s = -9; s <= 8; s++)
        bad += hd_tune_in(hd_tune_out(s)) != s;
    CHECK(!bad, "-9..8 semitones there and back: %d wrong", bad);
}

static void hit(uint32_t p, uint32_t tick, uint32_t pad, uint32_t vel) { hr_add(p, tick, pad, vel, scratch); }

static void test_export(void)
{
    int32_t n;
    uint32_t end;
    const uint8_t ev0[8] = {0x70, 0x32, 0xB0, 0x01, 0xE6, 0x6D, 0xAA, 0xFF};
    const uint8_t ev5[4] = {0xFE, 0xFE, 0xA4, 0xFF};
    setup();
    hit(0, 0, 0, 127);            /* the kick, the closed hat; the snare; the crash */
    hit(0, 0, 2, 64);
    hit(0, 48, 1, 1);
    hit(0, 150, 4, 100);
    sq.pattern[0].beats = 2;
    sq.pattern[5].beats = 3;      /* (no hits, a length of its own) */
    hr_std[1].tune = 1;
    hr_std[1].pan = -3;
    hr_std[1].level = 70;
    n = hd_export(img, HD_MAX, &SET);
    CHECK(n > 0 && !hd_has_header(), "made: %d bytes", n);
    CHECK(img[0xFE] == 0x27u && img[0xFF] == 0xB5u && img[0xC8] == 9u && img[0xCC] == 36u && img[0xCD] == 38u &&
              img[0xCE] == 42u && img[0xD0] == 46u && img[0xD8] == 51u && img[0xD9] == 49u && img[0xE6] == 1u,
          "a header made up: the manual's two bytes, channel %u, the notes %u %u %u", img[0xC8], img[0xCC], img[0xCE],
          img[0xD9]);
    CHECK(img[0x2D8] == 120u && img[0x2D5] == 4u && img[0x2D9] == 24u && img[0x2DA] == 4u,
          "tempo %u, swing %u, quantize %u clocks (%u)", img[0x2D8], img[0x2D5], img[0x2D9], img[0x2DA]);
    CHECK(img[0x200] == 0x86u && img[0x201] == 0x00u && img[0x202] == 0 && img[0x203] == 0 && img[0x20A] == 0x86u &&
              img[0x20B] == 0x3Cu,
          "the pointers: pattern 00 at 8600H, 05 after it at %02X%02X", img[0x20A], img[0x20B]);
    CHECK(img[0x400] == 0x3Cu && img[0x401] == 0 && img[0x402] == 0x02u && img[0x403] == 0,
          "pattern 00: %u bytes, BCD beats %02X", img[0x400], img[0x402]);
    CHECK(img[0x404] == 0u && img[0x405] == 85u && img[0x406] == (3u << 5 | 16u) && img[0x407] == 10u &&
              img[0x408] == 70u && img[0x409] == 18u,
          "its kick: sound %u level %u pan/pitch %02X; its snare: %u %u %02X", img[0x404], img[0x405], img[0x406],
          img[0x407], img[0x408], img[0x409]);
    CHECK(img[0x404 + 3 * 12] == 28u && img[0x404 + 3 * 13] == 30u && img[0x404 + 3 * 8] == 18u,
          "the ride a ride, the crash a crash, TOM 1 a tom: %u %u %u", img[0x404 + 36], img[0x404 + 39],
          img[0x404 + 24]);
    CHECK(!memcmp(img + 0x434, ev0, 8), "its events: %02X %02X %02X %02X %02X %02X %02X %02X", img[0x434], img[0x435],
          img[0x436], img[0x437], img[0x438], img[0x439], img[0x43A], img[0x43B]);
    CHECK(img[0x43C] == 0x38u && img[0x43E] == 0x03u && !memcmp(img + 0x43C + 0x34, ev5, 4),
          "pattern 05: %u bytes, 3 beats waited: %02X %02X %02X", img[0x43C], img[0x470], img[0x471], img[0x472]);
    end = 0x43C + 0x38;
    CHECK(img[0x3C8] == (uint8_t)((end + HD_BASE) >> 8) && img[0x3C9] == (uint8_t)(end + HD_BASE) &&
              img[end] == 3u && img[end + 1u] == 0u && img[end + 2u] == 0xFFu && (uint32_t)n == end + 3u,
          "song 99 empty at the end (%02X%02X), %d bytes", img[0x3C8], img[0x3C9], n);
    end += 3u + HD_BASE;
    CHECK(img[0x2CD] == (uint8_t)end && img[0x2CE] == (uint8_t)(end >> 8) &&
              (img[0x2D1] | img[0x2D2] << 8) == (int)(HD_END - end),
          "the free memory: from %02X%02X, %02X%02X bytes", img[0x2CE], img[0x2CD], img[0x2D2], img[0x2D1]);
}

static void test_round_trip(void)
{
    int32_t n, n2;
    uint32_t len;
    hd_report_t r;
    hr_pad_t pads[HR_PADS];
    setup();
    hit(0, 0, 0, 127);
    hit(0, 0, 2, 64);
    hit(0, 48, 1, 1);
    hit(0, 150, 4, 100);
    sq.pattern[0].beats = 2;
    hit(42, 700, 9, 90);          /* (TOM 2's pad) */
    sq.pattern[5].beats = 3;
    hr_std[1].tune = 1;
    hr_std[1].pan = -3;
    hr_std[1].level = 70;
    {
        uint8_t ev[5] = {0, 0, 0, 1, 0};   /* (an MMT-8 part: kept) */
        sq_replace(SQ_TRK(4, 2), ev, 5);
        sq.part[4].beats = 1;
    }
    n = hd_export(img, HD_MAX, &SET);
    len = through_rx(img, (uint32_t)n);
    CHECK(len == (uint32_t)n && !memcmp(rxbuf, img, len), "through the receiver: %u of %d bytes", len, n);
    hit(9, 0, 3, 50);             /* (gone as it comes in) */
    CHECK(hd_import(rxbuf, len, scratch, &r) == 0 && r.patterns == 3u && !r.lost && !r.songs && r.quant == 3 &&
              r.swing == 54,
          "taken in: %u patterns, %u lost, %u songs, quantize %d, swing %d", r.patterns, r.lost, r.songs, r.quant,
          r.swing);
    CHECK(hr_hits(0) == 4u && hr_beats(0) == 2u && hr_beats(5) == 3u && hr_hits(42) == 1u && hr_beats(42) == 8u &&
              !hr_hits(9) && !sq.pattern[9].beats && hd_has_header() && sq_events(4, 2) == 1u,
          "its patterns: 00 %u hits %u beats, 05 %u beats, 42 %u hits; 09 empty; the part kept", hr_hits(0),
          hr_beats(0), hr_beats(5), hr_hits(42));
    CHECK(hr_hit_at(0, 0)[2] == 0u && hr_hit_at(0, 0)[3] == 127u && hr_hit_at(0, 1)[2] == 2u &&
              hr_hit_at(0, 1)[3] == 63u && hr_hit_at(0, 2)[3] == 15u && hr_hit_tick(hr_hit_at(0, 3)) == 150u &&
              hr_hit_at(0, 3)[2] == 4u && hr_hit_at(0, 3)[3] == 111u && hr_hit_at(42, 0)[2] == 9u &&
              hr_hit_tick(hr_hit_at(42, 0)) == 700u,
          "the hits: velocities %u %u %u %u (8 levels)", hr_hit_at(0, 0)[3], hr_hit_at(0, 1)[3], hr_hit_at(0, 2)[3],
          hr_hit_at(0, 3)[3]);
    hr_sounds_get(0, pads);
    CHECK(hr_sounds_own(0) && hr_sounds_own(5) && !hr_sounds_own(9) && pads[1].sound == 1u && pads[1].tune == 1 &&
              pads[1].pan == -3 && pads[1].level == 70u && pads[4].sound == 4u && pads[5].sound == 4u &&
              pads[5].tune == -3 && pads[8].sound == HR_NO_SOUND,
          "their own sounds: the snare %u tune %d pan %d level %u; the ride's tune %d", pads[1].sound, pads[1].tune,
          pads[1].pan, pads[1].level, pads[5].tune);
    n2 = hd_export(img2, HD_MAX, &SET);
    CHECK(n2 == n && !memcmp(img, img2, (size_t)n), "sent again: the same, %d bytes (%d)", n2, n);
}

/* a dump made by hand, as the manual has it */
static uint32_t pat_at(uint8_t *m, uint32_t at, uint32_t p, uint32_t beats_bcd, const uint8_t *ev, uint32_t nev)
{
    uint32_t d, size = HD_PAT_HDR + nev;
    m[0x200 + 2 * p] = (uint8_t)((at + HD_BASE) >> 8);
    m[0x201 + 2 * p] = (uint8_t)(at + HD_BASE);
    m[at] = (uint8_t)size;
    m[at + 1] = (uint8_t)(size >> 8);
    m[at + 2] = (uint8_t)beats_bcd;
    m[at + 3] = (uint8_t)(beats_bcd >> 8);
    for (d = 0; d < HD_DRUMS; d++) {
        m[at + 4 + 3 * d] = 42;   /* every drum a cowbell, but */
        m[at + 5 + 3 * d] = 0x80u | 77u;   /* (output 2) */
        m[at + 6 + 3 * d] = 6u << 5 | 0u;   /* right, -16 */
    }
    m[at + 4] = 2;                /* the kick 22" power kick */
    memcpy(m + at + HD_PAT_HDR, ev, nev);
    return at + size;
}

static void test_document(void)
{
    static uint8_t m[HD_MAX];
    const uint8_t empty8[8] = {0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x8C, 0xFF};   /* the manual's */
    /* TOM 3 (dynamics 5) and the kick (7); 95 clocks, the kick (0); 97 more, a snare (0) at the very end (192: 2
     * beats), round to the start */
    const uint8_t ev[7] = {0x5A, 0x70, 0xDF, 0x00, 0xE1, 0x01, 0xFF};
    const uint8_t zero[1] = {0xFF};
    uint32_t at = HD_HDR, i;
    hd_report_t r;
    hr_pad_t pads[HR_PADS];
    setup();
    memset(m, 0, sizeof m);
    m[0xFE] = 0x27;
    m[0xFF] = 0xB5;
    m[0xC8] = 3;
    m[0x0A] = 0x5A;               /* (a byte not to alter) */
    m[0x2D5] = 7;
    m[0x2DA] = 1;                 /* 1/6 */
    at = pat_at(m, at, 2, 0x0008, empty8, 8);
    at = pat_at(m, at, 3, 0x0000, zero, 1);   /* 0 beats: empty */
    at = pat_at(m, at, 4, 0x0002, ev, 7);
    m[0x200 + 2 * 50] = 0x12;     /* a pointer before the patterns: none */
    m[0x302 + 2 * 3] = (uint8_t)((at + HD_BASE) >> 8);   /* song 03: pattern 04 twice */
    m[0x303 + 2 * 3] = (uint8_t)(at + HD_BASE);
    m[at] = 5, m[at + 2] = 4, m[at + 3] = 4, m[at + 4] = 0xFF;
    at += 5;
    m[0x302 + 2 * 99] = (uint8_t)((at + HD_BASE) >> 8);   /* song 99 empty */
    m[0x303 + 2 * 99] = (uint8_t)(at + HD_BASE);
    m[at] = 3, m[at + 2] = 0xFF;
    at += 3;
    CHECK(hd_import(m, at, scratch, &r) == 0 && r.patterns == 2u && !r.lost && r.songs == 1u && r.quant == 1 &&
              r.swing == 57,
          "taken in: %u patterns, %u songs not, quantize %d (1/6: 1/8), swing %d", r.patterns, r.songs, r.quant,
          r.swing);
    CHECK(hr_beats(2) == 8u && !hr_hits(2) && hr_sounds_own(2) && !sq.pattern[3].beats && !hr_sounds_own(3) &&
              !hr_hits(50),
          "the manual's empty pattern: %u beats; a pattern of 0 beats empty", hr_beats(2));
    CHECK(hr_beats(4) == 2u && hr_hits(4) == 4u && hr_hit_at(4, 0)[2] == 0u && hr_hit_at(4, 0)[3] == 127u &&
              hr_hit_tick(hr_hit_at(4, 1)) == 0u && hr_hit_at(4, 1)[2] == 1u && hr_hit_at(4, 1)[3] == 15u &&
              hr_hit_at(4, 2)[2] == 10u && hr_hit_at(4, 2)[3] == 95u && hr_hit_tick(hr_hit_at(4, 3)) == 95u &&
              hr_hit_at(4, 3)[2] == 0u && hr_hit_at(4, 3)[3] == 15u,
          "pattern 04: %u beats, %u hits: the kick, the snare round from the end, TOM 3 (pad %u, %u), the kick",
          hr_beats(4), hr_hits(4), hr_hit_at(4, 2)[2] + 1u, hr_hit_at(4, 2)[3]);
    hr_sounds_get(4, pads);
    CHECK(pads[0].sound == 0u && pads[0].level == 77u && pads[0].pan == 3 && pads[0].tune == -9 &&
              pads[12].sound == HR_NO_SOUND,
          "its sounds: a kick the KICK, level %u pan %d tune %d; a cowbell nothing (the pad's)", pads[0].level,
          pads[0].pan, pads[0].tune);
    for (i = 0; i < HD_HDR && sq.hr16[i] == m[i]; i++)
        ;
    CHECK(i == HD_HDR && sq.hr16_voice[0] == 2u && sq.hr16_voice[13] == 42u, "its header kept, its sounds: %u %u",
          sq.hr16_voice[0], sq.hr16_voice[13]);
    CHECK(hd_export(img, HD_MAX, &SET) > 0 && img[0x0A] == 0x5Au && img[0xC8] == 3u && img[0x404] == 2u &&
              img[0x404 + 3 * 13] == 30u && img[0x404 + 3 * 8] == 42u,
          "ours: its header's bytes, the kick its kick (%u), the crash a crash (%u), TOM 1 its sound (%u)",
          img[0x404], img[0x404 + 39], img[0x404 + 24]);
}

static void test_limits(void)
{
    uint32_t p, k, m;
    int32_t n;
    hd_report_t r;
    static uint8_t h[32768];
    setup();
    for (p = 0; p < 24u; p++) {   /* 24 patterns of 500 hits 130 clocks apart: 3 bytes each, more than an HR-16 holds */
        for (k = 0; k < 500u; k++) {
            uint32_t t = k * 130u;
            h[4 * k] = (uint8_t)t, h[4 * k + 1] = (uint8_t)(t >> 8), h[4 * k + 2] = (uint8_t)(k % 16u),
                  h[4 * k + 3] = 100;
        }
        CHECK(sq_replace(SQ_PAT(p), h, 4u * 500u) == 0, "pattern %u in", p);
        sq.pattern[p].beats = SQ_MAX_BEATS;
    }
    CHECK(hd_export(img, HD_MAX, &SET) == -1, "too big for an HR-16");

    /* ours of 8 patterns of 1,800 hits fit an HR-16, not the sequencer with a part of 30,000 bytes in it */
    setup();
    for (p = 0; p < 8u; p++) {
        for (k = 0; k < 1800u; k++) {
            uint32_t t = k / 16u * 2u, pad = k % 16u;
            h[4 * k] = (uint8_t)t, h[4 * k + 1] = (uint8_t)(t >> 8), h[4 * k + 2] = (uint8_t)pad, h[4 * k + 3] = 100;
        }
        sq_replace(SQ_PAT(p), h, 4u * 1800u);
        sq.pattern[p].beats = 3;
    }
    n = hd_export(img, HD_MAX, &SET);
    CHECK(n > 0, "8 patterns of 1,800 hits: %d bytes", n);
    setup();
    for (k = 0; k < 6000u; k++)   /* a part's track: 30,000 bytes */
        h[5 * k] = (uint8_t)k, h[5 * k + 1] = (uint8_t)(k >> 8), h[5 * k + 2] = 0xB0, h[5 * k + 3] = 7,
                  h[5 * k + 4] = 100;
    sq_replace(SQ_TRK(1, 0), h, 30000u);
    m = sq.used;
    CHECK(hd_import(img, (uint32_t)n, scratch, &r) == 0 && r.lost && r.patterns + r.lost == 8u && r.first_lost > 0 &&
              hr_hits(0) == 1800u && !hr_hits((uint32_t)r.first_lost) && sq_len(SQ_TRK(1, 0)) == 30000u,
          "taken in as far as it goes: %u, from %d on lost (the part kept, %u bytes used before)", r.patterns,
          r.first_lost, m);
}

int main(void)
{
    test_mapping();
    test_export();
    test_round_trip();
    test_document();
    test_limits();
    printf("hrdump: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
