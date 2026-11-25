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

/* SM-87's kit as resources/kit has it: its five sounds, its seven pads */
static const uint8_t DATA[3] = {0x80, 0x08, 0x00};
static const hr_kit_sound_t KIT[5] = {
    {DATA, 2, 48, 60, "KICK"}, {DATA, 2, 48, 60, "SNARE"}, {DATA, 2, 48, 60, "CLHAT"},
    {DATA, 2, 48, 60, "OPHAT"}, {DATA, 2, 48, 60, "CYMBL"},
};
static const hr_kit_pad_t KIT_PAD[7] = {
    {"KICK", {0, 0, 85, 0, 36, 0, 1, 0, 0, 0}},
    {"SNARE", {1, 0, 85, 0, 38, 0, 1, 0, 0, 0}},
    {"CLHAT", {2, 0, 85, 0, 42, 1, 1, 0, 0, 0}},
    {"OPHAT", {3, 0, 85, 0, 46, 1, 1, 0, 0, 0}},
    {"CRASH", {4, 0, 85, 0, 49, 0, 2, 0, 0, 0}},
    {"RIDE", {4, -3, 85, 0, 51, 0, 2, 0, 0, 0}},
    {"MDHAT", {3, 0, 85, 0, 44, 1, 1, 0, 0, 0}},
};
#define SLOT(n) (5u + (n) - 1u)

static void setup(void)
{
    uint32_t i;
    sq_clear();
    hr_kit_use(KIT, 5, KIT_PAD, 7);
    for (i = 0; i < HR_PADS; i++) {
        hr_pad_default(i);
        hr_std[i] = hr_pad[i];
    }
}

/* 1/16, swing 2 clocks (54.2%), channel 10, 120 BPM, program change off */
static const hd_settings_t SET = {3, 2, 9, 120, 0, 0};

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

/* sound snd on drum d, as ours sends it (a sound chosen here: none as it came) */
static uint32_t out_as(uint32_t snd, uint32_t d)
{
    hr_pad_t x = hr_std[0];
    x.sound = (uint8_t)snd;
    x.hr16 = 0;
    return hd_sound_out(&x, d);
}

