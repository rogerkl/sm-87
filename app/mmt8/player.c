/* SM-87: the MMT-8 playing and recording, see player.h */
#include "player.h"

volatile sqp_t sqp;
sq_raw_t sqp_take[SQP_TAKE_MAX] __attribute__((section(".pool")));

static float fs_now = 44117.647f, frames_per_tick, to_next;
static uint16_t cur[SQ_TRACKS];   /* each track's next event, a byte offset in its blob */

#define OFFS 64u
static struct {
    uint32_t at;                  /* the clock it is due at */
    uint8_t st, key;
} offs[OFFS];
static uint32_t n_offs;

void sqp_init(float fs)
{
    fs_now = fs;
    sqp.state = SQP_STOPPED;
    sqp.next_part = 0xFFu;
    sqp.mask = 0xFFu;
    sqp.click_rec = 1;
    sqp.clock_out = 1;
    sqp.take_state = SQP_TAKE_NONE;
    sqp_set_tempo(120);
}

void sqp_set_tempo(uint32_t bpm)
{
    if (bpm < 30u)
        bpm = 30u;
    if (bpm > 250u)
        bpm = 250u;
    sqp.tempo = (uint16_t)bpm;
    frames_per_tick = fs_now * 60.0f / ((float)bpm * (float)SQ_PPQN);
}

void sqp_all_notes_off(void)
{
    uint32_t i;
    for (i = 0; i < n_offs; i++)
        sqp_emit(offs[i].st, offs[i].key, 64u);
    n_offs = 0;
}

void sqp_resync(void)
{
    uint32_t t;
    for (t = 0; t < SQ_TRACKS; t++) {
        uint32_t b = SQ_TRK(sqp.part, t), len = sq_len(b), a = 0;
        const uint8_t *d = sq_data(b);
        while (a < len && sq_ev_tick(d + a) < sqp.tick)
            a += sq_ev_len(d + a);
        cur[t] = (uint16_t)a;
    }
}

static void load_step(void)
{
    uint8_t part, mask;
    sq_song_step(sqp.song, sqp.step, &part, &mask);
    sqp.part = part < SQ_PARTS ? part : 0u;
    sqp.mask = mask;
}

void sqp_select_part(uint32_t p)
{
    if (p >= SQ_PARTS)
        return;
    if (sqp.state == SQP_STOPPED) {
        sqp.part = (uint8_t)p;
        sqp.tick = 0;
        sqp_resync();
    } else {
        sqp.next_part = (uint8_t)p;
    }
}

void sqp_select_song(uint32_t s)
{
    if (s < SQ_SONGS && sqp.state == SQP_STOPPED) {
        sqp.song = (uint8_t)s;
        sqp.step = 0;
        sqp_set_tempo(sq.song[s].tempo);
        if (sq_song_steps(s))
            load_step();
        sqp.tick = 0;
        sqp_resync();
    }
}

void sqp_play(void)
{
    sqp_all_notes_off();
    if (sqp.song_mode) {
        if (!sq_song_steps(sqp.song))
            return;
        sqp.step = 0;
        sqp_set_tempo(sq.song[sqp.song].tempo);
        load_step();
    }
    sqp.tick = 0;
    sqp.clock = 0;
    sqp.next_part = 0xFFu;
    to_next = 0.0f;               /* the first tick at once */
    sqp_resync();
    if (sqp.rec_armed && !sqp.song_mode && sqp.take_state != SQP_TAKE_DONE) {
        sqp.take_n = 0;
        sqp.take_full = 0;
        sqp.take_part = sqp.part;
        sqp.take_track = sqp.rec_track;
        sqp.take_state = SQP_TAKE_RUNNING;
        sqp.countin = 4 * (int32_t)SQ_PPQN;   /* a bar of clicks (the MMT-8's default count-in, 4) */
        sqp.state = SQP_COUNTIN;
    } else {
        sqp.state = SQP_PLAYING;
    }
    if (sqp.clock_out)
        sqp_midi_rt(0xFA);
}

