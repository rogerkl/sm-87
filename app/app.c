/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: an MMT-8 style sequencer, an Akai S700 / X7000 style sampler and an HR-16 style drum machine on the M-VAVE
 * FM-1. The app's glue: the sound (app_render), MIDI in, the panel and the screen. Part of fm1-base's unity build
 * (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. The sampler (s700/), the
 * sequencer (mmt8/) and the drums (hr16/) are compiled apart, at -O2. */
#include <stddef.h>
#include "fastmath.h"
#include "s700/s7.h"
#include "s700/s7mem.h"
#include "s700/sds.h"
#include "mmt8/seq.h"
#include "mmt8/player.h"
#include "mmt8/mmtdump.h"
#include "hr16/hr.h"
#include "hr16/hrdump.h"
#include "ui/lcd.c"
#include "ui/pages_s700.c"

#ifdef PLAT_HOST
#define SM_FS 44100.0f            /* the simulator's rate */
#else
#define SM_FS 44117.647f          /* the FM-1's: 24 MHz / 544 (felucca-bsp fm1_audio.h) */
#endif

/* ------------------------------------------------------------------------------------------------ settings --- */
#define SET_MAGIC 0x37384D53u     /* "SM87" */
typedef struct {
    uint32_t magic;
    uint8_t s7_channel;           /* the sampler's MIDI channel, 0..15 (the X7000 received on 1) */
    uint8_t bend_range;           /* semitones (PWRNGE) */
    int8_t octave;                /* the keys: -2 .. +2 */
    uint8_t clock_in;             /* the sequencer's clock: 0 its own, 1 MIDI's */
    uint8_t clock_out_off;        /* MIDI clock out off (on unless) */
    uint8_t s7_mode;              /* the sampler's MIDI mode: S7_OMNI_OFF, S7_OMNI_ON, S7_MULTI (s700/s7.h) */
    uint16_t dr_kit_tag;          /* the kit the pads were saved with (hr_kit_tag; 0: before it was kept) */
    /* the drums' (a settings object saved before them is shorter: these take their defaults) */
    uint8_t dr_channel;           /* their MIDI channel, 0..15 (10: 9) */
    uint8_t dr_quant, dr_swing;   /* recording's quantize (hr16/hr.h HR_GRID's index) and swing, clocks (the HR-16's;
                                   * before, a percentage, 50..75: taken as clocks as it loads) */
    uint8_t dr_kit_n;             /* the kit's sounds when the pads were saved (hr_pads_rekit) */
    hr_pad_t pads[HR_PADS];       /* their global set-up (hr16/hr.h hr_std) */
    /* the mix (the MIX page; an object saved before it: each slot at full, in the middle, as the S700 is) */
    uint8_t mix_level[S7_SLOTS];  /* each slot's, 0..99 (99: as it is) */
    int8_t mix_pan[S7_SLOTS];     /* -50 (left) .. 50 */
    uint8_t drum_level;           /* the drums' bus, 0..99 */
    uint8_t s7_poly;              /* the sampler's voices, S7_POLY_STD (6, the S700's) .. S7_VOICES (0, before it was
                                   * kept: 6) */
    uint8_t dr_prog;              /* a program change on the drums' channel chooses their pattern (the HR-16's MIDI
                                   * PROGRAM CHANGE; off, as its default) */
    uint8_t rsv2[1];
    /* the MMT-8's LOOP and CLICK (the SEQ page's LOOP/CLICK; an object saved before them: their defaults) */
    uint8_t loop;                 /* LOOP_OFF, LOOP_PARTS (parts go round, songs stop at their end), LOOP_ALL */
    uint8_t countin;              /* the count-in before a take, beats: 0 (none) .. 99 */
    uint8_t click_q;              /* the click's value: 1/2 .. 1/64 (seq_edit.c GRID's index; 1: 1/4) */
    uint8_t click_when;           /* the click: bit 0 while recording, bit 1 while playing (1: recording only) */
    /* the MMT-8's MIDI FILTER (an object saved before it: its defaults) */
    uint8_t rec_filter;           /* what a take records besides notes: SQP_REC_* (all but aftertouch) */
    uint8_t rec_ch;               /* from which channel: 0 all, 1..16 */
    uint8_t rsv3[2];
} settings_t;
#define SET_V1_LEN 12u            /* (to the drums') */
#define SET_V2_LEN offsetof(settings_t, mix_level)   /* (to the mix) */
#define SET_V3_LEN offsetof(settings_t, loop)        /* (to the loop and click) */
#define SET_V4_LEN offsetof(settings_t, rec_filter)  /* (to the MIDI filter) */
enum { LOOP_OFF, LOOP_PARTS, LOOP_ALL, LOOP_N };
#define CLICK_QS 10u
static void seq_settings_apply(void);   /* LOOP, CLICK and MIDI FILTER into the sequencer (ui/seq_edit.c) */
#define MIX_PAN_MAX 50
static settings_t set;
static uint8_t set_dirty;

static void drums_defaults(void)
{
    uint32_t i;
    set.dr_channel = 9;
    set.dr_quant = 3;             /* 1/16 */
    set.dr_swing = 0;
    for (i = 0; i < HR_PADS; i++) {
        hr_pad_default(i);
        hr_std[i] = hr_pad[i];
    }
}

static void mix_defaults(void)
{
    uint32_t i;
    for (i = 0; i < S7_SLOTS; i++) {
        set.mix_level[i] = 99;
        set.mix_pan[i] = 0;
    }
    set.drum_level = 99;
}

/* LOOP PARTS (parts go round, as they always did; songs end), the MMT-8's click: 1/4, while recording, a count-in
 * of 4 */
static void loop_click_defaults(void)
{
    set.loop = LOOP_PARTS;
    set.countin = 4;
    set.click_q = 1;
    set.click_when = 1;
}

/* the MMT-8's MIDI FILTER: all but aftertouch, every channel */
static void filter_defaults(void)
{
    set.rec_filter = SQP_REC_BEND | SQP_REC_CTRL | SQP_REC_PROG;
    set.rec_ch = 0;
}

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.dr_kit_tag = (uint16_t)hr_kit_tag();
    set.s7_channel = 0;
    set.bend_range = 2;
    set.octave = 0;
    set.clock_in = 0;
    set.clock_out_off = 0;
    set.s7_mode = S7_OMNI_OFF;
    set.s7_poly = S7_POLY_STD;
    set.dr_prog = 0;
    loop_click_defaults();
    filter_defaults();
    drums_defaults();
    mix_defaults();
}

/* the settings as they load: what an older (shorter) object lacks, the defaults (the drums', the mix); the pads as
 * the kit is now */
static void settings_load(void)
{
    int n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    uint32_t i, bad = 0;
    if (n < (int)SET_V1_LEN || set.magic != SET_MAGIC || set.s7_channel > 15u || set.bend_range > 12u ||
        set.octave < -2 || set.octave > 2 || set.s7_mode > S7_MULTI) {
        settings_defaults();
        return;
    }
    if (set.s7_poly < S7_POLY_STD || set.s7_poly > S7_VOICES)
        set.s7_poly = S7_POLY_STD;
    if (set.dr_prog > 1u)
        set.dr_prog = 0;
    if (n < (int)SET_V4_LEN || set.loop >= LOOP_N || set.countin > 99u || set.click_q >= CLICK_QS ||
        set.click_when > 3u)
        loop_click_defaults();
    if (n < (int)sizeof set || set.rec_filter > 15u || set.rec_ch > 16u)
        filter_defaults();
    if (n < (int)SET_V2_LEN || set.dr_channel > 15u || set.dr_quant >= HR_GRIDS) {
        drums_defaults();
        mix_defaults();
        return;
    }
    {   /* the swing in clocks (one saved as a percentage, 50..75: those clocks at its quantize), at most its grid's */
        uint32_t g = HR_GRID[set.dr_quant];
        if (set.dr_swing >= 50u)
            set.dr_swing = (uint8_t)((((uint32_t)set.dr_swing - 50u) * 2u * g + 50u) / 100u);
        if (set.dr_swing > hr_swing_max(g))
            set.dr_swing = (uint8_t)hr_swing_max(g);
    }
    if (hr_kit_same(set.dr_kit_tag)) {
        for (i = 0; i < HR_PADS; i++)
            hr_std[i] = set.pads[i];
        hr_pads_rekit(set.dr_kit_n);
        for (i = 0; i < HR_PADS; i++)
            hr_pad[i] = hr_std[i];
    } else {
        for (i = 0; i < HR_PADS; i++) {   /* another kit: its pads' set-up */
            hr_pad_default(i);
            hr_std[i] = hr_pad[i];
        }
    }
    for (i = 0; i < S7_SLOTS; i++)
        bad |= set.mix_level[i] > 99u || set.mix_pan[i] > MIX_PAN_MAX || set.mix_pan[i] < -MIX_PAN_MAX;
    if (n < (int)SET_V3_LEN || bad || set.drum_level > 99u)
        mix_defaults();
}

/* the mix into the sound: a slot's level (squared: a fader's), its pan (a balance: in the middle both at full) */
static float dr_gain = 1.0f;      /* the drums' bus */
static float mix_gain(uint32_t level)
{
    float g = (float)level / 99.0f;
    return g * g;
}
static void mix_apply(uint32_t slot)
{
    float g = mix_gain(set.mix_level[slot]), p = (float)set.mix_pan[slot] / (float)MIX_PAN_MAX;
    uint32_t f = plat_lock();
    s7_mix(slot, p > 0.0f ? g * (1.0f - p) : g, p < 0.0f ? g * (1.0f + p) : g);
    dr_gain = mix_gain(set.drum_level);
    plat_unlock(f);
}

static int settings_save(void)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++)
        set.pads[i] = hr_std[i];
    set.dr_kit_n = (uint8_t)hr_kit_sounds();
    set.dr_kit_tag = (uint16_t)hr_kit_tag();
    return plat_store_save(OBJ_SETTINGS, &set, sizeof set);
}

/* the sample dumps' staging: one taken while the last is written to the flash (s700/sds.h) */
static uint8_t stage[SDS_BUFS][S7_PACKED(S7_MAXWORDS)] __attribute__((section(".pool")));

/* -------------------------------------------------------------------------- events: main loop -> audio --- */
/* the panel plays the sampler from the main loop; the voices live in the audio interrupt. A ring of MIDI-like
 * messages (status | d1 << 8 | d2 << 16) between them, one writer and one reader */
#define EVQ 64u
#define EV_INTERNAL (1u << 24)    /* heard here only: not recorded, not out over MIDI (STEP EDIT's hit shown) */
static volatile uint32_t evq[EVQ];
static volatile uint32_t evq_w, evq_r;

static void ev_push(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t w = evq_w;
    if (w - evq_r >= EVQ)
        return;                   /* full: dropped (64 in under 3 ms: not from hands) */
    evq[w % EVQ] = st | d1 << 8 | d2 << 16;
    evq_w = w + 1u;
}

/* one message to the sampler, on a channel its MIDI mode hears (audio interrupt). A program change chooses the
 * program (in MULTI on the basic channel only: the others' programs are theirs, by channel) */
static void s7_message(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t ch = st & 0x0Fu;
    switch (st & 0xF0u) {
    case 0x90u:
        s7_note_on(ch, d1, d2);   /* velocity 0: a note off at 64 */
        break;
    case 0x80u:
        s7_note_off(ch, d1, d2);
        break;
    case 0xB0u:
        if (d1 == 1u)
            s7_modwheel(ch, d2);
        else if (d1 == 120u)
            s7_kill();
        else if (d1 == 123u)
            s7_channel_off(ch);
        break;
    case 0xC0u:
        if (set.s7_mode != S7_MULTI || ch == set.s7_channel)
            s7_program(d1);
        break;
    case 0xD0u:
        s7_pressure(ch, d1);
        break;
    case 0xE0u:
        s7_bend(ch, (int32_t)(d1 | d2 << 7) - 8192);
        break;
    default:
        break;
    }
}

/* what the sampler hears, for the strips: "CH 1", "OMNI", "MULTI" */
static void s7_chan_text(char *t)
{
    t[0] = 0;
    if (set.s7_mode == S7_MULTI) {
        str_cat(t, "MULTI");
    } else if (set.s7_mode == S7_OMNI_ON) {
        str_cat(t, "OMNI");
    } else {
        str_cat(t, "CH ");
        fmt_uint(t + 3, set.s7_channel + 1u, 0, ' ');
    }
}

/* the channel the keys play the sampler on: its basic channel; in MULTI channel 1, the program shown's */
static uint32_t s7_keys_channel(void)
{
    return set.s7_mode == S7_MULTI ? 0u : set.s7_channel;
}

static volatile uint32_t midi_in_count, midi_in_last;
/* the channel lights over the tracks (ch_lights): a bit a channel, set as a message on it comes in or is played (in
 * the audio interrupt only; the main loop takes them under the lock) */
static volatile uint16_t ch_seen;
#define CH_SEEN(st) (ch_seen = (uint16_t)(ch_seen | 1u << ((st) & 0x0Fu)))