static void test_mapping(void)
{
    int32_t s, bad = 0;
    setup();
    hr_std[8].sound = (uint8_t)SLOT(3);  /* TOM 1's pad: a slot, set globally */
#define IN(n) hd_sound_in(n, 0, hr_std)
#define IN_B(n) hd_sound_in(n, 1, hr_std)
    CHECK(IN(0) == 0u && IN(7) == 0u && IN(11) == 1u && IN(15) == 1u && IN(16) == 1u,
          "kicks the KICK pad's sound, the snares, a rimshot and a sidestick the SNARE's: %u %u", IN(15), IN(16));
    CHECK(IN(23) == 2u && IN(27) == 2u && IN(25) == 3u && IN(26) == 3u,
          "closed hats the CLS HAT's, half open the MID HAT's, open the OPN HAT's (the kit's OPHAT both)");
    CHECK(IN(28) == 4u && IN(29) == 4u && IN(30) == 4u, "the rides the RIDE's, the crash the CRASH's: the one cymbal");
    CHECK(IN(18) == SLOT(3) && IN(20) == SLOT(3) && IN(19) == HR_NO_SOUND,
          "the 10\" toms TOM 1's (the kit none: the global set-up's, a slot), the 16\" TOM 3's (none): %u %u", IN(18),
          IN(19));
    CHECK(IN(42) == HR_NO_SOUND && IN(48) == HR_NO_SOUND && IN(46) == HR_NO_SOUND && IN(45) == HR_NO_SOUND &&
              IN(60) == HR_NO_SOUND,
          "a cowbell, the sticks (no drum's): none; claps, the tambourine (their pads have none): none");
    CHECK(!hd_is_b(img) && IN_B(0) == 0u && IN_B(7) == 1u && IN(7) == 0u && IN_B(17) == 1u &&
              IN_B(20) == 3u && IN_B(23) == 3u && IN_B(22) == 2u && IN_B(28) == 4u && IN_B(26) == HR_NO_SOUND &&
              IN_B(45) == HR_NO_SOUND,
          "an HR-16:B's: 07 a snare (the HR-16's a kick), the sidestick a snare, its mid and rock hats the MID HAT's, "
          "its ride; its aggressive tom TOM 4's (none), the glass break none: %u %u", IN_B(7), IN_B(23));
    {
        hr_pad_t x = hr_std[0];
#define OUT(snd, d) out_as(snd, d)
        CHECK(OUT(0, 0) == 0u && OUT(1, 1) == 11u && OUT(2, 2) == 23u && OUT(3, 4) == 26u && OUT(3, 3) == 25u,
              "ours on their drums: as an HR-16 sets them up (the SNARE's 11; half open on the MID HAT: %u)",
              OUT(3, 3));
        CHECK(OUT(4, 12) == 28u && OUT(4, 13) == 30u && OUT(4, 0) == 28u,
              "the cymbal a ride on the RIDE, a crash on the CRASH, elsewhere the first drum with it: %u %u %u",
              OUT(4, 12), OUT(4, 13), OUT(4, 0));
        CHECK(OUT(1, 5) == 11u && OUT(0, 8) == 0u, "another drum's sound: that drum's (%u %u)", OUT(1, 5), OUT(0, 8));
        CHECK(OUT(SLOT(3), 8) == 20u && OUT(SLOT(5), 8) == 20u && OUT(HR_NO_SOUND, 14) == 31u,
              "a slot, none: the drum's own (%u %u %u)", OUT(SLOT(3), 8), OUT(SLOT(5), 8), OUT(HR_NO_SOUND, 14));
        x.sound = 0, x.hr16 = 43;
        CHECK(hd_sound_out(&x, 0) == 42u, "a sound as it came (a cowbell on the KICK): as it came");
        sq.hr16[0xFE] = 0x27, sq.hr16[0xFF] = 0xB5;   /* an HR-16's header: its drums' sounds, most of its patterns' */
        sq.hr16_voice[0] = 7, sq.hr16_voice[1] = 42;
        CHECK(OUT(0, 0) == 7u && OUT(1, 1) == 11u, "after an HR-16's dump: the KICK its 07, the SNARE (a cowbell its "
              "most: not a snare) 11: %u %u", OUT(0, 0), OUT(1, 1));
        sq.hr16[0xE7] = 'B';      /* an HR-16:B's */
        sq.hr16_voice[1] = 0;
        CHECK(hd_is_b(sq.hr16) && OUT(1, 1) == 8u && OUT(2, 2) == 19u && OUT(0, 0) == 0u,
              "after an HR-16:B's: its own (the SNARE 08, CLS HAT 19), its 07 a snare not the KICK's: %u %u %u",
              OUT(1, 1), OUT(2, 2), OUT(0, 0));
#undef OUT
    }
    setup();
    CHECK(hd_tune_in(16) == 0 && hd_tune_in(0) == -144 && hd_tune_in(31) == 135 && hd_tune_in(17) == 9 &&
              hd_tune_in(0xE0u | 16u) == 0,
          "the pitch's steps as sixteenths, 9 a step: %d %d %d", hd_tune_in(0), hd_tune_in(31), hd_tune_in(17));
    CHECK(hd_tune_out(0) == 16u && hd_tune_out(4) == 16u && hd_tune_out(5) == 17u && hd_tune_out(16) == 18u &&
              hd_tune_out(-5) == 15u && hd_tune_out(-24 * 16) == 0u && hd_tune_out(24 * 16) == 31u,
          "sixteenths as steps, the nearest: %u %u %u %u", hd_tune_out(5), hd_tune_out(16), hd_tune_out(-5),
          hd_tune_out(24 * 16));
    for (s = 0; s < 32; s++)
        bad += hd_tune_out(hd_tune_in((uint32_t)s)) != (uint32_t)s;
    CHECK(!bad, "the 32 steps there and back: %d wrong", bad);
    {
        hr_pad_t x;
        int ok = 1;
        hd_tune_split(9, &x);     /* a step up: +1 -7 */
        ok &= x.tune == 1 && x.fine == -7;
        hd_tune_split(-9, &x);
        ok &= x.tune == -1 && x.fine == 7;
        hd_tune_split(135, &x);   /* +15 steps: +8 +7 */
        ok &= x.tune == 8 && x.fine == 7;
        hd_tune_split(-144, &x);  /* -16: -9 exactly */
        ok &= x.tune == -9 && x.fine == 0;
        for (s = -144; s <= 135; s += 9) {
            hd_tune_split(s, &x);
            ok &= x.tune * 16 + x.fine == s && x.fine >= -8 && x.fine <= 8;
        }
        CHECK(ok, "a step as TUNE, the nearest semitone, and FINE, the sixteenths from it");
    }
}

