/* SM-87: the MMT-8 playing and recording, see player.h */
#include "player.h"

volatile sqp_t sqp;
sq_raw_t sqp_take[SQP_TAKE_MAX] __attribute__((section(".pool")));

static float fs_now = 44117.647f, frames_per_tick, to_next;
static uint16_t cur[SQ_TRACKS];   /* each track's next event, a byte offset in its blob */

/* In a song each track has a place of its own: the song's, its offset (SONG QUANTIZE, sq.offset) behind it (late)
 * or ahead (early, into the next step's part). Its next event in tcur */
static struct {
    uint16_t wait;                /* clocks before it starts: late, at the song's start */
    uint16_t lead;                /* clocks it plays at once on the first: early at the start (the MMT-8's first beat
                                   * can't be advanced) */
    uint16_t tick;                /* in its step's part */
    uint8_t step, done;           /* done: past the song's end */
} tp[SQ_TRACKS];
static uint16_t tcur[SQ_TRACKS];

/* MIDI clock in: each F8 lets 4 ticks play (96 a beat, 24 clocks); their spacing from the last beat's clocks' */
#define EXT_N 24u
static uint32_t ext_at[EXT_N + 1u], ext_w, ext_n;   /* the last clocks' frames */
static uint32_t ext_budget;       /* ticks the clocks have let play and not yet played */
static float ext_fpt;             /* frames a tick at the clocks' rate */

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

/* the first event of track t in part p at tick or after it: a byte offset */
static uint16_t find_tick(uint32_t p, uint32_t t, uint32_t tick)
{
    uint32_t b = SQ_TRK(p, t), len = sq_len(b), a = 0;
    const uint8_t *d = sq_data(b);
    while (a < len && sq_ev_tick(d + a) < tick)
        a += sq_ev_len(d + a);
    return (uint16_t)a;
}

/* a song step's part (one not there: the first) and its tracks, and the part's clocks */
static uint32_t step_part(uint32_t s, uint8_t *mask)
{
    uint8_t part, m;
    sq_song_step(sqp.song, s, &part, &m);
    if (mask)
        *mask = m;
    return part < SQ_PARTS ? part : 0u;
}

void sqp_resync(void)
{
    uint32_t t;
    for (t = 0; t < SQ_TRACKS; t++) {
        cur[t] = find_tick(sqp.part, t, sqp.tick);
        if (sqp.song_mode && !tp[t].done && tp[t].step < sq_song_steps(sqp.song))
            tcur[t] = find_tick(step_part(tp[t].step, 0), t, tp[t].tick);
    }
}

/* a track's place past its part's end: on in the next step with something in it (empty parts skipped), the clocks
 * past the end carried; past the song's end the first step again (LOOP) or done */
static void tp_settle(uint32_t t)
{
    uint32_t n = sq_song_steps(sqp.song), guard = 0, ticks;
    while (!tp[t].done) {
        ticks = tp[t].step < n ? sq_part_ticks(step_part(tp[t].step, 0)) : 0u;
        if (tp[t].step < n && tp[t].tick < ticks)
            break;
        tp[t].tick = (uint16_t)(tp[t].tick - ticks);
        tcur[t] = 0;
        if (++guard > n + 1u || (tp[t].step + 1u >= n && !sqp.loop)) {
            tp[t].done = 1;      /* (a song of empty parts: done too) */
            break;
        }
        tp[t].step = (uint8_t)(tp[t].step + 1u >= n ? 0u : tp[t].step + 1u);
    }
}

/* every track at the song's start: a late one waits, an early one plays its first clocks at once */
static void tp_start(void)
{
    uint32_t t;
    for (t = 0; t < SQ_TRACKS; t++) {
        int32_t o = sq.offset[t];
        tp[t].step = 0;
        tp[t].tick = 0;
        tp[t].done = !sq_song_steps(sqp.song);
        tp[t].wait = (uint16_t)(o > 0 ? o : 0);
        tp[t].lead = (uint16_t)(o < 0 ? -o : 0);
        tcur[t] = 0;
        tp_settle(t);
    }
}