/* one message to the drums (audio interrupt): a note plays the pads on it (its off: nothing, they play out) */
static void hr_message(uint32_t st, uint32_t d1, uint32_t d2)
{
    if ((st & 0xF0u) == 0x90u && d2)
        hr_note(d1, d2);
    else if ((st & 0xF0u) == 0xB0u && d1 == 120u)
        hr_kill();
    else if ((st & 0xF0u) == 0xC0u && set.dr_prog)
        hrp_program(d1);          /* (the sequencer's, MIDI in's: a part's track can choose the drums) */
}

/* ------------------------------------------------------------------------------------------ the routing --- */
/* everything played, by the sequencer, the keys or MIDI in, goes here: the sampler on its channel (OMNI ON, MULTI:
 * every channel), the drums on theirs (in MULTI channel 10 is both), and out over USB (to (TO_USB): where it may go;
 * MIDI in does not go back where it came from) */
static void play_out(uint32_t st, uint32_t d1, uint32_t d2, uint32_t to)
{
    if (st < 0xF0u) {
        CH_SEEN(st);
        if (set.s7_mode != S7_OMNI_OFF || (st & 0x0Fu) == set.s7_channel)
            s7_message(st, d1, d2);
        if ((st & 0x0Fu) == set.dr_channel)
            hr_message(st, d1, d2);
        if (to) {
            uint32_t len = (st & 0xE0u) == 0xC0u ? 2u : 3u;
            plat_midi_send((st >> 4) | st << 8 | d1 << 16 | (len == 3u ? d2 << 24 : 0u), to);
        }
    } else if (st == 0xF2u && to) {          /* song position: system common, 3 bytes */
        plat_midi_send(0x03u | st << 8 | d1 << 16 | d2 << 24, to);
    }
}

/* the sequencer's (mmt8/player.h) */
void sqp_emit(uint32_t st, uint32_t d1, uint32_t d2) { play_out(st, d1, d2, TO_USB); }
void sqp_midi_rt(uint32_t b) { plat_midi_send(0x0Fu | b << 8, TO_USB); }

/* the drums' (hr16/hr.h): a pattern's hit sounds its pad, and goes out on its note (off a 32nd later) */
#define HR_OFF_TICKS 12u
void hr_emit(uint32_t pad, uint32_t vel)
{
    uint32_t ch = set.dr_channel, note = hr_pad[pad].note;
    hr_hit(pad, vel);
    CH_SEEN(ch);
    plat_midi_send(0x09u | (0x90u | ch) << 8 | note << 16 | vel << 24, TO_USB);
    sqp_off_after(HR_OFF_TICKS, 0x80u | ch, note);
}

/* each tick of the clock: the drums; recording, the click (LOOP/CLICK's record click, at its value) on their own
 * clocks, unless the sequencer clicks now (its record click in a take, else its play click) */
void sqp_on_tick(void)
{
    uint32_t seq_clicks = sqp.state == SQP_PLAYING &&
                          (sqp.take_state == SQP_TAKE_RUNNING ? sqp.click_rec : sqp.click_play);
    hrp_tick();
    if (hrp_recording() && sqp.click_rec && sqp.click_grid && hrp.at % sqp.click_grid == 0u && !seq_clicks)
        sqp_click(hrp.at % (4u * SQ_PPQN) == 0u);
}

/* the click: a short blip, higher on the bar's first beat */
static float click_env, click_ph, click_inc;
static uint32_t click_count;     /* clicks so far (the host's checks) */
void sqp_click(int accent)
{
    click_count++;
    click_env = 1.0f;
    click_ph = 0.0f;
    click_inc = (accent ? 2000.0f : 1400.0f) / SM_FS;
}

static void click_render(float *l, float *r, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && click_env > 1e-3f; i++) {
        float v = 900.0f * click_env * fm_sin_turns(click_ph);
        l[i] += v;
        r[i] += v;
        click_ph += click_inc;
        if (click_ph >= 1.0f)
            click_ph -= 1.0f;
        click_env *= 0.9985f;               /* ~15 ms */
    }
}

/* something played in (MIDI in, or the keys on the sampler's channel): into a take that runs, and through (the
 * MMT-8's MIDI ECHO: to the record track's channel while one is ready, unless it is UNCHANGED) */
static void played_in(uint32_t st, uint32_t d1, uint32_t d2, uint32_t to)
{
    if (st >= 0xF0u)
        return;
    sqp_input(st, d1, d2);
    if ((st & 0xF0u) == 0x90u && d2 && (st & 0x0Fu) == set.dr_channel && (hrp.step || hrp_recording())) {
        int32_t pad = hr_pad_of(d1);   /* the drums recording, or STEP EDIT: into the pattern */
        if (pad >= 0 && hrp.step)
            hrp_step_in((uint32_t)pad, d2);
        else if (pad >= 0)
            hrp_record((uint32_t)pad, d2);
    }
    if ((st & 0xE0u) == 0x80u && (st & 0x0Fu) == set.dr_channel && hr_pad_of(d1) >= 0)   /* FILL: held, let go */
        hrp_fill_pad((uint32_t)hr_pad_of(d1), d2, (st & 0xF0u) == 0x90u && d2);
    if (sqp.rec_armed || sqp.take_state == SQP_TAKE_RUNNING) {
        uint32_t ch = sq.part[sqp.part].chan[sqp.rec_track];
        if (ch)
            st = (st & 0xF0u) | (ch - 1u);
    }
    play_out(st, d1, d2, to);
}

static uint32_t frames_now;       /* frames rendered: MIDI clock in's time */

static void midi_in(void)         /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24 & 0x7Fu;
        if (st < 0xF0u) {
            CH_SEEN(st);          /* (the channel it came on; MIDI ECHO's, if another, as it is played) */
            played_in(st, d1, d2, TO_USB & ~plat_midi_in_from());
            midi_in_count++;
            midi_in_last = st;
        } else if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu || st == 0xF2u) {
            sqp_rt_in(st, d1 | d2 << 7, frames_now);   /* MIDI clock in (when the sequencer follows it) */
        }
    }
}

/* ------------------------------------------------------------------------------------------------- sound --- */
static float mixl[APP_BLOCK], mixr[APP_BLOCK], drl[APP_BLOCK], drr[APP_BLOCK];
static float gain_now;

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, done = 0;
    /* a voice at its loudest (LEVEL 99, velocity 127) peaks at -6 dBFS: room for a few at once */
    float gain = (float)master_q12 / 4096.0f * (8388607.0f * 0.5f / 2048.0f);
    while (evq_r != evq_w) {                 /* the keys: the sampler's channel, or (the pads) the drums' */
        uint32_t e = evq[evq_r % EVQ];
        if (e & EV_INTERNAL)
            play_out(e & 0xFFu, e >> 8 & 0xFFu, e >> 16 & 0xFFu, 0);
        else
            played_in(e & 0xFFu, e >> 8 & 0xFFu, e >> 16 & 0xFFu, TO_USB);
        evq_r = evq_r + 1u;
    }
    midi_in();
    for (i = 0; i < n; i++)
        mixl[i] = mixr[i] = drl[i] = drr[i] = 0.0f;
    /* in stretches to the sequencer's next tick, so its events land on their frame */
    while (done < n) {
        uint32_t seg = sqp_frames_to_tick();
        if (seg > n - done)
            seg = n - done;
        if (seg) {
            s7_render(mixl + done, mixr + done, 0, seg);
            hr_render(drl + done, drr + done, 0, seg);
            click_render(mixl + done, mixr + done, seg);
        }
        done += seg;
        sqp_advance(seg);
    }
    for (i = 0; i < n; i++) {     /* the sampler's slots as the mix puts them, the drums' pads panned, their bus */
        float l, r;
        gain_now += (gain - gain_now) * 0.004f;   /* MASTER glides: no zipper */
        l = (mixl[i] + drl[i] * dr_gain) * gain_now;
        r = (mixr[i] + drr[i] * dr_gain) * gain_now;
        lr[2 * i] = l > 8388607.0f ? 8388607 : l < -8388607.0f ? -8388607 : (int32_t)l;
        lr[2 * i + 1] = r > 8388607.0f ? 8388607 : r < -8388607.0f ? -8388607 : (int32_t)r;
    }
    frames_now += n;
}

void app_panic(void)
{
    s7_kill();
    hr_kill();
}

/* ------------------------------------------------------------------------------------ the sequencer's memory --- */
/* the arena changes inside plat_lock, the player's places found again before the unlock (mmt8/seq.h) */
static uint32_t sq_lock_flags;
void sq_lock(void) { sq_lock_flags = plat_lock(); }
void sq_unlock(void)
{
    sqp_resync();
    hrp_resync();
    plat_unlock(sq_lock_flags);
}

/* a track being made (a take, an edit), one at a time (mmt8/seq.h SQ_SCRATCH) */
static uint8_t sq_scratch[SQ_SCRATCH] __attribute__((section(".pool")));
static uint8_t sq_dirty;

/* The memory in the flash: XDATA1 (0xE5000, 23 sectors). The update loader reads the last 256 bytes of every sector
 * there (plat.h), so each sector holds 3,840 bytes and keeps those erased. A header (magic, length, a checksum),
 * then the tables and the arena's used bytes. One copy: written only when stopped (SAVE) */
#define SQS_BASE PLAT_XDATA1_LO
#define SQS_SECTORS ((PLAT_XDATA1_HI - PLAT_XDATA1_LO) / 4096u)
#define SQS_PER 0xF00u
#define SQS_MAGIC 0x56535153u     /* "SQSV" */
#define SQS_TABLES sq_image_tables(SQ_VERSION)   /* (sq up to its arena) */

static uint32_t sqs_sum(const uint8_t *p, uint32_t n, uint32_t h)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;   /* FNV-1a */
    return h;
}

static const uint8_t *sqs_byte_at(uint32_t i)   /* byte i of the saved image (after the header), in place */
{
    const uint8_t *sec = plat_data(SQS_BASE + (i / SQS_PER) * 4096u);
    return sec ? sec + i % SQS_PER : 0;
}

static int sq_save(void)
{
    static uint8_t sec[4096];
    uint32_t total = SQS_TABLES + sq.used, hdr[3], k, at = 0;
    const uint8_t *src = (const uint8_t *)&sq;
    hdr[0] = SQS_MAGIC;
    hdr[1] = total;
    hdr[2] = sqs_sum(sq.arena, sq.used, sqs_sum(src, SQS_TABLES, 2166136261u));
    if ((total + sizeof hdr + SQS_PER - 1u) / SQS_PER > SQS_SECTORS)
        return -1;
    /* the image: the header, the tables, the arena's used bytes; 3,840 of them a sector */
    for (k = 0; at < total + sizeof hdr; k++) {
        uint32_t i;
        for (i = 0; i < 4096u; i++)
            sec[i] = 0xFFu;
        for (i = 0; i < SQS_PER && at < total + sizeof hdr; i++, at++) {
            uint32_t j = at;
            if (j < sizeof hdr)
                sec[i] = ((const uint8_t *)hdr)[j];
            else if ((j -= sizeof hdr) < SQS_TABLES)
                sec[i] = src[j];
            else
                sec[i] = sq.arena[j - SQS_TABLES];
        }
        if (plat_data_erase(SQS_BASE + k * 4096u) != 0 || plat_data_write(SQS_BASE + k * 4096u, sec, 4096u) != 0)
            return -1;
    }
    sq_dirty = 0;
    return 0;
}

static void sq_load(void)
{
    uint32_t hdr[3], i, total, tables;
    uint8_t *dst = (uint8_t *)&sq;
    const uint8_t *b;
    for (i = 0; i < sizeof hdr; i++) {
        if (!(b = sqs_byte_at(i)))
            goto empty;
        ((uint8_t *)hdr)[i] = *b;
    }
    total = hdr[1];
    if (hdr[0] != SQS_MAGIC || !(b = sqs_byte_at(sizeof hdr + 4u)) || !sqs_byte_at(sizeof hdr + 5u))
        goto empty;
    /* the tables as long as the image's version has them (sq.version, after sq.magic): an older one's upgraded */
    tables = sq_image_tables(b[0] | (uint32_t)sqs_byte_at(sizeof hdr + 5u)[0] << 8);
    if (!tables || total < tables || total - tables > SQ_ARENA)
        goto empty;
    for (i = 0; i < total; i++) {
        uint32_t j = i;
        b = sqs_byte_at(i + sizeof hdr);
        if (j < tables)
            dst[j] = *b;
        else
            sq.arena[j - tables] = *b;
    }
    if (sqs_sum(sq.arena, total - tables, sqs_sum(dst, tables, 2166136261u)) != hdr[2])
        goto empty;
    if (sq.used != total - tables || sq_image_upgrade(sq_scratch) != 0 || !sq_valid())
        goto empty;
    return;
empty:
    sq_clear();
}