/* a take ends: the main loop makes it a track (app.c) */
static void take_end(uint32_t beats)
{
    if (sqp.take_state != SQP_TAKE_RUNNING)
        return;
    if (!beats || (!sq.part[sqp.take_part].beats && !sqp.take_n)) {
        sqp.take_state = SQP_TAKE_NONE;   /* nothing played into an empty part: nothing to keep */
    } else {
        sqp.take_beats = beats;
        sqp.take_state = SQP_TAKE_DONE;
    }
    sqp.rec_armed = 0;
}

void sqp_stop(void)
{
    if (sqp.state == SQP_STOPPED)
        return;
    if (sqp.take_state == SQP_TAKE_RUNNING) {
        uint32_t beats = sq.part[sqp.take_part].beats;
        if (!beats)               /* a first take: its length, to the nearest beat */
            beats = (sqp.tick + SQ_PPQN / 2u) / SQ_PPQN;
        take_end(sqp.state == SQP_PLAYING ? beats : 0u);
    }
    sqp.state = SQP_STOPPED;
    sqp_all_notes_off();
    if (sqp.clock_out)
        sqp_midi_rt(0xFC);
}

void sqp_punch(void)
{
    if (sqp.take_state != SQP_TAKE_RUNNING)
        return;
    if (!sq.part[sqp.take_part].beats || sqp.state != SQP_PLAYING)
        sqp_stop();
    else
        take_end(sq.part[sqp.take_part].beats);
}

void sqp_continue(void)
{
    uint32_t pos;
    if (sqp.state != SQP_STOPPED)
        return;
    sqp.tick = sqp.tick / SQ_PPQN * SQ_PPQN;   /* from the start of the beat */
    sqp_resync();
    to_next = 0.0f;
    sqp.state = SQP_PLAYING;
    if (sqp.clock_out) {
        pos = sqp.tick / 24u;     /* the song position: 16ths */
        sqp_emit(0xF2, pos & 0x7Fu, pos >> 7 & 0x7Fu);
        sqp_midi_rt(0xFB);
    }
}

static void off_add(uint32_t at, uint32_t st, uint32_t key)
{
    if (n_offs == OFFS) {         /* full: the first one's off now */
        uint32_t i;
        sqp_emit(offs[0].st, offs[0].key, 64u);
        for (i = 1; i < OFFS; i++)
            offs[i - 1u] = offs[i];
        n_offs--;
    }
    offs[n_offs].at = at;
    offs[n_offs].st = (uint8_t)st;
    offs[n_offs].key = (uint8_t)key;
    n_offs++;
}

static void offs_due(void)
{
    uint32_t i = 0;
    while (i < n_offs) {
        if ((int32_t)(sqp.clock - offs[i].at) >= 0) {
            sqp_emit(offs[i].st, offs[i].key, 64u);
            offs[i] = offs[--n_offs];
        } else {
            i++;
        }
    }
}

static void tracks_play(void)
{
    uint32_t t, p = sqp.part;
    for (t = 0; t < SQ_TRACKS; t++) {
        uint32_t b = SQ_TRK(p, t), len = sq_len(b), a = cur[t], ch = sq.part[p].chan[t];
        const uint8_t *d = sq_data(b);
        int play = (sqp.mask >> t & 1u) && !(sqp.take_state == SQP_TAKE_RUNNING && t == sqp.take_track);
        while (a < len && sq_ev_tick(d + a) <= sqp.tick) {
            uint32_t st = d[a + 2u];
            if (ch)
                st = (st & 0xF0u) | (ch - 1u);
            if (play && sq_ev_tick(d + a) == sqp.tick) {
                sqp_emit(st, d[a + 3u], d[a + 4u]);
                if (sq_ev_len(d + a) == SQ_NOTE_LEN)
                    off_add(sqp.clock + sq_ev_dur(d + a), 0x80u | (st & 0x0Fu), d[a + 3u]);
            }
            a += sq_ev_len(d + a);
        }
        cur[t] = (uint16_t)a;
    }
}

