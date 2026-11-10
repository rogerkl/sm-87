/* SM-87: an MMT-8 style sequencer, an Akai S700 / X7000 style sampler and an HR-16 style drum machine on the M-VAVE
 * FM-1. The app's glue: the sound (app_render), MIDI in, the panel and the screen. Part of fm1-base's unity build
 * (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. The sampler (s700/), the
 * sequencer (mmt8/) and the drums (hr16/) are compiled apart, at -O2. */
#include "fastmath.h"
#include "s700/s7.h"
#include "s700/s7mem.h"
#include "s700/sds.h"
#include "mmt8/seq.h"
#include "mmt8/player.h"
#include "mmt8/mmtdump.h"
#include "hr16/hr.h"
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
    uint8_t rsv[3];
    /* the drums' (a settings object saved before them is shorter: these take their defaults) */
    uint8_t dr_channel;           /* their MIDI channel, 0..15 (10: 9) */
    uint8_t dr_quant, dr_swing;   /* recording's quantize (hr16/hr.h HR_GRID's index) and swing, % */
    uint8_t dr_kit_n;             /* the kit's sounds when the pads were saved (hr_pads_rekit) */
    hr_pad_t pads[HR_PADS];
} settings_t;
#define SET_V1_LEN 12u            /* (to rsv) */
static settings_t set;
static uint8_t set_dirty;

static void drums_defaults(void)
{
    uint32_t i;
    set.dr_channel = 9;
    set.dr_quant = 3;             /* 1/16 */
    set.dr_swing = 50;
    for (i = 0; i < HR_PADS; i++)
        hr_pad_default(i);
}

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.s7_channel = 0;
    set.bend_range = 2;
    set.octave = 0;
    set.clock_in = 0;
    set.clock_out_off = 0;
    drums_defaults();
}

/* the settings as they load: an older (shorter) object's drums the defaults, the pads as the kit is now */
static void settings_load(void)
{
    int n = plat_store_load(OBJ_SETTINGS, &set, sizeof set);
    uint32_t i;
    if (n < (int)SET_V1_LEN || set.magic != SET_MAGIC || set.s7_channel > 15u || set.bend_range > 12u ||
        set.octave < -2 || set.octave > 2) {
        settings_defaults();
        return;
    }
    if (n < (int)sizeof set || set.dr_channel > 15u || set.dr_quant >= HR_GRIDS || set.dr_swing < 50u ||
        set.dr_swing > HR_SWING_MAX) {
        drums_defaults();
        return;
    }
    for (i = 0; i < HR_PADS; i++)
        hr_pad[i] = set.pads[i];
    hr_pads_rekit(set.dr_kit_n);
}

static int settings_save(void)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++)
        set.pads[i] = hr_pad[i];
    set.dr_kit_n = (uint8_t)hr_kit_sounds();
    return plat_store_save(OBJ_SETTINGS, &set, sizeof set);
}

/* the sample dumps' staging: one taken while the last is written to the flash (s700/sds.h) */
static uint8_t stage[SDS_BUFS][S7_PACKED(S7_MAXWORDS)] __attribute__((section(".pool")));

/* -------------------------------------------------------------------------- events: main loop -> audio --- */
/* the panel plays the sampler from the main loop; the voices live in the audio interrupt. A ring of MIDI-like
 * messages (status | d1 << 8 | d2 << 16) between them, one writer and one reader */
#define EVQ 64u
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

/* one message to the sampler (audio interrupt) */
static void s7_message(uint32_t st, uint32_t d1, uint32_t d2)
{
    switch (st & 0xF0u) {
    case 0x90u:
        s7_note_on(d1, d2);       /* velocity 0: a note off at 64 */
        break;
    case 0x80u:
        s7_note_off(d1, d2);
        break;
    case 0xB0u:
        if (d1 == 1u)
            s7_modwheel(d2);
        else if (d1 == 120u)
            s7_kill();
        else if (d1 == 123u)
            s7_all_off();
        break;
    case 0xC0u:
        s7_program(d1);
        break;
    case 0xD0u:
        s7_pressure(d1);
        break;
    case 0xE0u:
        s7_bend((int32_t)(d1 | d2 << 7) - 8192);
        break;
    default:
        break;
    }
}

static volatile uint32_t midi_in_count, midi_in_last;

/* one message to the drums (audio interrupt): a note plays the pads on it (its off: nothing, they play out) */
static void hr_message(uint32_t st, uint32_t d1, uint32_t d2)
{
    if ((st & 0xF0u) == 0x90u && d2)
        hr_note(d1, d2);
    else if ((st & 0xF0u) == 0xB0u && d1 == 120u)
        hr_kill();
}