/* the drums' recording: its hits into the pattern (or out of it, held to erase); STEP EDIT's pads at its place */
static void dr_step_add(uint32_t pad, uint32_t vel);
static void hr_service(void)
{
    hr_rec_t r;
    int full = 0;
    while (hrp_take(&r)) {
        if (r.kind == HR_REC_STEP) {
            dr_step_add(r.pad, r.vel);
            continue;
        }
        if (r.kind == HR_REC_ERASE) {
            full |= hr_remove(r.pattern, r.tick, r.pad, sq_scratch) != 0;
        } else {
            if (!sq.pattern[r.pattern].beats)   /* (its length now its own: the default it played at) */
                sq.pattern[r.pattern].beats = HR_DEFAULT_BEATS;
            full |= hr_add(r.pattern, r.tick, r.pad, r.vel, sq_scratch) != 0;
        }
        sq_dirty = 1;
    }
    if (full)
        app_say("MEMORY FULL", "");
    if (sqp.state == SQP_STOPPED && !sqp.aux && hr_snd.p != hrp.pattern) {
        uint32_t f = plat_lock();     /* stopped: the pads have the sounds of the pattern chosen */
        hr_sounds_apply(hrp.pattern);
        plat_unlock(f);
    }
}

/* a take done: made into its track (the sorting outside the lock, the move inside) */
static void take_service(void)
{
    char t[LCD_ROWBUF] = "TRACK ", n[4];
    int r;
    if (sqp.take_nothing) {                  /* stopped in the count-in, or nothing played into an empty part */
        uint32_t f = plat_lock();
        sqp.take_nothing = 0;
        plat_unlock(f);
        app_say("NOTHING RECORDED", "PLAY FROM BEAT 1");
    }
    if (sqp.take_state != SQP_TAKE_DONE)
        return;
    r = sq_commit(sqp.take_part, sqp.take_track, sqp_take, sqp.take_n, sqp.take_beats, sq_scratch);
    {
        uint32_t f = plat_lock();
        sqp.take_state = SQP_TAKE_NONE;
        plat_unlock(f);
    }
    fmt_uint(n, sqp.take_track + 1u, 1, ' ');
    str_cat(t, n);
    app_say(r == 0 ? (sqp.take_full ? "TAKE CUT SHORT" : "RECORDED") : "MEMORY FULL", t);
    sq_dirty = 1;
}

void app_init(void)
{
    hr_kit_bank(s7mem_bank());    /* (a kit built in with the samples: before the pads load) */
    settings_load();
    hrp.quant = set.dr_quant;
    hrp.swing = set.dr_swing;
    s7_init(SM_FS);
    s7_bend_range(set.bend_range);
    s7_midi_mode(set.s7_mode);
    s7_poly(set.s7_poly);
    {
        uint32_t i;
        for (i = 0; i < S7_SLOTS; i++)
            mix_apply(i);
    }
    s7mem_init();
    sds_init(stage[0], stage[1]);
    sq_load();
    hr_sounds_apply(hrp.pattern);
    sqp_init(SM_FS);
    sqp.clock_in = set.clock_in ? 1u : 0u;
    sqp.clock_out = set.clock_out_off ? 0u : 1u;
    seq_settings_apply();
    sqp_select_part(0);
}

/* ------------------------------------------------------------------------------------------- the S700's MIDI --- */
/* the FM-1 answers as an X7000 with 16 slots (s700/sds.h): sample dumps in (byte by byte, the USB interrupt) and out,
 * the parameters (SPRM) and the overall settings (OVS) both ways, on the sampler's channel */
static uint32_t sx_last_ms;       /* the last SysEx of ours: an automatic save waits for quiet */
static const uint8_t OVS_OMNI[3] = {0, 128, 64};   /* OVS's OMNION for S7_OMNI_OFF, S7_OMNI_ON, S7_MULTI */
static uint8_t sx_autosave;       /* parameters came over MIDI: saved once it is quiet */
static int32_t dump_request = -1; /* RSD: the slot to send, from the main loop */
static void say_slot(const char *a, const char *b, uint32_t slot);

static md_rx_t mdrx;              /* an MMT-8 dump coming in (mmt8/mmtdump.h) */
static md_rx_t hdrx = {.prefix = HD_PRE, .plen = HD_PREFIX};   /* an HR-16's (hr16/hrdump.h), packed the same */
static uint8_t hd_requested;      /* an HR-16's request for our dump: sent from the main loop, when stopped */
static uint8_t *md_take(uint32_t *max);

void app_sysex_byte(uint8_t b)
{
    sds_rx_byte(b);
    md_rx_byte(&mdrx, b, md_take);
    md_rx_byte(&hdrx, b, md_take);
}

/* the slot a dump was last said RECEIVED for, and when: its parameters just after (as Axs7 sends them) said with it */
static uint32_t rx_said_slot = 0xFFu, rx_said_ms;
/* a slot's last dump cut short on the way (sds_expected): the words that came and those it should have brought (0:
 * not cut), said again when the slot's parameters come after it (Axs7 sends them once the dump has gone out at MIDI's
 * speed, seconds later) */
static uint16_t cut_got[S7_SLOTS], cut_want[S7_SLOTS];
static uint32_t dump_cuts;        /* (the host's checks) */
static void say_cut(uint32_t slot);

/* the dumps received, into the flash, in the order they came (before any SPRM after them is taken) */
static void dumps_flush(void)
{
    for (;;) {
        sds_dump_t *d = 0;
        uint32_t i, len;
        int r;
        for (i = 0; i < SDS_BUFS; i++)
            if (sds_buf[i].state == SDS_READY && (!d || sds_buf[i].seq - d->seq > 0x80000000u))
                d = &sds_buf[i];
        if (!d)
            return;
        len = sds_length(d);
        if (d->slot >= S7_SLOTS) {
            say_slot("NO SUCH SLOT", "", d->slot);
        } else if ((r = s7mem_write(d->slot, d->data, len)) != 0) {
            say_slot(r == -1 ? "MEMORY FULL" : "WRITE FAILED", "SLOT ", d->slot);
        } else {
            uint32_t f = plat_lock(), want = sds_expected(d);
            sds_apply(d, s7_slot[d->slot].p);
            plat_unlock(f);
            sx_autosave = 1;
            cut_got[d->slot] = (uint16_t)d->words;
            cut_want[d->slot] = (uint16_t)(d->words < want ? want : 0u);
            if (cut_want[d->slot]) {   /* bytes lost on the way: what came is kept (its loop turns early), and said */
                dump_cuts++;
                rx_said_slot = 0xFFu;
                say_cut(d->slot);
            } else {
                say_slot("RECEIVED", "SLOT ", d->slot);
                rx_said_slot = d->slot;
                rx_said_ms = plat_ms();
            }
        }
        d->state = SDS_FREE;
        sx_last_ms = plat_ms();
    }
}

/* a dump going out (RSD), a few blocks a frame: the UI keeps going, and the computer reads as it comes */
#define TX_BLOCKS_A_FRAME 16u
static struct {
    uint8_t on, slot;
    sds_tx_t t;
    uint32_t next, blocks;
} tx;

static void dump_send(uint32_t slot)
{
    uint8_t h[SDS_HEADER];
    if (tx.on || plat_sysex_begin() != 0)
        return;
    tx.blocks = sds_tx_header(&tx.t, &s7_slot[slot], slot, h);
    tx.next = 0;
    tx.slot = (uint8_t)slot;
    tx.on = plat_sysex_put(h, SDS_HEADER) == 0;
    if (tx.on)
        s7mem_hold(slot);         /* (its sectors kept as they are till it has gone: a dump in for it goes elsewhere) */
    else
        (void)plat_sysex_end();
}