static void hit(uint32_t p, uint32_t tick, uint32_t pad, uint32_t vel) { hr_add(p, tick, pad, vel, scratch); }

static void test_export(void)
{
    int32_t n;
    uint32_t end;
    const uint8_t ev0[9] = {0x80, 0x70, 0x32, 0xB0, 0x01, 0xE6, 0x6D, 0xAA, 0xFF};   /* (a wait first: none) */
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
              img[0xCE] == 42u && img[0xD0] == 46u && img[0xD8] == 51u && img[0xD9] == 49u && img[0xE6] == 1u &&
              img[0xCB] == 34u,
          "a header made up: the manual's two bytes, channel %u, the notes %u %u %u, the click's %u", img[0xC8],
          img[0xCC], img[0xCE], img[0xD9], img[0xCB]);
    CHECK(img[0x2D8] == 120u && img[0x2D5] == 2u && img[0x2D9] == 24u && img[0x2DA] == 4u,
          "tempo %u, swing %u, quantize %u clocks (%u)", img[0x2D8], img[0x2D5], img[0x2D9], img[0x2DA]);
    CHECK(img[0x200] == 0x86u && img[0x201] == 0x00u && img[0x202] == 0 && img[0x203] == 0 && img[0x20A] == 0x86u &&
              img[0x20B] == 0x3Du,
          "the pointers: pattern 00 at 8600H, 05 after it at %02X%02X", img[0x20A], img[0x20B]);
    CHECK(img[0x400] == 0x3Du && img[0x401] == 0 && img[0x402] == 0x03u && img[0x403] == 0,
          "pattern 00: %u bytes, its 2 beats + 1 in BCD %02X", img[0x400], img[0x402]);
    CHECK(img[0x404] == 0u && img[0x405] == 85u && img[0x406] == (3u << 5 | 16u) && img[0x407] == 11u &&
              img[0x408] == 70u && img[0x409] == 18u,
          "its kick: sound %u level %u pan/pitch %02X; its snare: %u %u %02X", img[0x404], img[0x405], img[0x406],
          img[0x407], img[0x408], img[0x409]);
    CHECK(img[0x404 + 3 * 12] == 28u && img[0x404 + 3 * 13] == 30u && img[0x404 + 3 * 8] == 20u,
          "the ride a ride, the crash a crash, TOM 1 a tom: %u %u %u", img[0x404 + 36], img[0x404 + 39],
          img[0x404 + 24]);
    CHECK(!memcmp(img + 0x434, ev0, 9), "its events: %02X %02X %02X %02X %02X %02X %02X %02X %02X", img[0x434],
          img[0x435], img[0x436], img[0x437], img[0x438], img[0x439], img[0x43A], img[0x43B], img[0x43C]);
    CHECK(img[0x43D] == 0x38u && img[0x43F] == 0x04u && !memcmp(img + 0x43D + 0x34, ev5, 4),
          "pattern 05: %u bytes, 3 beats waited: %02X %02X %02X", img[0x43D], img[0x471], img[0x472], img[0x473]);
    end = 0x43D + 0x38;
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
              r.swing == 2,
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
    CHECK(pads[1].fine == 2 && pads[5].fine == 3 && pads[0].fine == 0 && sq_len(SQ_PSND(0)) == SQ_SOUNDS_HR16 &&
              pads[1].hr16 == 12u && pads[5].hr16 == 29u && pads[8].hr16 == 21u,
          "their tunings as the HR-16's steps: the snare's +1 as 2 steps (+1 +2/16), the ride's -3 as -5 (-3 +3/16): "
          "FINE %d %d; each drum's sound as it came kept (the snare's 11, the ride's 28, TOM 1's 20: %u %u %u)",
          pads[1].fine, pads[5].fine, pads[1].hr16 - 1u, pads[5].hr16 - 1u, pads[8].hr16 - 1u);
    CHECK(img[0xDD] == 0u && r.prog == 0, "program change off: in its header %u, taken %d", img[0xDD], r.prog);
    {   /* on: in the HR-16's header kept too, and taken from it */
        hd_settings_t on = SET;
        hd_report_t r2;
        on.prog = 1;
        n2 = hd_export(img2, HD_MAX, &on);
        CHECK(n2 == n && img2[0xDD] == 1u && hd_import(img2, (uint32_t)n2, scratch, &r2) == 0 && r2.prog == 1,
              "program change on: in its header %u, taken %d", img2[0xDD], r2.prog);
    }
    n2 = hd_export(img2, HD_MAX, &SET);
    CHECK(n2 == n && !memcmp(img, img2, (size_t)n), "sent again: the same, %d bytes (%d)", n2, n);
    {   /* cut short inside pattern 42 (the last): the two before taken, it told as lost on the way */
        uint32_t at42 = (uint32_t)(img[0x200 + 84] << 8 | img[0x201 + 84]) - HD_BASE;
        CHECK(hd_want(img, 0x2CE) == 0u && hd_want(img, (uint32_t)n) == (uint32_t)n,
              "its length from its header: %u (%d)", hd_want(img, (uint32_t)n), n);
        CHECK(hd_import(img, at42 + 10u, scratch, &r) == 0 && r.cut && !r.bad && !r.full && r.patterns == 2u &&
                  r.lost == 1u && r.first_lost == 42 && r.want == (uint32_t)n && r.got == at42 + 10u,
              "cut short: cut %u (bad %u full %u), %u patterns, first lost %d", r.cut, r.bad, r.full, r.patterns,
              r.first_lost);
        for (n2 = (int32_t)at42 + (int32_t)HD_PAT_HDR; n2 < n && img2[n2] != 0xFFu; n2++)
            ;
        img2[n2] = 0x00u;         /* whole, but pattern 42's end gone (a hit where its FFH was) */
        CHECK(hd_import(img2, (uint32_t)n, scratch, &r) == 0 && r.bad && !r.cut && !r.full && r.lost == 1u &&
                  r.first_lost == 42 && r.patterns == 2u,
              "damaged: bad %u (cut %u full %u), %u lost, first %d", r.bad, r.cut, r.full, r.lost, r.first_lost);
    }
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
    /* the HR-16's own notes: the click's, then its drums' (KICK 35 .. MID HAT 44 .. PERC 2 62) */
    static const uint8_t NOTES[17] = {34, 35, 38, 42, 44, 46, 39, 67, 68, 48, 45, 41, 63, 51, 49, 65, 62};
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
    memcpy(m + 0xCB, NOTES, sizeof NOTES);
    m[0x0A] = 0x5A;               /* (a byte not to alter) */
    m[0x2D5] = 7;
    m[0x2DA] = 1;                 /* 1/6 */
    at = pat_at(m, at, 2, 0x0009, empty8, 8);   /* (the beats + 1, BCD, as HR-16 dumps have them) */
    at = pat_at(m, at, 3, 0x0000, zero, 1);   /* 0 beats: empty */
    at = pat_at(m, at, 4, 0x0003, ev, 7);
    at = pat_at(m, at, 5, 0x0017, zero, 1);   /* no waits: 16 beats as said, 0017H */
    m[0x200 + 2 * 50] = 0x12;     /* a pointer before the patterns: none */
    m[0x302 + 2 * 3] = (uint8_t)((at + HD_BASE) >> 8);   /* song 03: pattern 04 twice */
    m[0x303 + 2 * 3] = (uint8_t)(at + HD_BASE);
    m[at] = 5, m[at + 2] = 4, m[at + 3] = 4, m[at + 4] = 0xFF;
    at += 5;
    m[0x302 + 2 * 99] = (uint8_t)((at + HD_BASE) >> 8);   /* song 99 empty */
    m[0x303 + 2 * 99] = (uint8_t)(at + HD_BASE);
    m[at] = 3, m[at + 2] = 0xFF;
    at += 3;
    CHECK(hd_import(m, at, scratch, &r) == 0 && r.patterns == 3u && !r.lost && r.songs == 1u && r.quant == 1 &&
              r.swing == 7,
          "taken in: %u patterns, %u songs not, quantize %d (1/6: 1/8), swing %d", r.patterns, r.songs, r.quant,
          r.swing);
    CHECK(hr_beats(2) == 8u && !hr_hits(2) && hr_sounds_own(2) && !sq.pattern[3].beats && !hr_sounds_own(3) &&
              !hr_hits(50),
          "the manual's empty pattern: %u beats; a pattern of 0 beats empty", hr_beats(2));
    CHECK(sq.pattern[5].beats == 16u && !hr_hits(5), "no waits: the length said, 0017H: %u beats", sq.pattern[5].beats);
    CHECK(HD_PAD[3] == 6u && hr_std[6].group && hr_std[6].group == hr_std[HD_PAD[2]].group &&
              hr_std[6].group == hr_std[HD_PAD[4]].group,
          "its MID HAT on pad 7, a hat with the CLS and OPN HAT (the HR-16's one voice): group %u", hr_std[6].group);
    CHECK(hr_beats(4) == 2u && hr_hits(4) == 4u && hr_hit_at(4, 0)[2] == 0u && hr_hit_at(4, 0)[3] == 127u &&
              hr_hit_tick(hr_hit_at(4, 1)) == 0u && hr_hit_at(4, 1)[2] == 1u && hr_hit_at(4, 1)[3] == 15u &&
              hr_hit_at(4, 2)[2] == 10u && hr_hit_at(4, 2)[3] == 95u && hr_hit_tick(hr_hit_at(4, 3)) == 95u &&
              hr_hit_at(4, 3)[2] == 0u && hr_hit_at(4, 3)[3] == 15u,
          "pattern 04: %u beats, %u hits: the kick, the snare round from the end, TOM 3 (pad %u, %u), the kick",
          hr_beats(4), hr_hits(4), hr_hit_at(4, 2)[2] + 1u, hr_hit_at(4, 2)[3]);
    hr_sounds_get(4, pads);
    CHECK(pads[0].sound == 0u && pads[0].level == 77u && pads[0].pan == 3 && pads[0].tune == -9 &&
              pads[0].hr16 == 3u && pads[12].sound == HR_NO_SOUND && pads[12].hr16 == 43u &&
              pads[4].sound == HR_NO_SOUND,
          "its sounds: a kick the KICK, level %u pan %d tune %d; a cowbell none (on the CRASH too)", pads[0].level,
          pads[0].pan, pads[0].tune);
    CHECK(hr_std[0].note == 35u && hr_std[1].note == 38u && hr_std[6].note == 44u && hr_std[3].note == 46u &&
              hr_std[15].note == 68u && hr_std[13].note == 62u && hr_pad[0].note == 35u && hr_pad[6].note == 44u,
          "its notes the pads': the KICK %u, the MID HAT (pad 7) %u, PERC 4 (pad 16) %u", hr_std[0].note,
          hr_std[6].note, hr_std[15].note);
    for (i = 0; i < HD_HDR && sq.hr16[i] == m[i]; i++)
        ;
    CHECK(i == HD_HDR && sq.hr16_voice[0] == 2u && sq.hr16_voice[13] == 42u, "its header kept, its sounds: %u %u",
          sq.hr16_voice[0], sq.hr16_voice[13]);
    CHECK(hd_export(img, HD_MAX, &SET) > 0 && img[0x0A] == 0x5Au && img[0xC8] == 3u && img[0x404] == 2u &&
              img[0x404 + 3 * 13] == 42u && img[0x404 + 3 * 8] == 42u && img[0xE3] == 0u,
          "ours: its header's bytes, its sounds as they came: the kick's %u, the crash's (a cowbell) %u, TOM 1's %u",
          img[0x404], img[0x404 + 39], img[0x404 + 24]);
    hr_std[0].note = 36;          /* a pad's note changed: ours has it */
    CHECK(hd_export(img, HD_MAX, &SET) > 0 && img[0xCC] == 36u && img[0xCD] == 38u && img[0xCF] == 44u,
          "ours: the pads' notes, the KICK's %u", img[0xCC]);
    memset(m + 0xCC, 0, 16);      /* a dump without notes (all 16 on 0): the pads keep theirs */
    CHECK(hd_import(m, at, scratch, &r) == 0 && hr_std[0].note == 36u && hr_std[6].note == 44u,
          "all on one note: not taken (%u %u)", hr_std[0].note, hr_std[6].note);
}