/* ------------------------------------------------------------------------------------------ the routing --- */
/* everything played, by the sequencer, the keys or MIDI in, goes here: the sampler on its channel, the drums on
 * theirs, and out over USB (to (TO_USB): where it may go; MIDI in does not go back where it came from) */
static void play_out(uint32_t st, uint32_t d1, uint32_t d2, uint32_t to)
{
    if (st < 0xF0u) {
        if ((st & 0x0Fu) == set.s7_channel)
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
    plat_midi_send(0x09u | (0x90u | ch) << 8 | note << 16 | vel << 24, TO_USB);
    sqp_off_after(HR_OFF_TICKS, 0x80u | ch, note);
}

/* each tick of the clock: the drums; recording on their own, a click on their beats (unless the sequencer's) */
void sqp_on_tick(void)
{
    hrp_tick();
    if (hrp_recording() && sqp.click_rec && hrp.at % SQ_PPQN == 0u &&
        !(sqp.state == SQP_PLAYING && (sqp.take_state == SQP_TAKE_RUNNING || sqp.click_play)))
        sqp_click(hrp.at % (4u * SQ_PPQN) == 0u);
}

/* the click: a short blip, higher on the bar's first beat */
static float click_env, click_ph, click_inc;
void sqp_click(int accent)
{
    click_env = 1.0f;
    click_ph = 0.0f;
    click_inc = (accent ? 2000.0f : 1400.0f) / SM_FS;
}

static void click_render(float *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && click_env > 1e-3f; i++) {
        out[i] += 900.0f * click_env * fm_sin_turns(click_ph);
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
    if ((st & 0xF0u) == 0x90u && d2 && (st & 0x0Fu) == set.dr_channel && hrp_recording()) {
        int32_t pad = hr_pad_of(d1);   /* the drums recording: into the pattern */
        if (pad >= 0)
            hrp_record((uint32_t)pad, d2);
    }
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
            played_in(st, d1, d2, TO_USB & ~plat_midi_in_from());
            midi_in_count++;
            midi_in_last = st;
        } else if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu || st == 0xF2u) {
            sqp_rt_in(st, d1 | d2 << 7, frames_now);   /* MIDI clock in (when the sequencer follows it) */
        }
    }
}

/* ------------------------------------------------------------------------------------------------- sound --- */
static float mix[APP_BLOCK], drl[APP_BLOCK], drr[APP_BLOCK];
static float gain_now;

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, done = 0;
    /* a voice at its loudest (LEVEL 99, velocity 127) peaks at -6 dBFS: room for a few at once */
    float gain = (float)master_q12 / 4096.0f * (8388607.0f * 0.5f / 2048.0f);
    while (evq_r != evq_w) {                 /* the keys: the sampler's channel, or (the pads) the drums' */
        uint32_t e = evq[evq_r % EVQ];
        played_in(e & 0xFFu, e >> 8 & 0xFFu, e >> 16 & 0xFFu, TO_USB);
        evq_r = evq_r + 1u;
    }
    midi_in();
    for (i = 0; i < n; i++)
        mix[i] = drl[i] = drr[i] = 0.0f;
    /* in stretches to the sequencer's next tick, so its events land on their frame */
    while (done < n) {
        uint32_t seg = sqp_frames_to_tick();
        if (seg > n - done)
            seg = n - done;
        if (seg) {
            s7_render(mix + done, seg);
            hr_render(drl + done, drr + done, seg);
            click_render(mix + done, seg);
        }
        done += seg;
        sqp_advance(seg);
    }
    for (i = 0; i < n; i++) {     /* the S700 is mono, the drums panned */
        float l, r;
        gain_now += (gain - gain_now) * 0.004f;   /* MASTER glides: no zipper */
        l = (mix[i] + drl[i]) * gain_now;
        r = (mix[i] + drr[i]) * gain_now;
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

/* the drums' recording: its hits into the pattern (or out of it, held to erase) */
static void hr_service(void)
{
    hr_rec_t r;
    int full = 0;
    while (hrp_take(&r)) {
        if (r.erase) {
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
}

/* a take done: made into its track (the sorting outside the lock, the move inside) */
static void take_service(void)
{
    char t[LCD_COLS + 1] = "TRACK ", n[4];
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
    settings_load();
    hrp.quant = set.dr_quant;
    hrp.swing = set.dr_swing;
    s7_init(SM_FS);
    s7_bend_range(set.bend_range);
    s7mem_init();
    sds_init(stage[0], stage[1]);
    sq_load();
    sqp_init(SM_FS);
    sqp.clock_in = set.clock_in ? 1u : 0u;
    sqp.clock_out = set.clock_out_off ? 0u : 1u;
    sqp_select_part(0);
}

/* ------------------------------------------------------------------------------------------- the S700's MIDI --- */
/* the FM-1 answers as an X7000 with 16 slots (s700/sds.h): sample dumps in (byte by byte, the USB interrupt) and out,
 * the parameters (SPRM) and the overall settings (OVS) both ways, on the sampler's channel */
static uint32_t sx_last_ms;       /* the last SysEx of ours: an automatic save waits for quiet */
static uint8_t sx_autosave;       /* parameters came over MIDI: saved once it is quiet */
static int32_t dump_request = -1; /* RSD: the slot to send, from the main loop */
static void say_slot(const char *a, const char *b, uint32_t slot);

static md_rx_t mdrx;              /* an MMT-8 dump coming in (mmt8/mmtdump.h) */
static uint8_t *md_take(uint32_t *max);

void app_sysex_byte(uint8_t b)
{
    sds_rx_byte(b);
    md_rx_byte(&mdrx, b, md_take);
}

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
            uint32_t f = plat_lock();
            sds_apply(d, s7_slot[d->slot].p);
            plat_unlock(f);
            sx_autosave = 1;
            say_slot("RECEIVED", "SLOT ", d->slot);
        }
        d->state = SDS_FREE;
        sx_last_ms = plat_ms();
    }
}