static void dump_send_service(void)
{
    uint8_t blk[SDS_BLOCK], f7 = 0xF7;
    uint32_t k;
    if (!tx.on)
        return;
    for (k = 0; k < TX_BLOCKS_A_FRAME && tx.next < tx.blocks; k++, tx.next++) {
        sds_tx_block(&tx.t, tx.next, blk);
        if (plat_sysex_put(blk, SDS_BLOCK) != 0) {   /* nobody reading: the frame is cut, given up */
            (void)plat_sysex_end();
            tx.on = 0;
            s7mem_hold(S7_SLOTS);
            app_say("SEND CUT SHORT", "");
            return;
        }
    }
    if (tx.next == tx.blocks) {
        (void)plat_sysex_put(&f7, 1);
        (void)plat_sysex_end();
        tx.on = 0;
        s7mem_hold(S7_SLOTS);
        say_slot("SENT", "SLOT ", tx.slot);
    }
    sx_last_ms = plat_ms();
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t ch = set.s7_channel;
    dumps_flush();
    if (n >= 5u && p[0] == 0x00u && p[1] == 0x00u && p[2] == 0x0Eu && p[3] == HD_ID) {   /* the HR-16's */
        static const uint8_t ANSWER[8] = {0xF0, 0x00, 0x00, 0x0E, HD_ID, HD_CMD_TEST, 0x01, 0xF7};
        if (p[4] == HD_CMD_REQUEST)
            hd_requested = 1;
        else if (p[4] == HD_CMD_TEST && n >= 6u && p[5] == 0x00u)   /* the MIDI test: answered */
            (void)plat_sysex_send(ANSWER, sizeof ANSWER);
        return 1;                            /* (a dump comes by the byte path) */
    }
    if (n >= 1u && p[0] == 0x7Eu) {          /* the sample dump's: a request, the handshake, a dump (byte path) */
        if (n >= 3u && p[1] == 0x00u)        /* RSD: F0 7E 00 nn 00 F7 */
            dump_request = p[2] < S7_SLOTS ? (int32_t)p[2] : -1;
        return 1;
    }
    if (n < 6u || p[0] != 0x47u || p[1] != ch || p[3] != AKAI_ID)
        return 0;                            /* not Akai's, or not our channel */
    sx_last_ms = plat_ms();
    switch (p[2]) {
    case 0x01: {                             /* ROVS: the overall settings */
        uint8_t o[OVS_LEN];
        s7_ovs_t v = {(uint8_t)ch, 3, (uint8_t)ch, OVS_OMNI[set.s7_mode], set.bend_range, 0, 5, 127};
        ovs_build(o, ch, &v);
        (void)plat_sysex_send(o, OVS_LEN);
        app_say("MIDI SETTINGS", "SENT");
        break;
    }
    case 0x04:                               /* RSPRM: a slot's parameters */
        if (p[4] < S7_SLOTS) {
            static uint8_t o[SPRM_LEN];
            sprm_build(o, ch, p[4], s7_slot[p[4]].p);
            (void)plat_sysex_send(o, SPRM_LEN);
            say_slot("PARAMETERS SENT", "SLOT ", p[4]);
        }
        break;
    case 0x05:
    case 0x06:                               /* SECRE / SECRD: sample dumps taken or not */
        sds_enabled = p[2] == 0x05u;
        break;
    case 0x08: {                             /* OVS: the channel, the MIDI mode, the pitch wheel's range */
        s7_ovs_t v = {(uint8_t)ch, 3, (uint8_t)ch, OVS_OMNI[set.s7_mode], set.bend_range, 0, 5, 127};
        if (ovs_parse(p, n, &v) == 0 && v.channel < 16u && v.bend_range <= 12u) {
            uint32_t m, f;
            for (m = 0; m <= S7_MULTI && OVS_OMNI[m] != v.omni; m++)
                ;
            set.s7_channel = v.channel;
            set.bend_range = v.bend_range;
            f = plat_lock();
            s7_bend_range(v.bend_range);
            if (m <= S7_MULTI && m != set.s7_mode) {   /* (another value: the mode kept) */
                set.s7_mode = (uint8_t)m;
                s7_all_off();
                s7_midi_mode(m);
            }
            plat_unlock(f);
            set_dirty = 1;
            app_say("MIDI SETTINGS", "RECEIVED");
        }
        break;
    }
    case 0x0A: {                             /* SPRM: a slot's parameters */
        int32_t slot = sprm_check(p, n, ch);
        if (slot >= 0) {
            uint32_t f = plat_lock();
            sprm_take(p, s7_slot[slot].p);
            plat_unlock(f);
            s7mem_touch();
            sx_autosave = 1;
            if (cut_want[slot]) {   /* after a dump cut short: that again, not "PARAMETERS IN" */
                say_cut((uint32_t)slot);
                cut_want[slot] = 0;
            } else if ((uint32_t)slot == rx_said_slot && plat_ms() - rx_said_ms < 1300u) {   /* its dump's, just said */
                char t[LCD_ROWBUF] = "SLOT ", k[4];
                fmt_uint(k, (uint32_t)slot + 1u, 2, '0');
                str_cat(t, k);
                str_cat(t, " + PARAMS");
                app_say("RECEIVED", t);
            } else {
                say_slot("PARAMETERS IN", "SLOT ", (uint32_t)slot);
            }
            rx_said_slot = 0xFFu;
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

static void acks_service(void);

/* the main loop's part of the S700's MIDI: the ACKS owed, dumps into the flash, a dump out, the save once the transfer
 * is over */
static void s700_midi_service(void)
{
    acks_service();
    dumps_flush();
    if (dump_request >= 0 && !tx.on) {
        uint32_t slot = (uint32_t)dump_request;
        dump_request = -1;
        dump_send(slot);
        sx_last_ms = plat_ms();
    }
    dump_send_service();
    if (sx_autosave && plat_ms() - sx_last_ms > 3000u && !sds_rx_progress()) {
        sx_autosave = 0;
        if (s7mem_save() != 0)
            app_say("SAVE", "FAILED");
    }
}

/* ------------------------------------------------------------------------------ the MMT-8's and HR-16's MIDI --- */
/* their memory dumps (mmt8/mmtdump.h, hr16/hrdump.h), in and out over USB, in the sample dumps' staging buffers
 * (98 KB, lent while neither holds a dump): an MMT-8's coming in replaces the sequencer's parts and songs, an
 * HR-16's its drum patterns; MIDI DUMP (ALGO on the SEQ page, and on the drum page) sends ours, as does an HR-16's
 * request */
static uint8_t *md_take(uint32_t *max)    /* the USB interrupt: the buffers, when free */
{
    if (!sds_hold())
        return 0;
    *max = MD_MAX;
    return &stage[0][0];
}

static struct {
    uint8_t on;
    uint32_t n, k, m;             /* the dump's bytes; the MIDI bytes sent, of m */
    const char *busy, *done;      /* what the LCD says meanwhile, and after */
} mdtx;

/* the ACKS a dump coming in is owed (sds.h sds_acks), out as the USB ring has room: a sender that waits for each
 * (closed loop) gets it within a frame, one that doesn't counts them. Not while a dump of ours streams out (the ring
 * is its own then); more than a dump's worth behind (nobody reading), given up */
static uint32_t acks_sent;
static void acks_service(void)
{
    static const uint8_t ACKS[4] = {0xF0, 0x7E, 0x7F, 0xF7};
    uint32_t owed = sds_acks - acks_sent;
    if (owed > 2u * 547u) {
        acks_sent = sds_acks;
        return;
    }
    for (; owed && !tx.on && !mdtx.on && plat_sysex_room() >= 6u; owed--) {   /* (two packets of three bytes) */
        if (plat_sysex_send(ACKS, sizeof ACKS) != 0)
            break;
        acks_sent++;
    }
}

#define MD_TX_A_FRAME 2048u       /* MIDI bytes a frame */

/* the staging buffers for a dump going out: 1 held (0: one is sending or coming in, said) */
static int dump_hold(void)
{
    uint32_t f;
    int ok;
    if (mdtx.on || tx.on) {
        app_say("SENDING ALREADY", "");
        return 0;
    }
    f = plat_lock();
    ok = mdrx.st != MD_RX_DATA && mdrx.st != MD_RX_DONE && hdrx.st != MD_RX_DATA && hdrx.st != MD_RX_DONE &&
         sds_hold();
    plat_unlock(f);
    if (!ok)
        app_say("BUSY", "A DUMP COMING IN");
    return ok;
}

/* a dump made in the buffers (n bytes; -1 too big) out: its prefix now, its bytes packed a frame at a time */
static void dump_go(const uint8_t *pre, uint32_t npre, int32_t n, const char *what)
{
    static char busy[LCD_COLS + 1], done[LCD_COLS + 1];
    if (n < 0) {
        sds_release();
        busy[0] = 0;
        str_cat(busy, "FOR AN ");
        str_cat(busy, what);
        app_say("TOO BIG", busy);
        return;
    }
    if (plat_sysex_begin() != 0 || plat_sysex_put(pre, npre) != 0) {
        (void)plat_sysex_end();
        sds_release();
        app_say("USB NOT READY", "");
        return;
    }
    busy[0] = done[0] = 0;
    str_cat(busy, "SENDING ");
    str_cat(busy, what);
    str_cat(done, what);
    str_cat(done, " DUMP SENT");
    mdtx.on = 1;
    mdtx.n = (uint32_t)n;
    mdtx.k = 0;
    mdtx.m = md_packed_len((uint32_t)n);
    mdtx.busy = busy;
    mdtx.done = done;
}

/* MIDI DUMP: ours, as an MMT-8's, out over USB */
static void md_send(void)
{
    static const uint8_t PRE[MD_PREFIX] = {0xF0, 0x00, 0x00, 0x0E, 0x00};
    if (dump_hold())
        dump_go(PRE, MD_PREFIX, md_export(&stage[0][0], MD_MAX), "MMT-8");
}

/* MIDI DUMP on the drum page, or an HR-16's request: the drum patterns, as an HR-16's dump */
static void hd_send(void)
{
    hd_settings_t s;
    s.quant = set.dr_quant;
    s.swing = set.dr_swing;
    s.channel = set.dr_channel;
    s.tempo = sqp.tempo;
    s.prog = set.dr_prog;
    if (dump_hold())
        dump_go(HD_PRE, HD_PREFIX, hd_export(&stage[0][0], HD_MAX, &s), "HR-16");
}

static void md_send_service(void)
{
    uint8_t chunk[256], f7 = 0xF7;
    uint32_t sent = 0;
    if (!mdtx.on)
        return;
    while (sent < MD_TX_A_FRAME && mdtx.k < mdtx.m) {
        uint32_t c = 0;
        while (c < sizeof chunk && mdtx.k < mdtx.m)
            chunk[c++] = md_packed_byte(&stage[0][0], mdtx.n, mdtx.k++);
        if (plat_sysex_put(chunk, c) != 0) {   /* nobody reading: given up */
            (void)plat_sysex_end();
            mdtx.on = 0;
            sds_release();
            app_say("SEND CUT SHORT", "");
            return;
        }
        sent += c;
    }
    if (mdtx.k == mdtx.m) {
        char t[LCD_ROWBUF] = "", n[8];
        (void)plat_sysex_put(&f7, 1);
        (void)plat_sysex_end();
        mdtx.on = 0;
        sds_release();
        fmt_uint(n, mdtx.n, 0, ' ');
        str_cat(t, n);
        str_cat(t, " BYTES");
        app_say(mdtx.done, t);
    }
}

/* ------------------------------------------------------------------------------------------------------ UI --- */
#include "ui/pages_seq.c"
#include "ui/pages_drum.c"
#define PG_SEQ PG_N               /* the sequencer's page, after the sampler's */
#define PG_DRUM (PG_N + 1u)       /* the drums' */
#define PG_MIDI (PG_N + 2u)       /* GLO: MIDI, the S700's (its basic channel, OMNI OFF / ON / MULTI, the bend's range) and
                                   * the drums' channel */

static const char *const S7_MODE_TXT[3] = {"OFF", "ON", "MULT"};

/* "CH  OMNI BEND DR" / "01  MULT   2 10" */
static void page_midi(void)
{
    char t[LCD_ROWBUF], n[4];
    uint32_t i;
    for (i = 0; i < LCD_COLS; i++)
        t[i] = ' ';
    t[LCD_COLS] = 0;
    fmt_uint(n, set.s7_channel + 1u, 2, '0');
    t[0] = n[0], t[1] = n[1];
    for (i = 0; S7_MODE_TXT[set.s7_mode % 3u][i]; i++)
        t[4 + i] = S7_MODE_TXT[set.s7_mode % 3u][i];
    fmt_uint(n, set.bend_range, 2, ' ');
    t[10] = n[0], t[11] = n[1];
    fmt_uint(n, set.dr_channel + 1u, 2, '0');
    t[14] = n[0], t[15] = n[1];
    lcd_row(0, "CH  OMNI BEND DR");
    lcd_row(1, t);
}

#define PG_MIX (PG_N + 3u)        /* GLO again: the mix, each slot's level and pan, the drums' bus */

#define PG_VOICES (PG_N + 4u)     /* GLO a third time: the sampler's voices */

/* "VOICES (S700)  6" (the S700's six) or "VOICES        10", and "PLAYING        3": the voices sounding now (one
 * note may take several, a sample each). K1 sets it */
static void page_voices(void)
{
    char t[LCD_ROWBUF], n[4];
    uint32_t p = s7_poly_now();
    fmt_uint(n, p, 2, ' ');
    ed_lr(t, p == S7_POLY_STD ? "VOICES (S700)" : "VOICES", n);
    lcd_row(0, t);
    fmt_uint(n, s7_voices_active(), 2, ' ');
    ed_lr(t, "PLAYING", n);
    lcd_row(1, t);
}

/* K1: the voices a note may take, 6 (as the S700) to 16. More cost the audio interrupt more: the strip's CPU says
 * what is left */
static void voices_turn(uint32_t k, int32_t e)
{
    uint32_t f, v;
    if (k != EN_K1)
        return;
    v = (uint32_t)clamp((int32_t)set.s7_poly + e, (int32_t)S7_POLY_STD, (int32_t)S7_VOICES);
    if (v == set.s7_poly)
        return;
    set.s7_poly = (uint8_t)v;
    f = plat_lock();
    s7_poly(v);
    plat_unlock(f);
    set_dirty = 1;
}

/* CLEAR (EDIT's last page): "CLEAR    SLOT 05" / what it holds, "BUILT-IN SAMPLE", "SAMPLE       48K" (the flash it
 * takes) or "TEST TONE" (nothing to clear). Its name is on the strip under */
static void page_clear(uint32_t slot)
{
    char t[LCD_ROWBUF], n[12] = "SLOT ", m[4];
    fmt_uint(m, slot + 1u, 2, '0');
    str_cat(n, m);
    ed_lr(t, "CLEAR", n);
    lcd_row(0, t);
    if (s7mem_is_rom(slot)) {
        lcd_row(1, "BUILT-IN SAMPLE");
    } else if (s7mem_has_sample(slot)) {
        fmt_uint(n, S7M_SECTORS_FOR(s7_slot[slot].words) * (S7M_SECTOR / 1024u), 0, ' ');
        str_cat(n, "K");
        ed_lr(t, "SAMPLE", n);
        lcd_row(1, t);
    } else {
        lcd_row(1, "TEST TONE");
    }
}

/* REC on CLEAR: the slot's sample gone, a built-in one or one received (its flash free for the next), as an X7000's
 * empty slot: the test tone, its parameters as the sampler starts them, in no program. SAVE keeps it, or a dump
 * received after it (until then a power cycle brings the sample back; a dump into its flash saves the slots first,
 * s7mem_write). Not while the slot goes out as a dump: the dump reads it */
static void slot_clear(uint32_t slot)
{
    if (tx.on) {
        app_say("BUSY", "SENDING A DUMP");
        return;
    }
    if (!s7mem_has_sample(slot)) {
        say_slot("NOTHING TO CLEAR", "SLOT ", slot);
        return;
    }
    s7mem_clear(slot);
    cut_want[slot] = 0;
    say_slot("CLEARED", "SLOT ", slot);
}

/* "LEV PAN SLOT DRM" / "99  L20  01  99" */
static void page_mix(uint32_t slot)
{
    char t[LCD_ROWBUF], n[4];
    uint32_t i;
    int32_t p = set.mix_pan[slot];
    for (i = 0; i < LCD_COLS; i++)
        t[i] = ' ';
    t[LCD_COLS] = 0;
    fmt_uint(n, set.mix_level[slot], 2, ' ');
    t[0] = n[0], t[1] = n[1];
    if (p) {                      /* "L20", "R50", the middle " C" */
        t[4] = p < 0 ? 'L' : 'R';
        fmt_uint(n, (uint32_t)(p < 0 ? -p : p), 2, '0');
        t[5] = n[0], t[6] = n[1];
    } else {
        t[5] = 'C';
    }
    fmt_uint(n, slot + 1u, 2, '0');
    t[9] = n[0], t[10] = n[1];
    fmt_uint(n, set.drum_level, 2, ' ');
    t[13] = n[0], t[14] = n[1];
    lcd_row(0, "LEV PAN SLOT DRM");
    lcd_row(1, t);
}

/* K1 the slot's level, K2 its pan, K3 the slot (as SELECT), K4 the drums' bus */
static void mix_turn(uint32_t k, int32_t e, uint8_t *slot)
{
    uint32_t s = *slot;
    if (k == EN_K1)
        set.mix_level[s] = (uint8_t)clamp((int32_t)set.mix_level[s] + e, 0, 99);
    else if (k == EN_K2)
        set.mix_pan[s] = (int8_t)clamp(set.mix_pan[s] + e, -MIX_PAN_MAX, MIX_PAN_MAX);
    else if (k == EN_K3) {
        *slot = (uint8_t)clamp((int32_t)s + e, 0, S7_SLOTS - 1);
        return;
    } else if (k == EN_K4)
        set.drum_level = (uint8_t)clamp((int32_t)set.drum_level + e, 0, 99);
    else
        return;
    mix_apply(s);
    set_dirty = 1;
}

/* K1 the sampler's channel, K2 its mode, K3 the bend's range, K4 the drums' channel */
static void midi_turn(uint32_t k, int32_t e)
{
    uint32_t f;
    if (k == EN_K1) {
        set.s7_channel = (uint8_t)clamp((int32_t)set.s7_channel + e, 0, 15);
    } else if (k == EN_K2) {
        uint32_t m = (uint32_t)clamp((int32_t)set.s7_mode + e, S7_OMNI_OFF, S7_MULTI);
        if (m == set.s7_mode)
            return;
        set.s7_mode = (uint8_t)m;
        f = plat_lock();
        s7_all_off();             /* (a note held stays where it was: let go) */
        s7_midi_mode(m);
        plat_unlock(f);
    } else if (k == EN_K3) {
        set.bend_range = (uint8_t)clamp((int32_t)set.bend_range + e, 0, 12);
        f = plat_lock();
        s7_bend_range(set.bend_range);
        plat_unlock(f);
    } else if (k == EN_K4) {
        set.dr_channel = (uint8_t)clamp((int32_t)set.dr_channel + e, 0, 15);
    } else {
        return;
    }
    set_dirty = 1;
}

/* an HR-16's dump in: the drums stopped, their patterns replaced, its swing and quantize the recording's */
static void hd_service(void)
{
    hd_report_t r;
    char t[LCD_ROWBUF] = "", n[8];
    uint32_t f;
    if (hdrx.over || hdrx.n < HD_HDR) {
        app_say("HR-16 DUMP", hdrx.over ? "NOT TAKEN" : "TOO SHORT");
        return;
    }
    f = plat_lock();
    sqp_stop();
    hrp.rec = 0;
    hrp.next = 0xFFu;
    plat_unlock(f);
    (void)hd_import(&stage[0][0], hdrx.n, sq_scratch, &r);
    if (r.quant >= 0)
        set.dr_quant = (uint8_t)r.quant;
    if (r.swing >= 0)             /* (at most its quantize's: its 1/6 is our 1/8) */
        set.dr_swing = (uint8_t)((uint32_t)r.swing > hr_swing_max(HR_GRID[set.dr_quant])
                                     ? hr_swing_max(HR_GRID[set.dr_quant])
                                     : (uint32_t)r.swing);
    if (r.prog >= 0)
        set.dr_prog = (uint8_t)r.prog;
    f = plat_lock();
    hrp.quant = set.dr_quant;
    hrp.swing = set.dr_swing;
    hr_sounds_apply(hrp.pattern);   /* (the pads: the chosen pattern's sounds, as they came) */
    plat_unlock(f);
    dr.beats = (uint16_t)hr_beats(hrp.pattern);
    set_dirty = 1;
    sq_dirty = 1;
    if (r.lost && !r.cut && !r.bad) {   /* the sequencer's memory full: from this pattern on not taken */
        fmt_uint(n, (uint32_t)r.first_lost, 2, '0');
        str_cat(t, "FROM PATT ");
        str_cat(t, n);
        app_say("MEMORY FULL", t);
        return;
    }
    fmt_uint(n, r.patterns, 0, ' ');
    str_cat(t, n);
    str_cat(t, " PATTERNS");
    /* bytes lost on the way, or a pattern not readable: what came is taken, and said */
    app_say(r.cut ? "DUMP CUT SHORT" : r.bad ? "DUMP DAMAGED" : r.songs ? "SONGS NOT TAKEN" : "HR-16 DUMP IN", t);
}

/* a dump in: the sequencer stopped, its memory replaced */
static void md_service(void)
{
    if (mdrx.st == MD_RX_DONE) {
        md_report_t r;
        uint32_t f;
        if (mdrx.over || mdrx.n < MD_HDR) {
            app_say("MMT-8 DUMP", mdrx.over ? "NOT TAKEN" : "TOO SHORT");
        } else {
            char t[LCD_ROWBUF] = "", n[8];
            f = plat_lock();
            sqp_stop();
            plat_unlock(f);
            (void)md_import(&stage[0][0], mdrx.n, sq_scratch, &r);
            f = plat_lock();
            if (sqp.song_mode)
                sqp_select_song(0);
            else
                sqp_select_part(0);
            plat_unlock(f);
            seq_last_part = 0;
            seq_song_step = 0;
            ed_load();
            sq_dirty = 1;
            if (r.full && !r.cut && !r.bad && r.first_lost >= 0) {   /* the sequencer full: from this part on not taken */
                fmt_uint(n, (uint32_t)r.first_lost, 2, '0');
                str_cat(t, "FROM PART ");
                str_cat(t, n);
                app_say("MEMORY FULL", t);
            } else {              /* what was taken; above it, why not all (bytes lost on the way first: send again) */
                fmt_uint(n, r.parts, 0, ' ');
                str_cat(t, n);
                str_cat(t, " PTS ");
                fmt_uint(n, r.songs, 0, ' ');
                str_cat(t, n);
                str_cat(t, " SONGS");
                app_say(r.cut ? "DUMP CUT SHORT" : r.bad ? "DUMP DAMAGED" : r.full ? "MEMORY FULL" : "MMT-8 DUMP IN", t);
            }
        }
        sds_release();
        mdrx.st = MD_RX_IDLE;
    }
    if (hdrx.st == MD_RX_DONE) {
        hd_service();
        sds_release();
        hdrx.st = MD_RX_IDLE;
    }
    if (hd_requested) {           /* (as the HR-16: not while playing) */
        hd_requested = 0;
        if (sqp.state == SQP_STOPPED && !sqp.aux && !mdtx.on)
            hd_send();
    }
    md_send_service();
}


static struct {
    uint32_t keys, btn;           /* held at the last input */
    uint8_t key_note[NKEYS];      /* the note each held key plays (the octave may change while held); 0xFF none,
                                   * 0xFE a pad held to erase */
    uint8_t key_ch[NKEYS];        /* its channel */
    char msg[2][LCD_COLS + 1];    /* a message over the page for a moment */
    uint32_t msg_until;
    uint8_t redraw;
    uint8_t page;                 /* PG_* (ui/pages_s700.c), PG_SEQ */
    uint8_t slot;                 /* the slot the sampler's pages edit, 0..15 */
    uint8_t rec_used;             /* REC held: a track key chose the track (its release does not arm) */
    uint8_t rec_manual;           /* the record track chosen (REC + its key): kept until another part */
    uint8_t rec_none;             /* none chosen: REC + the armed track's key again (the MMT-8's), until REC + a key */
} ui;

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < LCD_COLS && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < LCD_COLS && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        char t[40] = "";
        str_cat(t, ui.msg[0]);
        str_cat(t, " ");
        str_cat(t, ui.msg[1]);
        ann_say(t, 0, ANN_NOW);
    }
}

static int unsaved(void) { return set_dirty || s7mem_dirty() || sq_dirty; }

static void save_all(void)
{
    int ok = 1;
    if (sqp.state != SQP_STOPPED && sq_dirty) {
        app_say("STOP FIRST", "");          /* a save stops the sound a moment */
        return;
    }
    if (set_dirty) {
        if (settings_save() == 0)
            set_dirty = 0;
        else
            ok = 0;
    }
    if (s7mem_dirty() && s7mem_save() != 0)
        ok = 0;
    if (sq_dirty && sq_save() != 0)
        ok = 0;
    app_say(ok ? "SAVED" : "SAVE", ok ? "" : "FAILED");
}

static void program_step(int32_t d)
{
    int32_t p = (int32_t)s7_current_program() + d;
    p = p < 0 ? 0 : p > 31 ? 31 : p;
    ev_push(0xC0u | set.s7_channel, (uint32_t)p, 0);
    ui.redraw = 1;
}

/* the transport, any page: PLAY plays from the top or stops, SEL + PLAY continues; REC (released without a track
 * key) arms recording, or punches out a take that runs. Armed, as the MMT-8: on the part's lowest empty track,
 * unless one was chosen (REC + its key); a part with all eight recorded, none (choose one) */
static void transport_button(uint32_t k, uint32_t btn)
{
    const char *say = 0;
    uint32_t f = plat_lock();
    if (k == B_PLAY) {
        if (sqp.state != SQP_STOPPED || sqp.aux) {
            sqp_stop();           /* (a part queued goes; a drum pattern queued too) */
            hrp.next = 0xFFu;
        }
        else if (ui.page == PG_DRUM) {      /* the drums alone (SEL: on from their beat); STEP EDIT left */
            if (dr.fn == DF_STEP)
                dr.fn = DF_PLAY;
            sqp_aux_play((int)(btn >> B_SEL & 1u));
        }
        else if (btn >> B_SEL & 1u)
            sqp_continue();
        else
            sqp_play();
    } else if (k == B_REC && ui.page == PG_DRUM) {
        dr_rec_arm();
    } else if (k == B_REC) {
        if (sqp.take_state == SQP_TAKE_RUNNING) {
            sqp_punch();
        } else if (sqp.rec_armed) {
            sqp.rec_armed = 0;
        } else if (sqp.song_mode) {
            say = "RECORD A PART";        /* (the MMT-8 records parts; a song plays them) */
        } else {
            uint32_t used = sq_part_used(sqp.part), t = 0;
            if (!ui.rec_manual) {
                while (t < SQ_TRACKS && (used >> t & 1u))
                    t++;
                if (t < SQ_TRACKS)
                    sqp.rec_track = (uint8_t)t;
            }
            if (ui.rec_manual && ui.rec_none)
                say = "NO TRACK CHOSEN";  /* (the MMT-8's RECORD stays dark) */
            else if (t < SQ_TRACKS)
                sqp.rec_armed = 1;
            else
                say = "NO EMPTY TRACK";
        }
    }
    plat_unlock(f);
    if (say)
        app_say(say, say[0] == 'R' ? "SEQ: PART MODE" : "REC + KEY: TRACK");
    ui.redraw = 1;
}

/* a track key on the sequencer's page: SEL held, its mute (and the song step's); REC held, the record track (the
 * armed one again: none, as the MMT-8's, until REC + a key chooses one) */
static void track_key(uint32_t t, uint32_t btn)
{
    uint32_t f;
    if (btn >> B_REC & 1u) {
        f = plat_lock();
        if (sqp.rec_armed && sqp.rec_track == t && sqp.take_state != SQP_TAKE_RUNNING) {
            sqp.rec_armed = 0;
            ui.rec_none = 1;
        } else {
            sqp.rec_track = (uint8_t)t;
            sqp.rec_armed = 1;
            ui.rec_none = 0;
        }
        plat_unlock(f);
        ui.rec_used = 1;
        ui.rec_manual = 1;
    } else {
        f = plat_lock();
        sqp.mask ^= (uint8_t)(1u << t);
        plat_unlock(f);
        if (sqp.song_mode && sqp.state == SQP_STOPPED && seq_song_step < sq_song_steps(sqp.song)) {
            uint8_t part, mask;
            sq_song_step(sqp.song, seq_song_step, &part, &mask);
            (void)sq_song_set(sqp.song, seq_song_step, part, sqp.mask, sq_scratch);
            sq_dirty = 1;
        }
    }
    ui.redraw = 1;
}

/* the sequencer's encoders */
/* SELECT on the SEQ page: the place, as the MMT-8's FF/RW. A part by beats (SEL: by four); a song by steps (stopped:
 * the step shown and edited, up to its END, and SEL + PLAY plays from its start), SEL by beats over them. Not while
 * recording, nor following MIDI clock (its song position sets the place) */
/* past a part's end into the part queued (sqp_locate): the rest of the turn dropped, SELECT not moving the place until
 * it has rested this long */
#define LOCATE_REST_MS 300u
static uint8_t locate_held;
static uint32_t locate_ms;

static void seq_locate(int32_t e, int sel)
{
    uint32_t f, n = sq_song_steps(sqp.song), was;
    int32_t moved;
    if (locate_held && plat_ms() - locate_ms < LOCATE_REST_MS) {
        locate_ms = plat_ms();    /* (still turning: held) */
        return;
    }
    locate_held = 0;
    if (sqp.song_mode && sqp.state == SQP_STOPPED && !sel) {
        int32_t st = (int32_t)seq_song_step + e;
        seq_song_step = (uint32_t)(st < 0 ? 0 : st > (int32_t)n ? (int32_t)n : st);
        f = plat_lock();
        sqp_to_step(seq_song_step);
        plat_unlock(f);
        return;
    }
    if (sqp.state == SQP_COUNTIN || sqp.take_state == SQP_TAKE_RUNNING || hrp_recording()) {
        app_say("RECORDING", "STOP FIRST");
        return;
    }
    if (sqp.clock_in) {
        app_say("CLOCK: MIDI", "IT SETS THE BEAT");
        return;
    }
    if (!sqp.song_mode && !sq.part[sqp.part].beats) {
        app_say("EMPTY PART", "NO BEATS YET");
        return;
    }
    f = plat_lock();
    if (sqp.song_mode) {
        (void)sqp_locate(sel ? 0 : e, sel ? e : 0);
    } else {
        was = sqp.part;
        moved = sqp_locate(0, e * (sel ? 4 : 1));
        if (moved)
            hrp_shift(moved);     /* (the drums' own loop: as far, so they stay with the part) */
        if (sqp.part != was) {    /* into the part queued: the scroll stops there */
            locate_held = 1;
            locate_ms = plat_ms();
        }
    }
    plat_unlock(f);
    if (sqp.song_mode && sqp.state == SQP_STOPPED)
        seq_song_step = sqp.step;
}

static void seq_turn(uint32_t enc, int32_t e, uint32_t btn)
{
    uint32_t f;
    if (enc == EN_PRESET && !sqp.song_mode && sqp.state != SQP_STOPPED && !sq.part[sqp.part].beats &&
        (sqp.take_state == SQP_TAKE_RUNNING || sqp.rec_armed)) {
        app_say("FIRST TAKE", "REC SETS LENGTH");   /* (an empty part's first take: open-ended until stopped) */
        return;
    }
    if (enc == EN_PRESET) {                  /* the part (queued while playing), or the song */
        int32_t v = (int32_t)(sqp.song_mode ? sqp.song : sqp.next_part != 0xFFu ? sqp.next_part : sqp.part) + e;
        v = v < 0 ? 0 : v > 99 ? 99 : v;
        f = plat_lock();
        if (sqp.song_mode)
            sqp_select_song((uint32_t)v);
        else
            sqp_select_part((uint32_t)v);
        plat_unlock(f);
        if (!sqp.song_mode) {
            seq_last_part = (uint8_t)v;
            ui.rec_manual = ui.rec_none = 0;   /* another part: its lowest empty track again */
        }
        seq_song_step = 0;
    } else if (enc == EN_K1) {               /* the tempo (a song keeps its own) */
        f = plat_lock();
        sqp_set_tempo((uint32_t)((int32_t)sqp.tempo + e));
        plat_unlock(f);
        if (sqp.song_mode) {
            sq.song[sqp.song].tempo = (uint8_t)sqp.tempo;
            sq_dirty = 1;
        }
    } else if (enc == EN_K4 && !sqp.song_mode) {   /* the drums' pattern with the part */
        dr_pattern_turn(e);
    } else if (enc == EN_K4) {               /* the song step's drum pattern: none, 00..99 */
        seq_step_drums(e, "K3 ADDS A STEP");
    } else if (enc == EN_K2) {               /* the record track's channel: UNCHANGED, 1..16 */
        int32_t c = (int32_t)sq.part[sqp.part].chan[sqp.rec_track] + e;
        sq.part[sqp.part].chan[sqp.rec_track] = (uint8_t)(c < 0 ? 0 : c > 16 ? 16 : c);
        sq_dirty = 1;
    } else if (enc == EN_SELECT) {           /* the place (a song stopped: the step to edit too) */
        seq_locate(e, (int)(btn >> B_SEL & 1u));
    } else if (sqp.song_mode && sqp.state == SQP_STOPPED && enc == EN_K3) {       /* the step's part; past 99: END */
        uint32_t n = sq_song_steps(sqp.song);
        uint8_t part = 100, mask = sqp.mask;
        int32_t v;
        if (seq_song_step < n)
            sq_song_step(sqp.song, seq_song_step, &part, &mask);
        v = seq_song_step < n ? (int32_t)part + e : (int32_t)seq_last_part;   /* at the END: the last part, appended */
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        if (sq_song_set(sqp.song, seq_song_step, v == 100 ? 0xFFu : (uint8_t)v, mask, sq_scratch) != 0)
            app_say("MEMORY FULL", "");
        sq_dirty = 1;
    }
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, lit_keys, dim_keys = 0, lit_btn;
    int32_t e;
    int tracks = ui.page == PG_SEQ && (ed.fn || (btn & (1u << B_SEL | 1u << B_REC)));
    hrp.fill = (uint8_t)(ui.page == PG_DRUM && (btn >> B_OCTUP & 1u));   /* the HR-16's FILL, before the pads below */
    touch_poll();
    keys = touch_keys();
    dr_lights_take();
    for (k = 0; k < NKEYS; k++) {            /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t note = (uint32_t)(53 + (int32_t)k + 12 * set.octave), v = touch_vel(k);
            ui.key_note[k] = 0xFFu;
            if (tracks && (key_track(k) >= 0 || !ed.fn)) {   /* the eight track buttons (a function's: the others play) */
                if (key_track(k) >= 0 && ed.fn)
                    ed_track((uint32_t)key_track(k));
                else if (key_track(k) >= 0)
                    track_key((uint32_t)key_track(k), btn);
                else if (k == WHITE[SQ_TRACKS] && (btn >> B_SEL & 1u))
                    hrp.mute = (uint8_t)!hrp.mute;   /* the ninth: the drums with the parts */
                ui.redraw = 1;
                continue;
            }
            if (ui.page == PG_DRUM) {        /* the white keys: the pads; the black ones nothing */
                int32_t pad = key_pad(k);
                uint32_t f;
                if (pad < 0)
                    continue;
                if ((btn >> B_SEL & 1u) && hrp.rec) {   /* SEL + a pad, recording: its hits out as they come */
                    f = plat_lock();
                    hrp.erase |= (uint16_t)(1u << pad);
                    plat_unlock(f);
                    ui.key_note[k] = 0xFEu;
                    ui.redraw = 1;
                    continue;
                }
                dr_pad_key((uint32_t)pad);
                note = hr_pad[pad].note;
                ui.key_ch[k] = set.dr_channel;
                ui.redraw = 1;
            } else {
                ui.key_ch[k] = (uint8_t)s7_keys_channel();
            }
            ui.key_note[k] = (uint8_t)note;
            ev_push(0x90u | ui.key_ch[k], note, v ? v : 1u);
        } else if (!(keys & bit) && (ui.keys & bit) && ui.key_note[k] == 0xFEu) {
            uint32_t f = plat_lock();
            hrp.erase &= (uint16_t)~(1u << key_pad(k));
            plat_unlock(f);
        } else if (!(keys & bit) && (ui.keys & bit) && ui.key_note[k] != 0xFFu) {
            ev_push(0x80u | ui.key_ch[k], ui.key_note[k], 64u);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (k == B_REC && !(btn & bit) && (ui.btn & bit)) {   /* REC released */
            if (!ui.rec_used)
                transport_button(B_REC, btn);
            ui.rec_used = 0;
        }
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if ((k == B_OCTDN || k == B_OCTUP) && ui.page != PG_DRUM) {   /* (the pads have no octave: OCT+ is FILL) */
            int32_t o = set.octave + (k == B_OCTUP ? 1 : -1);
            if (o >= -2 && o <= 2) {
                set.octave = (int8_t)o;
                set_dirty = 1;
                ui.redraw = 1;
            }
        } else if (k == B_SAVE) {
            save_all();
        } else if (k == B_PLAY) {
            transport_button(k, btn);
        } else if (k == B_REC && ui.page == PG_SEQ && ed.fn) {   /* a function's page: REC does it */
            ed_rec(btn);
            ui.rec_used = 1;
            ui.redraw = 1;
        } else if (k == B_REC && ui.page == PG_DRUM && dr.fn) {
            dr_rec(btn);
            ui.rec_used = 1;
            ui.redraw = 1;
        } else if (k == B_REC && ui.page == PG_CLEAR) {
            slot_clear(ui.slot);
            ui.rec_used = 1;
            ui.redraw = 1;
        } else if (k == B_ARP) {             /* the drums (from a function of theirs: back to the pattern) */
            if (ui.page == PG_DRUM)
                dr.fn = DF_PLAY;
            ui.page = PG_DRUM;
            ui.redraw = 1;
        } else if (k == B_SEQ) {             /* the sequencer; again: PART or SONG (from a function: back to it) */
            if (ui.page != PG_SEQ || ed.fn) {
                ed.fn = F_PLAY;
            } else if (sqp.state == SQP_STOPPED) {
                uint32_t f = plat_lock();
                sqp.song_mode = (uint8_t)!sqp.song_mode;
                if (sqp.song_mode) {
                    seq_last_part = sqp.part;
                    sqp_select_song(sqp.song);
                } else {
                    sqp.mask = 0xFFu;
                    sqp_select_part(seq_last_part);
                }
                plat_unlock(f);
                seq_song_step = 0;
            }
            ui.page = PG_SEQ;
            ui.redraw = 1;
        } else if (k == B_EDIT) {            /* EDIT: the points, the loop, the tuning, the keys, CLEAR, round */
            ui.page = ui.page >= PG_POINTS && ui.page < PG_KEYS ? (uint8_t)(ui.page + 1u)
                      : ui.page == PG_KEYS                    ? (uint8_t)PG_CLEAR
                                                              : (uint8_t)PG_POINTS;
            ui.redraw = 1;
        } else if (k == B_ENV) {             /* ENV: the S700's OUT, then its velocity amounts */
            ui.page = ui.page == PG_OUT1 ? PG_OUT2 : PG_OUT1;
            ui.redraw = 1;
        } else if (k == B_LFO) {
            ui.page = PG_LFO;
            ui.redraw = 1;
        } else if (k == B_GLO) {             /* GLO: MIDI, again the MIX, the VOICES, round */
            ui.page = ui.page == PG_MIDI ? PG_MIX : ui.page == PG_MIX ? PG_VOICES : PG_MIDI;
            ui.redraw = 1;
        } else if (k == B_HOME) {
            ui.page = PG_HOME;
            ui.redraw = 1;
        }
    }
    if (btn != ui.btn)
        ui.redraw = 1;
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (ui.page == PG_DRUM) {
            if (k == EN_ALGO) {              /* the HR-16's functions */
                uint32_t was = dr.fn;
                dr_algo(e);
                if (dr.fn != was && ann_on())
                    ann_say(dr.fn ? DF_TXT[dr.fn] : "PATTERN", 0, ANN_NOW);
            } else {
                dr_turn(k, e, btn);
            }
            ui.redraw = 1;
        } else if (ui.page == PG_SEQ) {
            if (k == EN_ALGO) {              /* the MMT-8's functions */
                uint32_t was = ed.fn;
                ed_algo(e);
                if (ed.fn != was && ann_on())
                    ann_say(ed.fn ? F_TXT[ed.fn] : sqp.song_mode ? "SONG" : "PART", 0, ANN_NOW);
            } else if (ed.fn && k != EN_PRESET) {
                ed_turn(k, e, btn);
            } else {
                seq_turn(k, e, btn);
                if (ed.fn)
                    ed_load();               /* another part (or song) */
            }
            ui.redraw = 1;
        } else if (k == EN_PRESET) {
            program_step(e);
        } else if (ui.page == PG_MIDI) {
            midi_turn(k, e);
            ui.redraw = 1;
        } else if (ui.page == PG_MIX && k != EN_SELECT) {   /* (SELECT: the slot, as on the sampler's pages) */
            mix_turn(k, e, &ui.slot);
            ui.redraw = 1;
        } else if (ui.page == PG_VOICES && k != EN_SELECT) {
            voices_turn(k, e);
            ui.redraw = 1;
        } else if (k == EN_SELECT) {         /* SELECT: the slot the sampler's pages edit */
            int32_t sl = (int32_t)ui.slot + e;
            ui.slot = (uint8_t)(sl < 0 ? 0 : sl > 15 ? 15 : sl);
            ui.redraw = 1;
        } else if (k >= EN_K1 && ui.page != PG_HOME &&
                   page_s700_turn(ui.page, ui.slot, s7_current_program(), k - EN_K1, e)) {
            ui.redraw = 1;
        }
    }
    ui.keys = keys;
    ui.btn = btn;
    /* the lights: the held, SAVE while something is not saved, the transport, the page's button */
    lit_btn = btn | (unsaved() ? 1u << B_SAVE : 0u) | (sqp.rec_armed || hrp.rec ? 1u << B_REC : 0u);
    lit_btn |= ui.page == PG_SEQ ? 1u << B_SEQ : ui.page == PG_DRUM ? 1u << B_ARP : ui.page == PG_MIDI || ui.page == PG_MIX || ui.page == PG_VOICES ? 1u << B_GLO : 0u;
    if (sqp.state != SQP_STOPPED || sqp.aux)
        lit_btn |= LED_PLAY_GREEN;
    lit_keys = keys;
    if (tracks) {
        lit_keys = ed.fn ? keys & ~track_key_bits() : 0u;
        seq_track_lights(&lit_keys, &dim_keys);
        if (!ed.fn && (btn >> B_SEL & 1u) && !hrp.mute)
            lit_keys |= 1u << WHITE[SQ_TRACKS];   /* the ninth: the drums on */
    } else if (ui.page == PG_DRUM) {
        dr_key_lights(&lit_keys, &dim_keys);
    }
    plat_play_red(sqp.state == SQP_COUNTIN || sqp.take_state == SQP_TAKE_RUNNING || hrp_recording());
    plat_lights(lit_btn, lit_keys, 0, dim_keys, 1);
}

void app_input_only(void) { input(); }

static void say_slot(const char *a, const char *b, uint32_t slot)
{
    char t[LCD_ROWBUF] = "", n[4];
    str_cat(t, b);
    fmt_uint(n, slot + 1u, 2, '0');
    str_cat(t, n);
    app_say(a, t);
}

/* a sample dump cut short: "DUMP CUT SHORT" / "S10 21000/28752", the words that came of those it should have brought;
 * longer than a message, as it means sending it again */
static void say_cut(uint32_t slot)
{
    char t[LCD_ROWBUF] = "S", n[8];
    fmt_uint(n, slot + 1u, 2, '0');
    str_cat(t, n);
    str_cat(t, " ");
    fmt_uint(n, cut_got[slot], 0, ' ');
    str_cat(t, n);
    str_cat(t, "/");
    fmt_uint(n, cut_want[slot], 0, ' ');
    str_cat(t, n);
    app_say("DUMP CUT SHORT", t);
    ui.msg_until = plat_ms() + 4000u;
}

/* the page under the panel: what the sampler holds */
static void draw_status(void)
{
    char t[32];
    uint32_t i, used = 0, fr = s7mem_free_sectors();
    for (i = 0; i < S7_SLOTS; i++)
        used += (uint32_t)s7mem_has_sample(i);
    cv_begin(240, STATUS_H, C_BLACK);
    cv_text(8, 4, &FONT_XS, "SAMPLES", C_GRAY);
    fmt_uint(t, used, 0, ' ');
    str_cat(t, "/16");
    cv_text(70, 4, &FONT_XS, t, C_HI);
    cv_text(120, 4, &FONT_XS, "FREE", C_GRAY);
    fmt_uint(t, fr * 4u, 0, ' ');
    str_cat(t, "K");
    cv_text(160, 4, &FONT_XS, t, C_HI);
    cv_text(8, 22, &FONT_XS, "MIDI", C_GRAY);
    s7_chan_text(t);
    cv_text(70, 22, &FONT_XS, t, C_HI);
    cv_text(120, 22, &FONT_XS, "CPU", C_GRAY);
    fmt_uint(t, plat_cpu_pct(), 0, ' ');
    str_cat(t, "%");
    cv_text(160, 22, &FONT_XS, t, C_HI);
    /* what the sampler's pages edit: the slot (SELECT), its sample, the program (PRESET) */
    cv_text(8, 40, &FONT_XS, "SLOT", C_GRAY);
    fmt_uint(t, ui.slot + 1u, 2, '0');
    cv_text(70, 40, &FONT_XS, t, C_HI);
    cv_text(120, 40, &FONT_XS, "PROGRAM", C_GRAY);
    fmt_uint(t, s7_current_program() + 1u, 2, '0');
    cv_text(190, 40, &FONT_XS, t, C_HI);
    {
        const char *nm = s7mem_name(ui.slot) ? s7mem_name(ui.slot) : s7mem_has_sample(ui.slot) ? "SAMPLE" : "TEST TONE";
        for (i = 0; nm[i] && i < 28u; i++)  /* (a built-in sample's name: its file's) */
            t[i] = nm[i];
        t[i] = 0;
    }
    cv_text(8, 58, &FONT_XS, t, C_HI);
    if (!s7_slot_in_program(&s7_slot[ui.slot], s7_current_program()))
        cv_text(190, 58, &FONT_XS, "OUT", C_GRAY);
    cv_blit(0, LCD_Y + LCD_H + 10);
}

/* the 16 channel lights, between the LCD and the strip: two over each track (channels 1 and 2 over track 1 .. 15 and
 * 16 over track 8; on the drum page over the first eight pads), each flashing as a message on its channel comes in
 * (MIDI in, the keys) or is played (the sequencer, the drums, MIDI ECHO). Drawn when one changes */
#define CH_FLASH_MS 100u
#define CH_LIGHTS_Y (LCD_Y + LCD_H + 2)
static uint16_t ch_shown;         /* the channels lit on the screen */

static void ch_lights(uint32_t now, int redraw)
{
    static uint32_t until[16];
    uint32_t f = plat_lock(), seen = ch_seen, lit = 0, i;
    ch_seen = 0;
    plat_unlock(f);
    for (i = 0; i < 16u; i++) {
        if (seen >> i & 1u)
            until[i] = now + CH_FLASH_MS;
        if ((int32_t)(until[i] - now) > 0)
            lit |= 1u << i;
    }
    if (lit == ch_shown && !redraw)
        return;
    ch_shown = (uint16_t)lit;
    cv_begin(240, 8, C_BLACK);
    for (i = 0; i < 16u; i++) {   /* round, 5 pixels: over a track's box (6 + 29 a track, 26 wide) a quarter in */
        int32_t x = 6 + (int32_t)(i / 2u) * 29 + (i & 1u ? 17 : 4);
        uint16_t c = lit >> i & 1u ? RGB(240, 60, 40) : RGB(64, 44, 44);
        cv_rect(x + 1, 2, 3, 5, c);
        cv_rect(x, 3, 5, 3, c);
    }
    cv_blit(0, CH_LIGHTS_Y);
}

static void page_home(void)
{
    char t[LCD_ROWBUF], n[8];
    uint32_t prog = s7_current_program(), i, nslots = 0;
    for (i = 0; i < S7_SLOTS; i++)
        nslots += (uint32_t)s7_slot_in_program(&s7_slot[i], prog);
    t[0] = 0;
    str_cat(t, "PROGRAM ");
    fmt_uint(n, prog + 1u, 2, '0');
    str_cat(t, n);
    str_cat(t, " OCT");
    fmt_signed(n, set.octave, 2);
    str_cat(t, n);
    lcd_row(0, t);
    t[0] = 0;
    str_cat(t, "SMP ");
    fmt_uint(n, nslots, 2, '0');
    str_cat(t, n);
    str_cat(t, "  VOICES ");
    fmt_uint(n, s7_voices_active(), 1, ' ');
    str_cat(t, n);
    lcd_row(1, t);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    lcd_clear();
    ui.redraw = 1;
}

/* a transfer's progress on a row: "BYTES 41256  86%", the share once the whole is known */
static void progress_row(char *t, const char *unit, uint32_t n, uint32_t whole)
{
    char k[8];
    t[0] = 0;
    str_cat(t, unit);
    fmt_uint(k, n, 5, ' ');
    str_cat(t, k);
    if (whole) {
        fmt_uint(k, n >= whole ? 100u : n * 100u / whole, 4, ' ');
        str_cat(t, k);
        str_cat(t, "%");
    }
}

void app_frame(void)
{
    static uint32_t last_status, last_panel;
    const sds_dump_t *sd;
    uint32_t now = plat_ms();
    int32_t cursor = -1;
    hrp.step = (uint8_t)(ui.page == PG_DRUM && dr.fn == DF_STEP);   /* (the pads go to its place) */
    input();
    {   /* a song stopped (PLAY, or its end): the step shown is where it stopped, and SEL + PLAY goes on from there */
        static uint8_t song_ran;
        if (song_ran && sqp.state == SQP_STOPPED && sqp.song_mode)
            seq_song_step = sqp.step;
        song_ran = (uint8_t)(sqp.state != SQP_STOPPED && sqp.song_mode);
    }
    s700_midi_service();
    md_service();
    take_service();
    hr_service();
    sd = sds_rx_dump();           /* (once: the USB interrupt may finish it meanwhile) */
    if (mdrx.st == MD_RX_DATA || hdrx.st == MD_RX_DATA || mdtx.on) {   /* an MMT-8 or HR-16 dump in or out */
        char t[LCD_ROWBUF];
        if (mdtx.on) {
            lcd_row(0, mdtx.busy);
            progress_row(t, "BYTES ", mdtx.k * 7u / 8u, mdtx.n);
        } else if (mdrx.st == MD_RX_DATA) {   /* (its whole known once its header has come) */
            uint32_t got = mdrx.n;
            lcd_row(0, "MMT-8 DUMP IN");
            progress_row(t, "BYTES ", got, md_want(mdrx.buf, got));
        } else {
            uint32_t got = hdrx.n;
            lcd_row(0, "HR-16 DUMP IN");
            progress_row(t, "BYTES ", got, hd_want(hdrx.buf, got));
        }
        lcd_row(1, t);
    } else if (sd || tx.on) {   /* a sample dump in or out: its slot, its words so far */
        const sds_dump_t *d = sd;
        char t[LCD_ROWBUF] = "", k[4];
        str_cat(t, d ? "DUMP IN  SLOT " : "DUMP OUT SLOT ");
        fmt_uint(k, (d ? d->slot : tx.slot) + 1u, 2, '0');
        str_cat(t, k);
        lcd_row(0, t);
        if (d)
            progress_row(t, "WORDS ", d->words, d->total);
        else
            progress_row(t, "WORDS ", tx.next * SDS_WORDS_PER_BLOCK < tx.t.n ? tx.next * SDS_WORDS_PER_BLOCK : tx.t.n,
                         tx.t.n);
        lcd_row(1, t);
    } else if (now < ui.msg_until) {
        lcd_row(0, ui.msg[0]);
        lcd_row(1, ui.msg[1]);
    } else if (ui.page == PG_SEQ) {
        seq_draw(ui.btn >> B_SEL & 1u);
        cursor = ed_cursor_row();
    } else if (ui.page == PG_DRUM) {
        dr_draw((int)(ui.btn >> B_SEL & 1u));
    } else if (ui.page == PG_MIDI) {
        page_midi();
    } else if (ui.page == PG_MIX) {
        page_mix(ui.slot);
    } else if (ui.page == PG_VOICES) {
        page_voices();
    } else if (ui.page == PG_HOME) {
        page_home();
    } else if (ui.page == PG_CLEAR) {
        page_clear(ui.slot);
    } else {
        page_s700_draw(ui.page, ui.slot, s7_current_program());
    }
    lcd_cursor(cursor, ed.cur);
    lcd_draw(ui.redraw);
    if (ui.redraw || now - last_status >= (ui.page == PG_DRUM ? 50u : 250u)) {   /* (the pads light as they sound) */
        int panel = ui.redraw || now - last_panel >= 250u;   /* (under the strip: four times a second) */
        last_status = now;
        if (panel)
            last_panel = now;
        if (ui.page == PG_SEQ) {
            seq_status();
            seq_panel();          /* (under it: the part playing and the next) */
        } else if (ui.page == PG_DRUM) {
            dr_status();
            if (panel)
                dr_panel();       /* (the pattern playing and the next) */
        } else {
            draw_status();
            panel_off();
        }
    }
    ch_lights(now, ui.redraw);
    ui.redraw = 0;
    ann_tick(now);
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "program"))
        *got = (int)s7_current_program() + 1;
    else if (!strcmp(what, "voices"))
        *got = (int)s7_voices_active();
    else if (!strcmp(what, "poly"))         /* the voices a note may take (the GLO page's VOICES) */
        *got = (int)s7_poly_now();
    else if (!strncmp(what, "mixlev", 6))   /* mixlevN: slot N's level in the mix */
        *got = set.mix_level[(atoi(what + 6) - 1) & 15];
    else if (!strncmp(what, "mixpan", 6))   /* mixpanN: its pan, -50..50 */
        *got = set.mix_pan[(atoi(what + 6) - 1) & 15];
    else if (!strcmp(what, "drumlev"))      /* the drums' bus */
        *got = set.drum_level;
    else if (!strcmp(what, "s7mode"))       /* the sampler's MIDI mode: 0 OMNI OFF, 1 OMNI ON, 2 MULTI */
        *got = set.s7_mode;
    else if (!strcmp(what, "s7ch"))         /* its basic channel, 1..16 */
        *got = set.s7_channel + 1;
    else if (!strcmp(what, "bendrange"))
        *got = set.bend_range;
    else if (!strcmp(what, "keych"))        /* the channel the keys play it on, 1..16 */
        *got = (int)s7_keys_channel() + 1;
    else if (!strncmp(what, "progof", 6))   /* progofN: the program channel N plays, 1..32 */
        *got = (int)s7_program_of((uint32_t)atoi(what + 6) - 1u) + 1;
    else if (!strcmp(what, "drbeat"))       /* the drums' own loop: the beat it is at (from 1) */
        *got = hrp.tick / SQ_PPQN + 1;
    else if (!strcmp(what, "seqstep"))      /* the song's step playing, or to go on from (from 1) */
        *got = sqp.step + 1;
    else if (!strcmp(what, "drpat"))        /* the drums: their own pattern */
        *got = hrp.pattern;
    else if (!strcmp(what, "dumpcut"))      /* sample dumps cut short on the way (DUMP CUT SHORT) */
        *got = (int)dump_cuts;
    else if (!strcmp(what, "lcdcur"))       /* the LCD's cursor as last drawn: 1 shown, 0 not (or none) */
        *got = lcd_blink;
    else if (!strcmp(what, "chlights"))     /* the channel lights lit: bit 0 channel 1 */
        *got = ch_shown;
    else if (!strcmp(what, "recfilter"))    /* MIDI FILTER: what a take records besides notes (SQP_REC_*) */
        *got = sqp.rec_filter;
    else if (!strcmp(what, "recch"))        /* and its channel: 0 all */
        *got = sqp.rec_ch;
    else if (!strcmp(what, "loop"))         /* LOOP: 0 off, 1 parts, 2 all */
        *got = set.loop;
    else if (!strcmp(what, "loopsong"))     /* a song goes round */
        *got = sqp.loop_song;
    else if (!strcmp(what, "looppart"))     /* a part goes round */
        *got = sqp.loop_part;
    else if (!strcmp(what, "countin"))      /* the count-in's beats */
        *got = sqp.countin_beats;
    else if (!strcmp(what, "clickgrid"))    /* the click's value, clocks (96: a beat) */
        *got = sqp.click_grid;
    else if (!strcmp(what, "clickwhen"))    /* 1 recording, 2 playing, 3 both, 0 never */
        *got = sqp.click_rec | sqp.click_play << 1;
    else if (!strcmp(what, "clicks"))       /* clicks so far */
        *got = (int)click_count;
    else if (!strcmp(what, "drprog"))       /* program change chooses the drums' pattern: 1 on */
        *got = set.dr_prog;
    else if (!strcmp(what, "drnext"))       /* the one queued (-1 none) */
        *got = hrp.next == 0xFFu ? -1 : hrp.next;
    else if (!strcmp(what, "drplaying"))    /* the pattern sounding (a song step's), -1 none */
        *got = hrp.playing == 0xFFu ? -1 : hrp.playing;
    else if (!strcmp(what, "drhits"))       /* the hits in their pattern */
        *got = (int)hr_hits(hrp.pattern);
    else if (!strncmp(what, "drhit", 5) && what[5] >= '0' && what[5] <= '9')   /* drhitN: their pattern's hit N's
                                                                                 * clock (from 1; -1 none) */
        *got = hr_hit_at(hrp.pattern, (uint32_t)atoi(what + 5) - 1u)
                   ? (int)hr_hit_tick(hr_hit_at(hrp.pattern, (uint32_t)atoi(what + 5) - 1u))
                   : -1;
    else if (!strncmp(what, "drhvel", 6))   /* drhvelN: their pattern's hit N's velocity (from 1; -1 none) */
        *got = hr_hit_at(hrp.pattern, (uint32_t)atoi(what + 6) - 1u)
                   ? hr_hit_at(hrp.pattern, (uint32_t)atoi(what + 6) - 1u)[3]
                   : -1;
    else if (!strncmp(what, "drhpad", 6))   /* drhpadN: its pad (from 1; -1 none) */
        *got = hr_hit_at(hrp.pattern, (uint32_t)atoi(what + 6) - 1u)
                   ? hr_hit_at(hrp.pattern, (uint32_t)atoi(what + 6) - 1u)[2] + 1
                   : -1;
    else if (!strncmp(what, "drbeats", 7) && what[7])   /* drbeatsN: pattern N's length */
        *got = (int)hr_beats((uint32_t)atoi(what + 7) % SQ_PATTERNS);
    else if (!strcmp(what, "hr16hdr"))      /* an HR-16's dump header kept */
        *got = hd_has_header();
    else if (!strncmp(what, "drhits", 6))   /* drhitsN: pattern N's */
        *got = (int)hr_hits((uint32_t)atoi(what + 6) % SQ_PATTERNS);
    else if (!strcmp(what, "drbeats"))
        *got = (int)hr_beats(hrp.pattern);
    else if (!strcmp(what, "drrec"))        /* recording armed */
        *got = hrp.rec;
    else if (!strcmp(what, "drrecording"))  /* hits going in now */
        *got = hrp_recording();
    else if (!strcmp(what, "drvoices"))
        *got = (int)hr_voices_active();
    else if (!strcmp(what, "drmute"))
        *got = hrp.mute;
    else if (!strcmp(what, "drch"))         /* the drums' channel, 1..16 */
        *got = set.dr_channel + 1;
    else if (!strcmp(what, "drquant"))      /* HR_GRID's index */
        *got = hrp.quant;
    else if (!strcmp(what, "drswing"))
        *got = hrp.swing;
    else if (!strcmp(what, "drfn"))         /* the drums' function shown: 0 none, 1 STEP EDIT .. 8 MIDI DUMP */
        *got = dr.fn;
    else if (!strcmp(what, "drpad"))        /* the pad a function has (from 1; 17: the whole pattern) */
        *got = dr.pad + 1;
    else if (!strcmp(what, "drfill"))       /* FILL held */
        *got = hrp.fill;
    else if (!strcmp(what, "droff"))        /* OFFSET: its clocks */
        *got = dr.off;
    else if (!strcmp(what, "drstep"))       /* STEP EDIT: the place, clocks */
        *got = dr.st;
    else if (!strncmp(what, "drstep", 6)) { /* drstepn: its hits; drstepi: the one shown (from 1); drsteppad,
                                             * drstepvel: its pad (from 1), velocity (0: none) */
        uint32_t nh;
        const uint8_t *h = dr_step_hit(&nh);
        if (!strcmp(what, "drstepn"))
            *got = (int)nh;
        else if (!strcmp(what, "drstepi"))
            *got = h ? dr.hi + 1 : 0;
        else if (!strcmp(what, "drsteppad"))
            *got = h ? h[2] + 1 : 0;
        else if (!strcmp(what, "drstepvel"))
            *got = h ? h[3] : 0;
        else
            return 0;
    }
    else if (!strcmp(what, "aux"))          /* the drums running alone */
        *got = sqp.aux;
    else if (!strcmp(what, "kit"))          /* the kit's sounds built in */
        *got = (int)hr_kit_sounds();
    else if (!strncmp(what, "padsnd", 6))   /* padsndN: pad N's sound (-1 none) */
        *got = hr_pad[(atoi(what + 6) - 1) & 15].sound == HR_NO_SOUND ? -1 : hr_pad[(atoi(what + 6) - 1) & 15].sound;
    else if (!strncmp(what, "padnote", 7))
        *got = hr_pad[(atoi(what + 7) - 1) & 15].note;
    else if (!strncmp(what, "padtune", 7))
        *got = hr_pad[(atoi(what + 7) - 1) & 15].tune;
    else if (!strncmp(what, "padlevel", 8))
        *got = hr_pad[(atoi(what + 8) - 1) & 15].level;
    else if (!strncmp(what, "padpan", 6))
        *got = hr_pad[(atoi(what + 6) - 1) & 15].pan;
    else if (!strncmp(what, "stdsnd", 6))   /* stdsndN: pad N's sound in the global set-up (-1 none) */
        *got = hr_std[(atoi(what + 6) - 1) & 15].sound == HR_NO_SOUND ? -1 : hr_std[(atoi(what + 6) - 1) & 15].sound;
    else if (!strcmp(what, "drsndp"))       /* the pattern the pads have the sounds of */
        *got = hr_snd.p == 0xFFu ? -1 : hr_snd.p;
    else if (!strcmp(what, "drown"))        /* they are its own */
        *got = hr_snd.own;
    else if (!strcmp(what, "dredited"))     /* its own edited, not stored */
        *got = hr_snd.edited;
    else if (!strncmp(what, "patown", 6))   /* patownN: pattern N keeps its own sounds */
        *got = hr_sounds_own((uint32_t)atoi(what + 6) % SQ_PATTERNS);
    else if (!strncmp(what, "stepdr", 6))   /* stepdrN: the song's step N's drum pattern (-1 none) */
        *got = sq_song_drums(sqp.song, (uint32_t)atoi(what + 6) - 1u) == SQ_NO_DRUMS
                   ? -1
                   : (int)sq_song_drums(sqp.song, (uint32_t)atoi(what + 6) - 1u);
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "free"))
        *got = (int)s7mem_free_sectors();
    else if (!strncmp(what, "words", 5))    /* wordsN: slot N's words (1..16), 0 for the test tone */
        *got = s7mem_has_sample((uint32_t)atoi(what + 5) - 1u) ? (int)s7_slot[(atoi(what + 5) - 1) % 16].words : 0;
    else if (!strncmp(what, "end", 3))      /* endN: slot N's END + 1 */
        *got = (int)s7_word(s7_slot[(atoi(what + 3) - 1) % 16].p, S7_P_SEND);
    else if (!strncmp(what, "mode", 4))     /* modeN: slot N's scan mode */
        *got = s7_slot[(atoi(what + 4) - 1) % 16].p[S7_P_RPMODE];
    else if (!strncmp(what, "root", 4))     /* rootN */
        *got = s7_slot[(atoi(what + 4) - 1) % 16].p[S7_P_RECNP];
    else if (!strncmp(what, "filter", 6))   /* filterN */
        *got = s7_slot[(atoi(what + 6) - 1) % 16].p[S7_P_FLTFRQ];
    else if (!strncmp(what, "keyh", 4))     /* keyhN: slot N's high key in the program */
        *got = s7_slot[(atoi(what + 4) - 1) % 16].p[S7_P_KEYH(s7_current_program())];
    else if (!strncmp(what, "keyl", 4))     /* keylN: its low key */
        *got = s7_slot[(atoi(what + 4) - 1) % 16].p[S7_P_KEYL(s7_current_program())];
    else if (!strncmp(what, "inprog", 6))   /* inprogN: slot N plays in the program */
        *got = s7_slot_in_program(&s7_slot[(atoi(what + 6) - 1) % 16], s7_current_program());
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "slot"))
        *got = ui.slot + 1;
    else if (!strncmp(what, "rom", 3))      /* romN: slot N plays a built-in sample */
        *got = s7mem_is_rom((uint32_t)atoi(what + 3) - 1u);
    else if (!strcmp(what, "unsaved"))
        *got = unsaved();
    else if (!strcmp(what, "seq"))          /* the transport: 0 stopped, 1 count-in, 2 playing */
        *got = sqp.state;
    else if (!strcmp(what, "part"))
        *got = sqp.part;
    else if (!strcmp(what, "beat"))
        *got = (int)sqp_beat();
    else if (!strcmp(what, "beats"))        /* the part's length */
        *got = sq.part[sqp.part].beats;
    else if (!strcmp(what, "tracks"))       /* the part's tracks with data, a bit each */
        *got = (int)sq_part_used(sqp.part);
    else if (!strcmp(what, "mask"))
        *got = sqp.mask;
    else if (!strcmp(what, "armed"))
        *got = sqp.rec_armed;
    else if (!strcmp(what, "rectrack"))
        *got = sqp.rec_track + 1;
    else if (!strcmp(what, "take"))
        *got = sqp.take_state;
    else if (!strcmp(what, "tempo"))
        *got = sqp.tempo;
    else if (!strcmp(what, "steps"))
        *got = (int)sq_song_steps(sqp.song);
    else if (!strcmp(what, "songmode"))
        *got = sqp.song_mode;
    else if (!strcmp(what, "nextpart"))     /* the part queued for the end of the one playing, -1 none */
        *got = sqp.next_part == 0xFFu ? -1 : (int)sqp.next_part;
    else if (!strcmp(what, "fn"))           /* the MMT-8 function shown (ALGO): 0 none, 1 LENGTH .. 9 EDIT */
        *got = ed.fn;
    else if (!strcmp(what, "sel"))          /* the tracks it takes, a bit each */
        *got = ed.sel;
    else if (!strcmp(what, "evi"))          /* EDIT: the event shown (from 1; past the last: the count + 1) */
        *got = ed.ev + 1;
    else if (!strcmp(what, "evn"))          /* EDIT: the track's events */
        *got = (int)sq_events(sqp.part, ed.trk);
    else if (!strncmp(what, "ev", 2) && strncmp(what, "events", 6) && sq_event(sqp.part, ed.trk, ed.ev)) {   /* evkey, evvel, evtick, evdur, evst */
        const uint8_t *e = sq_event(sqp.part, ed.trk, ed.ev);
        if (!strcmp(what, "evkey"))
            *got = e[3];
        else if (!strcmp(what, "evvel"))
            *got = e[4];
        else if (!strcmp(what, "evtick"))
            *got = (int)sq_ev_tick(e);
        else if (!strcmp(what, "evdur"))
            *got = sq_ev_len(e) == SQ_NOTE_LEN ? (int)sq_ev_dur(e) : -1;
        else if (!strcmp(what, "evst"))
            *got = e[2];
        else
            return 0;
    } else if (!strcmp(what, "clockin"))   /* the clock: 0 its own, 1 MIDI's */
        *got = sqp.clock_in;
    else if (!strcmp(what, "clockout"))
        *got = sqp.clock_out;
    else if (!strcmp(what, "exttempo"))     /* MIDI clock in's tempo */
        *got = sqp.ext_tempo;
    else if (!strcmp(what, "mmt8hdr"))      /* an MMT-8's dump header kept */
        *got = md_has_header();
    else if (!strncmp(what, "off", 3))    /* offN: track N's song offset (SONG QUANTIZE), clocks */
        *got = sq.offset[(atoi(what + 3) - 1) & 7];
    else if (!strcmp(what, "songstep"))     /* the step the song pages show (from 1) */
        *got = (int)seq_song_step + 1;
    else if (!strncmp(what, "chan", 4))     /* chanN: track N's channel in the part (0 UNCHANGED) */
        *got = sq.part[sqp.part].chan[(atoi(what + 4) - 1) & 7];
    else if (!strncmp(what, "beats", 5))    /* beatsN: part N's length (N as the LCD shows it, 00..99) */
        *got = sq.part[atoi(what + 5) % SQ_PARTS].beats;
    else if (!strncmp(what, "used", 4))     /* usedN: part N's tracks with data */
        *got = (int)sq_part_used((uint32_t)atoi(what + 4) % SQ_PARTS);
    else if (!strncmp(what, "events", 6))   /* eventsN: track N's events in the part */
        *got = (int)sq_events(sqp.part, (uint32_t)(atoi(what + 6) - 1) & 7u);
    else if (!strncmp(what, "step", 4))     /* stepN: the song's step N's part (from 1) */
        *got = atoi(what + 4) - 1 < (int)sq_song_steps(sqp.song)
                   ? sq_data(SQ_SONG(sqp.song))[SQ_STEP * (uint32_t)(atoi(what + 4) - 1)]
                   : -1;
    else if (!strncmp(what, "name", 4))     /* nameN: the part's name, its character N (from 1) */
        *got = sq.part[sqp.part].name[(atoi(what + 4) - 1) % SQ_NAME];
    else
        return 0;
    return 1;
}
#endif