/* MANUAL VOICE/TUNE/MIX on in a dump: the kit it has now (103H-132H) the global set-up, every pattern playing it, its
 * own kept; sent back with ours */
static void test_manual(void)
{
    static uint8_t m[HD_MAX];
    const uint8_t ev[2] = {0x80 | 96, 0xFF};   /* a beat */
    uint32_t at = HD_HDR, d;
    hd_report_t r;
    hd_settings_t on = SET;
    setup();
    memset(m, 0, sizeof m);
    m[0xFE] = 0x27;
    m[0xFF] = 0xB5;
    m[0xE3] = 1;
    for (d = 0; d < HD_DRUMS; d++) {   /* its kit now: cowbells, right, as they are */
        m[0x103 + 3 * d] = 42;
        m[0x104 + 3 * d] = 50;
        m[0x105 + 3 * d] = 6u << 5 | 16u;
    }
    m[0x103] = 5;                 /* the KICK a kick; the SNARE the gated snare, a step up, left 2, level 60 */
    m[0x106] = 13, m[0x107] = 60, m[0x108] = 1u << 5 | 17u;
    at = pat_at(m, at, 0, 0x0002, ev, 2);   /* (its own: cowbells, the kick 2, level 77) */
    CHECK(hd_import(m, at, scratch, &r) == 0 && r.manual == 1 && r.patterns == 1u, "manual on: %d, %u patterns",
          r.manual, r.patterns);
    CHECK(hr_std[0].sound == 0u && hr_std[0].hr16 == 6u && hr_std[0].level == 50u && hr_std[0].pan == 3 &&
              hr_std[1].sound == 1u && hr_std[1].hr16 == 14u && hr_std[1].level == 60u && hr_std[1].pan == -2 &&
              hr_std[1].tune == 1 && hr_std[1].fine == -7 && hr_std[12].sound == HR_NO_SOUND &&
              hr_std[12].hr16 == 43u && hr_pad[1].fine == -7,
          "its kit now the global set-up: the KICK's %u (as it came %u), the SNARE's %u level %u pan %d tune %d %+d/16",
          hr_std[0].sound, hr_std[0].hr16 - 1u, hr_std[1].sound, hr_std[1].level, hr_std[1].pan, hr_std[1].tune,
          hr_std[1].fine);
    hr_manual = 1;
    hr_sounds_apply(0);
    CHECK(hr_sounds_own(0) && !hr_snd.own && hr_pad[0].level == 50u, "manual: pattern 00's own kept, not played (%u)",
          hr_pad[0].level);
    hr_manual = 0;
    hr_sounds_apply(0);
    CHECK(hr_snd.own && hr_pad[0].level == 77u, "manual off: its own played (%u)", hr_pad[0].level);
    on.manual = 1;
    hr_std[0].level = 40;         /* (the set-up changed here) */
    CHECK(hd_export(img, HD_MAX, &on) > 0 && img[0xE3] == 1u && img[0x103] == 5u && img[0x104] == 40u &&
              img[0x105] == (6u << 5 | 16u) && img[0x106] == 13u && img[0x107] == 60u &&
              img[0x108] == (1u << 5 | 17u) &&
              img[0x103 + 3 * 14] == 42u,
          "ours, manual on: the global set-up its kit (the KICK's %u level %u, the SNARE's %u %02X)", img[0x103],
          img[0x104], img[0x106], img[0x108]);
    CHECK(hd_export(img, HD_MAX, &SET) > 0 && img[0xE3] == 0u && img[0x104] == 50u,
          "ours, manual off: its kit as it came (level %u)", img[0x104]);
    m[0xE3] = 7;
    CHECK(hd_import(m, at, scratch, &r) == 0 && r.manual == -1, "not 0 or 1: not one (%d)", r.manual);
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
    test_manual();
    test_limits();
    printf("hrdump: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