/* a dump going out (RSD), a few blocks a frame: the UI keeps going, and the computer reads as it comes */
#define TX_BLOCKS_A_FRAME 16u
static struct {
    uint8_t on;
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
    tx.on = plat_sysex_put(h, SDS_HEADER) == 0;
    if (!tx.on)
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
            return;
        }
    }
    if (tx.next == tx.blocks) {
        (void)plat_sysex_put(&f7, 1);
        (void)plat_sysex_end();
        tx.on = 0;
    }
    sx_last_ms = plat_ms();
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t ch = set.s7_channel;
    dumps_flush();
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
        s7_ovs_t v = {(uint8_t)ch, 3, (uint8_t)ch, 0, set.bend_range, 0, 5, 127};
        ovs_build(o, ch, &v);
        (void)plat_sysex_send(o, OVS_LEN);
        break;
    }
    case 0x04:                               /* RSPRM: a slot's parameters */
        if (p[4] < S7_SLOTS) {
            static uint8_t o[SPRM_LEN];
            sprm_build(o, ch, p[4], s7_slot[p[4]].p);
            (void)plat_sysex_send(o, SPRM_LEN);
        }
        break;
    case 0x05:
    case 0x06:                               /* SECRE / SECRD: sample dumps taken or not */
        sds_enabled = p[2] == 0x05u;
        break;
    case 0x08: {                             /* OVS: the channel and the pitch wheel's range */
        s7_ovs_t v = {(uint8_t)ch, 3, (uint8_t)ch, 0, set.bend_range, 0, 5, 127};
        if (ovs_parse(p, n, &v) == 0 && v.channel < 16u && v.bend_range <= 12u) {
            set.s7_channel = v.channel;
            set.bend_range = v.bend_range;
            s7_bend_range(v.bend_range);
            set_dirty = 1;
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
        }
        break;
    }
    default:
        break;
    }
    return 1;
}

/* the main loop's part of the S700's MIDI: dumps into the flash, a dump out, the save once the transfer is over */
static void s700_midi_service(void)
{
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

/* ------------------------------------------------------------------------------------------- the MMT-8's MIDI --- */
/* its memory dump (mmt8/mmtdump.h), in and out over USB, in the sample dumps' staging buffers (98 KB, lent while
 * neither holds a dump): one coming in replaces the sequencer's memory; MIDI DUMP (ALGO on the SEQ page) sends ours */
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
} mdtx;

#define MD_TX_A_FRAME 2048u       /* MIDI bytes a frame */