static void part_end(void)
{
    if (sqp.take_state == SQP_TAKE_RUNNING)   /* punch out at the part's end; the part plays on */
        take_end(sq.part[sqp.take_part].beats);
    sqp_all_notes_off();
    if (sqp.song_mode) {
        if (++sqp.step >= sq_song_steps(sqp.song)) {
            if (!sqp.loop) {
                sqp.step = 0;
                sqp_stop();
                load_step();
                sqp.tick = 0;
                sqp_resync();
                return;
            }
            sqp.step = 0;
        }
        load_step();
    } else if (sqp.next_part != 0xFFu) {
        sqp.part = sqp.next_part;
        sqp.next_part = 0xFFu;
    }
    sqp.tick = 0;
    sqp_resync();
}

static void do_tick(void)
{
    uint32_t ticks;
    if (sqp.state == SQP_COUNTIN) {
        if (sqp.countin % (int32_t)SQ_PPQN == 0)
            sqp_click(sqp.countin == 4 * (int32_t)SQ_PPQN);
        if (--sqp.countin > 0)
            return;
        sqp.state = SQP_PLAYING;
        sqp.tick = 0;
        sqp.clock = 0;
        sqp_resync();
        return;
    }
    if (sqp.state != SQP_PLAYING)
        return;
    offs_due();
    tracks_play();
    if (sqp.tick % SQ_PPQN == 0u &&
        ((sqp.take_state == SQP_TAKE_RUNNING && sqp.click_rec) || sqp.click_play))
        sqp_click(sqp.tick % (4u * SQ_PPQN) == 0u);
    if (sqp.clock_out && sqp.clock % 4u == 0u)
        sqp_midi_rt(0xF8);
    sqp.tick++;
    sqp.clock++;
    ticks = sq_part_ticks(sqp.part);
    if (!ticks && sqp.song_mode)
        part_end();               /* an empty part in a song: nothing to play, on to the next step */
    else if (!ticks && sqp.tick >= SQ_MAX_BEATS * SQ_PPQN)
        sqp_stop();               /* a first take as long as a part can be */
    else if (ticks && sqp.tick >= ticks)
        part_end();
}

uint32_t sqp_frames_to_tick(void)
{
    if (sqp.state == SQP_STOPPED)
        return 0xFFFFFFFFu;
    return to_next <= 0.0f ? 0u : (uint32_t)to_next + ((float)(uint32_t)to_next < to_next ? 1u : 0u);
}

void sqp_advance(uint32_t frames)
{
    if (sqp.state == SQP_STOPPED)
        return;
    to_next -= (float)frames;
    while (to_next <= 0.0f && sqp.state != SQP_STOPPED) {
        do_tick();
        to_next += frames_per_tick;
    }
}

void sqp_input(uint32_t st, uint32_t d1, uint32_t d2)
{
    sq_raw_t *r;
    uint32_t kind = st & 0xF0u;
    if (sqp.take_state != SQP_TAKE_RUNNING || sqp.state != SQP_PLAYING || st >= 0xF0u || kind == 0xA0u)
        return;                   /* (poly pressure: the MMT-8 does not record it) */
    if (sqp.take_n >= SQP_TAKE_MAX) {
        sqp.take_full = 1;
        return;
    }
    r = &sqp_take[sqp.take_n];
    /* between two ticks: the nearer one (the tick just played, or the next) */
    r->tick = sqp.tick && to_next > 0.5f * frames_per_tick ? sqp.tick - 1u : sqp.tick;
    r->st = (uint8_t)st;
    r->d1 = (uint8_t)d1;
    r->d2 = (uint8_t)d2;
    sqp.take_n = sqp.take_n + 1u;
}

uint32_t sqp_beat(void) { return sqp.state == SQP_COUNTIN ? 0u : sqp.tick / SQ_PPQN + 1u; }