/* every track where the song is now (CONTINUE): its offset behind or ahead of the song's step and tick */
static void tp_place(void)
{
    uint32_t t;
    for (t = 0; t < SQ_TRACKS; t++) {
        int32_t o = sq.offset[t], tick = (int32_t)sqp.tick, step = sqp.step;
        tp[t].wait = tp[t].lead = 0;
        tp[t].done = 0;
        while (o > 0) {           /* behind: back over the steps before, waiting if it passes the song's start */
            if (tick >= o) {
                tick -= o;
                o = 0;
            } else if (step == 0) {
                tp[t].wait = (uint16_t)(o - tick);
                tick = 0;
                o = 0;
            } else {
                o -= tick;
                step--;
                tick = (int32_t)sq_part_ticks(step_part((uint32_t)step, 0));
            }
        }
        tp[t].step = (uint8_t)step;
        tp[t].tick = (uint16_t)(tick - o);   /* ahead: on, tp_settle carries it into the steps after */
        tp_settle(t);
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

static int running(void) { return sqp.state != SQP_STOPPED || sqp.aux; }

void sqp_play(void)
{
    if (sqp.aux)
        sqp_stop();
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
    to_next = 0.0f;               /* the first tick at once (following MIDI clock: at the next clock) */
    ext_budget = 0;
    sqp.started = 1;
    if (sqp.song_mode)
        tp_start();
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
        sqp.take_state = SQP_TAKE_NONE;   /* stopped in the count-in, or nothing played into an empty part */
        sqp.take_nothing = 1;
    } else {
        sqp.take_beats = beats;
        sqp.take_state = SQP_TAKE_DONE;
    }
    sqp.rec_armed = 0;
}

void sqp_stop(void)
{
    if (sqp.aux) {
        sqp.aux = 0;
        sqp_all_notes_off();
        if (sqp.clock_out)
            sqp_midi_rt(0xFC);
    }
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
    if (sqp.state != SQP_STOPPED || sqp.aux)
        return;
    sqp.tick = sqp.tick / SQ_PPQN * SQ_PPQN;   /* from the start of the beat */
    ext_budget = 0;
    sqp.started = 2;
    if (sqp.song_mode)
        tp_place();
    sqp_resync();
    to_next = 0.0f;
    sqp.state = SQP_PLAYING;
    if (sqp.clock_out) {
        pos = sqp.tick / 24u;     /* the song position: 16ths */
        sqp_emit(0xF2, pos & 0x7Fu, pos >> 7 & 0x7Fu);
        sqp_midi_rt(0xFB);
    }
}

void sqp_aux_play(int cont)
{
    if (running())
        return;
    sqp_all_notes_off();
    sqp.aux = 1;
    sqp.started = cont ? 2u : 1u;
    to_next = 0.0f;
    ext_budget = 0;
    if (sqp.clock_out)
        sqp_midi_rt(cont ? 0xFB : 0xFA);
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

void sqp_off_after(uint32_t ticks, uint32_t st, uint32_t key) { off_add(sqp.clock + ticks, st, key); }

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

/* a song's tracks, each at its own place: its events up to it (an early one's first clocks at once), then a clock on */
static void tracks_play_song(void)
{
    uint32_t t, n;
    for (t = 0; t < SQ_TRACKS; t++) {
        if (tp[t].wait) {
            tp[t].wait--;
            continue;
        }
        for (n = tp[t].lead + 1u, tp[t].lead = 0; n && !tp[t].done; n--) {
            uint8_t mask;
            uint32_t p = step_part(tp[t].step, &mask), b = SQ_TRK(p, t), len = sq_len(b), a = tcur[t];
            uint32_t ch = sq.part[p].chan[t];
            const uint8_t *d = sq_data(b);
            if (tp[t].step == sqp.step)
                mask = sqp.mask;  /* the step playing: its mutes as they are now (SEL + a key) */
            while (a < len && sq_ev_tick(d + a) <= tp[t].tick) {
                if (mask >> t & 1u) {
                    uint32_t st = ch ? (d[a + 2u] & 0xF0u) | (ch - 1u) : d[a + 2u];
                    sqp_emit(st, d[a + 3u], d[a + 4u]);
                    if (sq_ev_len(d + a) == SQ_NOTE_LEN)
                        off_add(sqp.clock + sq_ev_dur(d + a), 0x80u | (st & 0x0Fu), d[a + 3u]);
                }
                a += sq_ev_len(d + a);
            }
            tcur[t] = (uint16_t)a;
            tp[t].tick++;
            tp_settle(t);
        }
    }
}

static void part_end(void)
{
    if (sqp.take_state == SQP_TAKE_RUNNING)   /* punch out at the part's end; the part plays on */
        take_end(sq.part[sqp.take_part].beats);
    if (!sqp.song_mode)           /* (in a song the notes end as they are long: an early track's next ones sound) */
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
    if (sqp.state != SQP_PLAYING) {
        if (!sqp.aux)
            return;
        offs_due();               /* the drums alone */
        sqp_on_tick();
        if (sqp.clock_out && sqp.clock % 4u == 0u)
            sqp_midi_rt(0xF8);
        sqp.clock++;
        return;
    }
    offs_due();
    if (sqp.song_mode)
        tracks_play_song();
    else
        tracks_play();
    sqp_on_tick();
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
    if (!running() || (sqp.clock_in && !ext_budget))
        return 0xFFFFFFFFu;       /* (following MIDI clock: none until the next clock) */
    return to_next <= 0.0f ? 0u : (uint32_t)to_next + ((float)(uint32_t)to_next < to_next ? 1u : 0u);
}

void sqp_advance(uint32_t frames)
{
    if (!running())
        return;
    to_next -= (float)frames;
    while (to_next <= 0.0f && running()) {
        if (sqp.clock_in) {
            if (!ext_budget) {
                to_next = 0.0f;   /* ahead of the clocks: waiting for the next */
                return;
            }
            ext_budget--;
        }
        do_tick();
        to_next += sqp.clock_in ? ext_fpt : frames_per_tick;
        if (sqp.clock_in && ext_budget >= 4u)
            to_next = 0.0f;       /* a clock's first tick still to play: behind the clocks, caught up at once */
    }
}

/* the song position (F2): ticks from the song's (the part's) start, stopped */
static void ext_locate(uint32_t ticks)
{
    if (sqp.song_mode) {
        uint32_t n = sq_song_steps(sqp.song), s;
        for (s = 0; s < n; s++) {
            uint32_t t = sq_part_ticks(step_part(s, 0));
            if (ticks < t)
                break;
            ticks -= t;
        }
        if (s >= n)
            s = 0, ticks = 0;     /* past the end: the start */
        sqp.step = (uint8_t)s;
        if (n)
            load_step();
        sqp.tick = ticks;
    } else {
        uint32_t t = sq_part_ticks(sqp.part);
        sqp.tick = t ? ticks % t : 0u;
    }
    sqp_resync();
}

static void ext_clock(uint32_t frame)
{
    uint32_t i;
    if (ext_n && frame - ext_at[(ext_w - 1u) % (EXT_N + 1u)] > (uint32_t)fs_now)
        ext_n = 0;                /* a second without one: measured again */
    ext_at[ext_w++ % (EXT_N + 1u)] = frame;
    if (ext_n < EXT_N + 1u)
        ext_n++;
    if (ext_n >= 2u) {
        i = (ext_w - ext_n) % (EXT_N + 1u);
        if (frame != ext_at[i]) {
            float per = (float)(frame - ext_at[i]) / (float)(ext_n - 1u);   /* frames a clock */
            ext_fpt = per / 4.0f;
            sqp.ext_tempo = (uint16_t)(fs_now * 60.0f / (per * 24.0f) + 0.5f);
        }
    } else if (ext_fpt <= 0.0f) {
        ext_fpt = frames_per_tick;   /* (the first: the tempo's, until there are two) */
    }
    if (running()) {
        to_next = 0.0f;           /* the clock's first tick on it (any owed before it first) */
        ext_budget += 4u;
    }
}

void sqp_rt_in(uint32_t st, uint32_t d, uint32_t frame)
{
    if (!sqp.clock_in)
        return;
    switch (st) {
    case 0xF8:
        ext_clock(frame);
        break;
    case 0xFA:                    /* start: from the top, at the next clock */
        if (running())
            sqp_stop();
        sqp_play();
        ext_budget = 0;
        break;
    case 0xFB:                    /* continue: from where it is (stopped, or the song position) */
        if (!running()) {
            if (sqp.song_mode)
                tp_place();
            sqp_resync();
            ext_budget = 0;
            to_next = 0.0f;
            sqp.started = 2;
            sqp.state = SQP_PLAYING;
            if (sqp.clock_out)
                sqp_midi_rt(0xFB);
        }
        break;
    case 0xFC:
        sqp_stop();
        break;
    case 0xF2:
        if (sqp.state == SQP_STOPPED)
            ext_locate(d * 24u);  /* 16ths: 24 ticks */
        break;
    default:
        break;
    }
}

void sqp_input(uint32_t st, uint32_t d1, uint32_t d2)
{
    sq_raw_t *r;
    uint32_t kind = st & 0xF0u, tick;
    if (sqp.take_state != SQP_TAKE_RUNNING || st >= 0xF0u || kind == 0xA0u)
        return;                   /* (poly pressure: the MMT-8 does not record it) */
    if (sqp.state == SQP_COUNTIN && sqp.countin <= (int32_t)(SQ_PPQN / 4u))
        tick = 0;                 /* in the count-in's last sixteenth: a little early, on the one */
    else if (sqp.state != SQP_PLAYING)
        return;
    else                          /* between two ticks: the nearer one (the tick just played, or the next) */
        tick = sqp.tick && to_next > 0.5f * frames_per_tick ? sqp.tick - 1u : sqp.tick;
    if (sqp.take_n >= SQP_TAKE_MAX) {
        sqp.take_full = 1;
        return;
    }
    r = &sqp_take[sqp.take_n];
    r->tick = tick;
    r->st = (uint8_t)st;
    r->d1 = (uint8_t)d1;
    r->d2 = (uint8_t)d2;
    sqp.take_n = sqp.take_n + 1u;
}

uint32_t sqp_beat(void) { return sqp.state == SQP_COUNTIN ? 0u : sqp.tick / SQ_PPQN + 1u; }

int sqp_nearer_last(void)
{
    return to_next > 0.5f * (sqp.clock_in ? ext_fpt : frames_per_tick);
}