/* MIDI DUMP: ours, as an MMT-8's, out over USB */
static void md_send(void)
{
    static const uint8_t PRE[MD_PREFIX] = {0xF0, 0x00, 0x00, 0x0E, 0x00};
    int32_t n;
    uint32_t f;
    int ok;
    if (mdtx.on || tx.on) {
        app_say("SENDING ALREADY", "");
        return;
    }
    f = plat_lock();
    ok = mdrx.st != MD_RX_DATA && mdrx.st != MD_RX_DONE && sds_hold();
    plat_unlock(f);
    if (!ok) {
        app_say("BUSY", "A DUMP COMING IN");
        return;
    }
    n = md_export(&stage[0][0], MD_MAX);
    if (n < 0) {
        sds_release();
        app_say("TOO BIG", "FOR AN MMT-8");
        return;
    }
    if (plat_sysex_begin() != 0 || plat_sysex_put(PRE, MD_PREFIX) != 0) {
        (void)plat_sysex_end();
        sds_release();
        app_say("USB NOT READY", "");
        return;
    }
    mdtx.on = 1;
    mdtx.n = (uint32_t)n;
    mdtx.k = 0;
    mdtx.m = md_packed_len((uint32_t)n);
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
        char t[LCD_COLS + 1] = "", n[8];
        (void)plat_sysex_put(&f7, 1);
        (void)plat_sysex_end();
        mdtx.on = 0;
        sds_release();
        fmt_uint(n, mdtx.n, 0, ' ');
        str_cat(t, n);
        str_cat(t, " BYTES");
        app_say("MMT-8 DUMP SENT", t);
    }
}

/* ------------------------------------------------------------------------------------------------------ UI --- */
#include "ui/pages_seq.c"
#include "ui/pages_drum.c"
#define PG_SEQ PG_N               /* the sequencer's page, after the sampler's */
#define PG_DRUM (PG_N + 1u)       /* the drums' */

/* a dump in: the sequencer stopped, its memory replaced */
static void md_service(void)
{
    if (mdrx.st == MD_RX_DONE) {
        md_report_t r;
        uint32_t f;
        if (mdrx.over || mdrx.n < MD_HDR) {
            app_say("MMT-8 DUMP", mdrx.over ? "NOT TAKEN" : "TOO SHORT");
        } else {
            char t[LCD_COLS + 1] = "", n[8];
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
            if (r.lost_parts) {   /* the sequencer full: from this part on not taken */
                fmt_uint(n, (uint32_t)r.first_lost, 2, '0');
                str_cat(t, "FROM PART ");
                str_cat(t, n);
                app_say("MEMORY FULL", t);
            } else {
                fmt_uint(n, r.parts, 0, ' ');
                str_cat(t, n);
                str_cat(t, " PTS ");
                fmt_uint(n, r.songs, 0, ' ');
                str_cat(t, n);
                str_cat(t, " SONGS");
                app_say("MMT-8 DUMP IN", t);
            }
        }
        sds_release();
        mdrx.st = MD_RX_IDLE;
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
        if (sqp.state != SQP_STOPPED || sqp.aux)
            sqp_stop();
        else if (ui.page == PG_DRUM)        /* the drums alone (SEL: on from their beat) */
            sqp_aux_play((int)(btn >> B_SEL & 1u));
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
            if (t < SQ_TRACKS)
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

/* a track key on the sequencer's page: SEL held, its mute (and the song step's); REC held, the record track */
static void track_key(uint32_t t, uint32_t btn)
{
    uint32_t f;
    if (btn >> B_REC & 1u) {
        f = plat_lock();
        sqp.rec_track = (uint8_t)t;
        sqp.rec_armed = 1;
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
static void seq_turn(uint32_t enc, int32_t e)
{
    uint32_t f;
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
            ui.rec_manual = 0;               /* another part: its lowest empty track again */
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
    } else if (enc == EN_K4 && sqp.state == SQP_STOPPED) {   /* the song step's drum pattern: none, 00..99 */
        if (seq_song_step < sq_song_steps(sqp.song)) {
            uint32_t d = sq_song_drums(sqp.song, seq_song_step);
            int32_t v = clamp((d >= SQ_PATTERNS ? -1 : (int32_t)d) + e, -1, (int32_t)SQ_PATTERNS - 1);
            sq_song_set_drums(sqp.song, seq_song_step, v < 0 ? SQ_NO_DRUMS : (uint32_t)v);
            sq_dirty = 1;
        }
    } else if (enc == EN_K2) {               /* the record track's channel: UNCHANGED, 1..16 */
        int32_t c = (int32_t)sq.part[sqp.part].chan[sqp.rec_track] + e;
        sq.part[sqp.part].chan[sqp.rec_track] = (uint8_t)(c < 0 ? 0 : c > 16 ? 16 : c);
        sq_dirty = 1;
    } else if (sqp.song_mode && sqp.state == SQP_STOPPED && enc == EN_SELECT) {   /* the step to edit */
        int32_t st = (int32_t)seq_song_step + e, n = (int32_t)sq_song_steps(sqp.song);
        seq_song_step = (uint32_t)(st < 0 ? 0 : st > n ? n : st);
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
                ui.key_ch[k] = set.s7_channel;
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
        if (k == B_OCTDN || k == B_OCTUP) {
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
        } else if (k == B_EDIT) {            /* EDIT: the points, the loop, the tuning, the keys, round */
            ui.page = ui.page >= PG_POINTS && ui.page < PG_KEYS ? (uint8_t)(ui.page + 1u) : PG_POINTS;
            ui.redraw = 1;
        } else if (k == B_ENV) {             /* ENV: the S700's OUT, then its velocity amounts */
            ui.page = ui.page == PG_OUT1 ? PG_OUT2 : PG_OUT1;
            ui.redraw = 1;
        } else if (k == B_LFO) {
            ui.page = PG_LFO;
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
                seq_turn(k, e);
                if (ed.fn)
                    ed_load();               /* another part (or song) */
            }
            ui.redraw = 1;
        } else if (k == EN_PRESET) {
            program_step(e);
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
    lit_btn |= ui.page == PG_SEQ ? 1u << B_SEQ : ui.page == PG_DRUM ? 1u << B_ARP : 0u;
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
    char t[LCD_COLS + 1] = "", n[4];
    str_cat(t, b);
    fmt_uint(n, slot + 1u, 2, '0');
    str_cat(t, n);
    app_say(a, t);
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
    cv_text(8, 22, &FONT_XS, "MIDI CH", C_GRAY);
    fmt_uint(t, set.s7_channel + 1u, 0, ' ');
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

static void page_home(void)
{
    char t[LCD_COLS + 1], n[8];
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

void app_frame(void)
{
    static uint32_t last_status;
    uint32_t now = plat_ms();
    int32_t cursor = -1;
    input();
    s700_midi_service();
    md_service();
    take_service();
    hr_service();
    if (mdrx.st == MD_RX_DATA || mdtx.on) {  /* an MMT-8 dump coming in or going out: its bytes */
        char t[LCD_COLS + 1] = "BYTES ", n[8];
        fmt_uint(n, mdtx.on ? mdtx.k * 7u / 8u : mdrx.n, 5, ' ');
        str_cat(t, n);
        lcd_row(0, mdtx.on ? "SENDING MMT-8" : "MMT-8 DUMP IN");
        lcd_row(1, t);
    } else if (sds_rx_progress()) {          /* a dump coming in: its progress, as the sampler shows it */
        char t[LCD_COLS + 1] = "WORDS ", n[8];
        fmt_uint(n, sds_rx_progress(), 5, ' ');
        str_cat(t, n);
        lcd_row(0, "RECEIVING DUMP");
        lcd_row(1, t);
    } else if (now < ui.msg_until) {
        lcd_row(0, ui.msg[0]);
        lcd_row(1, ui.msg[1]);
    } else if (ui.page == PG_SEQ) {
        seq_draw(ui.btn >> B_SEL & 1u);
        cursor = ed_cursor_row();
    } else if (ui.page == PG_DRUM) {
        dr_draw((int)(ui.btn >> B_SEL & 1u));
    } else if (ui.page == PG_HOME) {
        page_home();
    } else {
        page_s700_draw(ui.page, ui.slot, s7_current_program());
    }
    lcd_cursor(cursor, ed.cur);
    lcd_draw(ui.redraw);
    if (ui.redraw || now - last_status >= (ui.page == PG_DRUM ? 50u : 250u)) {   /* (the pads light as they sound) */
        last_status = now;
        if (ui.page == PG_SEQ)
            seq_status();
        else if (ui.page == PG_DRUM)
            dr_status();
        else
            draw_status();
    }
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
    else if (!strcmp(what, "drpat"))        /* the drums: their own pattern */
        *got = hrp.pattern;
    else if (!strcmp(what, "drnext"))       /* the one queued (-1 none) */
        *got = hrp.next == 0xFFu ? -1 : hrp.next;
    else if (!strcmp(what, "drplaying"))    /* the pattern sounding (a song step's), -1 none */
        *got = hrp.playing == 0xFFu ? -1 : hrp.playing;
    else if (!strcmp(what, "drhits"))       /* the hits in their pattern */
        *got = (int)hr_hits(hrp.pattern);
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
    else if (!strcmp(what, "drfn"))         /* the drums' function shown: 0 none, 1 LENGTH .. 5 PAD MIDI */
        *got = dr.fn;
    else if (!strcmp(what, "drpad"))        /* the pad a function has (from 1; 17: the whole pattern) */
        *got = dr.pad + 1;
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
