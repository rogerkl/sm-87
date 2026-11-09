/* SM-87: the MMT-8's functions on the panel (documentation/MMT-8_Manual.pdf, 4-16). On the sequencer's page ALGO
 * turns from the part (or song) playing through them, as the MMT-8's buttons: LENGTH, ERASE, COPY, MERGE, QUANTIZE,
 * TRANSPOSE, MIDI CHANNEL, NAME and EDIT (in SONG: ERASE, COPY, NAME, EDIT). The first eight white keys are the
 * track buttons, K1..K4 the function's values (the MMT-8's +/- and page buttons), REC does it (RECORD, with the
 * function held). SEQ goes back to the part playing. Part of the unity build (app.c, after ui/pages_seq.c).
 *
 * As the MMT-8's: a function chooses all eight tracks until a track key chooses one, then the keys add and take
 * away; COPY and MERGE take a track from (lit) and a track to (flashing); nothing happens until REC, and LENGTH,
 * ERASE, COPY, MERGE, QUANTIZE and TRANSPOSE wait for the sequencer to stop. */

enum { F_PLAY, F_LENGTH, F_ERASE, F_COPY, F_MERGE, F_QUANT, F_TRANSP, F_CHAN, F_NAME, F_EDIT, F_N };
#define F_IN_SONG (1u << F_PLAY | 1u << F_ERASE | 1u << F_COPY | 1u << F_NAME | 1u << F_EDIT)
static const char *const F_TXT[F_N] = {"",         "LENGTH",    "ERASE",        "COPY", "MERGE",
                                       "QUANTIZE", "TRANSPOSE", "MIDI CHANNEL", "NAME", "EDIT"};

static const char *const FILTER_TXT[SQ_F_CHAN] = {"ALL EVENTS",  "NOTES ONLY",      "PITCH BEND",
                                                  "CONTROLLERS", "PROGRAM CHANGES", "AFTER TOUCH"};
static const uint8_t GRID[10] = {192, 96, 64, 48, 32, 24, 16, 12, 8, 6};   /* 1/2 .. 1/64, in clocks */
static const char *const GRID_TXT[10] = {"1/2", "1/4", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/64"};
static const char *const QMODE_TXT[4] = {"NOTE START", "START & END", "NOTE END", "KEEP DURATION"};
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+/&.#'!?";
static const uint8_t ADD_ST[5] = {0x90, 0xC0, 0xB0, 0xE0, 0xD0};   /* EDIT: what REC adds at the end */
static const char *const ADD_TXT[5] = {"NOTE", "PROG", "CTRL", "BEND", "TOUCH"};

static struct {
    uint8_t fn;
    uint8_t sel, fresh;           /* ERASE, QUANTIZE, TRANSPOSE: the tracks; fresh: all eight, no key yet */
    int8_t src, dst;              /* COPY: the track from (-1: the whole part), to (-1: the same); MERGE: from, to */
    uint8_t trk;                  /* MIDI CHANNEL, EDIT: the track */
    uint8_t to;                   /* COPY: the part or song to */
    uint8_t filter, grid, qmode, top, add;
    int8_t semis;
    uint16_t beats;               /* LENGTH: what REC sets (0: none yet) */
    uint16_t ev;                  /* EDIT: the event shown; the count: the track's end, where REC adds */
    uint16_t add_tick;            /* EDIT, at the end: where REC adds */
    uint8_t cur;                  /* NAME: the cursor */
    char name[SQ_NAME];
} ed = {.grid = 5};

static int32_t ed_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the row: a at the left, b at the right */
static void ed_lr(char *t, const char *a, const char *b)
{
    uint32_t i, la = 0, lb = 0;
    while (a[la])
        la++;
    while (b[lb])
        lb++;
    for (i = 0; i < LCD_COLS; i++)
        t[i] = i < la ? a[i] : ' ';
    for (i = 0; i < lb && lb <= LCD_COLS; i++)
        t[LCD_COLS - lb + i] = b[i];
    t[LCD_COLS] = 0;
}

static void ed_num(char *t, const char *pre, uint32_t v, uint32_t w)
{
    char n[8];
    t[0] = 0;
    str_cat(t, pre);
    fmt_uint(n, v, w, '0');
    str_cat(t, n);
}

/* a place in the part, as the MMT-8 shows it: beat (from 1) and clock in the beat */
static void ed_pos(char *t, uint32_t tick, uint32_t from)
{
    char n[8];
    fmt_uint(t, tick / SQ_PPQN + from, 3, '0');
    str_cat(t, ".");
    fmt_uint(n, tick % SQ_PPQN, 2, '0');
    str_cat(t, n);
}

/* the part's (or song's) values a function starts from */
static void ed_load(void)
{
    const char *n = sqp.song_mode ? sq.song[sqp.song].name : sq.part[sqp.part].name;
    uint32_t i;
    for (i = 0; i < SQ_NAME; i++)
        ed.name[i] = n[i];
    ed.beats = sq.part[sqp.part].beats;
    ed.ev = 0;
    ed.add_tick = 0;
}

static void ed_enter(uint32_t fn)
{
    ed.fn = (uint8_t)fn;
    ed.sel = 0xFFu;
    ed.fresh = 1;
    ed.src = ed.dst = -1;
    ed.filter = SQ_F_ALL;         /* (ERASE starts at everything, as the MMT-8's) */
    ed.top = 0;
    ed.to = sqp.song_mode ? sqp.song : sqp.part;
    ed.trk = sqp.rec_track;
    ed.cur = 0;
    ed_load();
}

/* ALGO: the next function (in SONG the song's) */
static void ed_algo(int32_t d)
{
    int32_t f = ed.fn;
    uint32_t ok = sqp.song_mode ? F_IN_SONG : 0xFFFFFFFFu;
    while (d) {
        int32_t s = d > 0 ? 1 : -1, g = f + s;
        while (g >= 0 && g < F_N && !(ok >> g & 1u))
            g += s;
        if (g < 0 || g >= F_N)
            break;
        f = g;
        d -= s;
    }
    if ((uint32_t)f != ed.fn)
        ed_enter((uint32_t)f);
}

/* the song step EDIT shows: its part and tracks, 0 past the end */
static int ed_step(uint8_t *part, uint8_t *mask)
{
    if (seq_song_step >= sq_song_steps(sqp.song))
        return 0;
    sq_song_step(sqp.song, seq_song_step, part, mask);
    return 1;
}

/* the song as the player has it again (its first step's part and tracks), after its steps changed */
static void ed_song_reload(void)
{
    uint32_t f = plat_lock();
    sqp_select_song(sqp.song);
    plat_unlock(f);
}

/* a track key on a function's page */
static void ed_track(uint32_t t)
{
    uint8_t part, mask;
    switch (ed.fn) {
    case F_ERASE:
    case F_QUANT:
    case F_TRANSP:
        if (ed.fresh)
            ed.sel = (uint8_t)(1u << t), ed.fresh = 0;
        else
            ed.sel ^= (uint8_t)(1u << t);
        break;
    case F_COPY:
    case F_MERGE:
        if (sqp.song_mode)
            break;
        if (ed.src < 0)
            ed.src = (int8_t)t;
        else if (ed.src == (int8_t)t)
            ed.src = ed.dst = -1;     /* (COPY: the whole part again) */
        else if (ed.dst == (int8_t)t)
            ed.dst = -1;
        else
            ed.dst = (int8_t)t;
        break;
    case F_CHAN:
        ed.trk = (uint8_t)t;
        break;
    case F_EDIT:
        if (sqp.song_mode) {      /* the step's tracks */
            if (ed_step(&part, &mask)) {
                (void)sq_song_set(sqp.song, seq_song_step, part, (uint8_t)(mask ^ (1u << t)), sq_scratch);
                ed_song_reload();
                sq_dirty = 1;
            }
        } else {
            ed.trk = (uint8_t)t;
            ed.ev = 0;
            ed.add_tick = 0;
        }
        break;
    default:
        break;
    }
}

/* EDIT, a part: SELECT the event, K1 its place (SEL: by beats), K2 the key (a controller's number, a program, the
 * bend coarse; SEL: the channel), K3 the velocity (the value, the bend fine), K4 the duration (SEL: by beats; not a
 * note: the channel). At the end: K1 where REC adds, K2 what */
static void ed_turn_event(uint32_t enc, int32_t d, int sel)
{
    uint32_t p = sqp.part, t = ed.trk, n = sq_events(p, t), ticks = sq_part_ticks(p), st, l, k, tick, dur;
    const uint8_t *e;
    uint8_t ev[SQ_NOTE_LEN];
    int32_t at, b;
    if (enc == EN_SELECT) {
        ed.ev = (uint16_t)ed_clamp((int32_t)ed.ev + d, 0, (int32_t)n);
        return;
    }
    if (ed.ev >= n) {
        if (enc == EN_K1 && ticks)
            ed.add_tick = (uint16_t)ed_clamp((int32_t)ed.add_tick + d * (sel ? (int32_t)SQ_PPQN : 1), 0,
                                             (int32_t)ticks - 1);
        else if (enc == EN_K2)
            ed.add = (uint8_t)ed_clamp((int32_t)ed.add + d, 0, 4);
        return;
    }
    e = sq_event(p, t, ed.ev);
    l = sq_ev_len(e);
    for (k = 0; k < l; k++)
        ev[k] = e[k];
    st = ev[2] & 0xF0u;
    tick = sq_ev_tick(ev);
    dur = l == SQ_NOTE_LEN ? sq_ev_dur(ev) : 0u;
    b = ev[3] | (int32_t)ev[4] << 7;  /* (a bend's value) */
    if (enc == EN_K1) {
        tick = (uint32_t)ed_clamp((int32_t)tick + d * (sel ? (int32_t)SQ_PPQN : 1), 0, (int32_t)ticks - 1);
        if (dur > ticks - tick)
            dur = ticks - tick;
    } else if ((enc == EN_K2 && sel) || (enc == EN_K4 && st != 0x90u)) {
        ev[2] = (uint8_t)(st | (uint32_t)ed_clamp((int32_t)(ev[2] & 0x0Fu) + d, 0, 15));
    } else if (enc == EN_K2) {
        if (st == 0xE0u)
            b = ed_clamp(b + d * 128, 0, 16383);
        else
            ev[3] = (uint8_t)ed_clamp(ev[3] + d, 0, 127);
    } else if (enc == EN_K3) {
        if (st == 0x90u)
            ev[4] = (uint8_t)ed_clamp(ev[4] + d, 1, 127);
        else if (st == 0xE0u)
            b = ed_clamp(b + d, 0, 16383);
        else if (st == 0xB0u || st == 0xA0u)
            ev[4] = (uint8_t)ed_clamp(ev[4] + d, 0, 127);
        else
            return;
    } else if (enc == EN_K4) {
        dur = (uint32_t)ed_clamp((int32_t)dur + d * (sel ? (int32_t)SQ_PPQN : 1), 1, (int32_t)(ticks - tick));
    } else {
        return;
    }
    if (st == 0xE0u) {
        ev[3] = (uint8_t)(b & 0x7F);
        ev[4] = (uint8_t)(b >> 7);
    }
    ev[0] = (uint8_t)tick;
    ev[1] = (uint8_t)(tick >> 8);
    if (l == SQ_NOTE_LEN) {
        ev[5] = (uint8_t)dur;
        ev[6] = (uint8_t)(dur >> 8);
    }
    at = sq_event_put(p, t, ed.ev, ev, sq_scratch);
    if (at < 0)
        app_say("MEMORY FULL", "");
    else
        ed.ev = (uint16_t)at;
    sq_dirty = 1;
}

/* EDIT, a song: SELECT the step, K1 its part (past the end: the part REC adds) */
static void ed_turn_step(uint32_t enc, int32_t d)
{
    uint8_t part, mask;
    if (enc == EN_SELECT) {
        seq_song_step = (uint32_t)ed_clamp((int32_t)seq_song_step + d, 0, (int32_t)sq_song_steps(sqp.song));
    } else if (enc == EN_K1) {
        if (ed_step(&part, &mask)) {
            part = (uint8_t)ed_clamp(part + d, 0, (int32_t)SQ_PARTS - 1);
            (void)sq_song_set(sqp.song, seq_song_step, part, mask, sq_scratch);
            ed_song_reload();
            sq_dirty = 1;
        } else {
            seq_last_part = (uint8_t)ed_clamp(seq_last_part + d, 0, (int32_t)SQ_PARTS - 1);
        }
    }
}

/* an encoder on a function's page (PRESET stays the part or song: seq_turn) */
static void ed_turn(uint32_t enc, int32_t d, uint32_t btn)
{
    int sel = (int)(btn >> B_SEL & 1u), song = sqp.song_mode;
    int32_t v;
    uint32_t i;
    switch (ed.fn) {
    case F_LENGTH:                /* K1 the beats (SEL: tens), K2 at the end or the top */
        if (enc == EN_K1) {
            v = (int32_t)ed.beats + d * (sel ? 10 : 1);
            ed.beats = (uint16_t)(!ed.beats && v < 1 ? 0 : ed_clamp(v, 1, (int32_t)SQ_MAX_BEATS));
        } else if (enc == EN_K2) {
            ed.top = d > 0;
        }
        break;
    case F_ERASE:                 /* K1 what */
        if (enc == EN_K1 && !song)
            ed.filter = (uint8_t)ed_clamp((int32_t)ed.filter + d, 0, SQ_F_N - 1);
        break;
    case F_COPY:                  /* K1 the part (song) to, K2 what */
        if (enc == EN_K1)
            ed.to = (uint8_t)ed_clamp((int32_t)ed.to + d, 0, 99);
        else if (enc == EN_K2 && !song)
            ed.filter = (uint8_t)ed_clamp((int32_t)ed.filter + d, 0, SQ_F_N - 1);
        break;
    case F_QUANT:                 /* K1 the grid, K2 how */
        if (enc == EN_K1)
            ed.grid = (uint8_t)ed_clamp((int32_t)ed.grid + d, 0, 9);
        else if (enc == EN_K2)
            ed.qmode = (uint8_t)ed_clamp((int32_t)ed.qmode + d, 0, 3);
        break;
    case F_TRANSP:                /* K1 semitones (SEL: octaves) */
        if (enc == EN_K1)
            ed.semis = (int8_t)ed_clamp(ed.semis + d * (sel ? 12 : 1), -99, 99);
        break;
    case F_CHAN:                  /* K1 the track's channel: UNCHANGED, 1..16 (at once) */
        if (enc == EN_K1) {
            sq.part[sqp.part].chan[ed.trk] = (uint8_t)ed_clamp(sq.part[sqp.part].chan[ed.trk] + d, 0, 16);
            sq_dirty = 1;
        }
        break;
    case F_NAME:                  /* K1 the cursor, K2 the character under it */
        if (enc == EN_K1) {
            ed.cur = (uint8_t)ed_clamp((int32_t)ed.cur + d, 0, SQ_NAME - 1);
        } else if (enc == EN_K2) {
            for (i = 0; NAME_CHARS[i] && NAME_CHARS[i] != ed.name[ed.cur]; i++)
                ;
            if (!NAME_CHARS[i])
                i = 0;
            v = ed_clamp((int32_t)i + d, 0, (int32_t)sizeof NAME_CHARS - 2);
            ed.name[ed.cur] = NAME_CHARS[v];
        }
        break;
    case F_EDIT:
        if (song)
            ed_turn_step(enc, d);
        else
            ed_turn_event(enc, d, sel);
        break;
    default:
        break;
    }
}

/* what a function did, for a moment over its page */
static void ed_say(int r, const char *done)
{
    char t[LCD_COLS + 1];
    ed_num(t, sqp.song_mode ? "SONG " : "PART ", sqp.song_mode ? sqp.song : sqp.part, 2);
    if (r == 0)
        app_say(done, t);
    else if (r == SQ_E_LONG)
        app_say(sqp.song_mode ? "SONG TOO LONG" : "PART TOO LONG", sqp.song_mode ? "255 STEPS" : "682 BEATS");
    else if (r == SQ_E_NONE)
        app_say(sqp.song_mode ? "EMPTY SONG" : "EMPTY PART", t);
    else
        app_say("MEMORY FULL", "");
}

/* EDIT, a part: REC adds (at the end what K2 chose where K1 put it; on an event a note at its place, the MMT-8's
 * C3, velocity 64, a beat); SEL + REC erases the event shown */
static void ed_rec_event(int sel)
{
    uint32_t p = sqp.part, t = ed.trk, n = sq_events(p, t), ticks = sq_part_ticks(p), tick, st, ch, dur;
    uint8_t ev[SQ_NOTE_LEN];
    int32_t at;
    if (!ticks) {
        app_say("EMPTY PART", "SET ITS LENGTH");
        return;
    }
    if (sel) {
        if (ed.ev < n && sq_event_delete(p, t, ed.ev, sq_scratch) == 0) {
            sq_dirty = 1;
            app_say("EVENT ERASED", "");
        }
        return;
    }
    tick = ed.ev < n ? sq_ev_tick(sq_event(p, t, ed.ev)) : ed.add_tick;
    st = ed.ev < n ? 0x90u : ADD_ST[ed.add];
    ch = sq.part[p].chan[t] ? sq.part[p].chan[t] - 1u : set.s7_channel;   /* (UNCHANGED: the sampler's) */
    dur = ticks - tick < SQ_PPQN ? ticks - tick : SQ_PPQN;
    ev[0] = (uint8_t)tick;
    ev[1] = (uint8_t)(tick >> 8);
    ev[2] = (uint8_t)(st | ch);
    ev[3] = st == 0x90u ? 60u : 0u;
    ev[4] = st == 0x90u ? 64u : st == 0xE0u ? 64u : 0u;   /* (a bend: the middle) */
    ev[5] = (uint8_t)dur;
    ev[6] = (uint8_t)(dur >> 8);
    at = sq_event_put(p, t, n, ev, sq_scratch);
    if (at < 0) {
        app_say("MEMORY FULL", "");
        return;
    }
    ed.ev = (uint16_t)at;
    sq_dirty = 1;
}

/* EDIT, a song: REC puts a step in before the one shown, as it (at the end: the part K1 chose, every track);
 * SEL + REC takes the step out */
static void ed_rec_step(int sel)
{
    uint8_t part = seq_last_part, mask = 0xFFu;
    int r;
    if (sel) {
        if (!ed_step(&part, &mask))
            return;
        r = sq_song_delete(sqp.song, seq_song_step, sq_scratch);
        ed_say(r, "STEP ERASED");
    } else {
        (void)ed_step(&part, &mask);
        r = sq_song_insert(sqp.song, seq_song_step, part, mask, sq_scratch);
        ed_say(r, "STEP ADDED");
    }
    ed_song_reload();
    sq_dirty = 1;
}

/* REC on a function's page: do it */
static void ed_rec(uint32_t btn)
{
    int sel = (int)(btn >> B_SEL & 1u), r = 0;
    uint32_t p = sqp.part, i;
    if (ed.fn != F_CHAN && ed.fn != F_NAME && ed.fn != F_EDIT && sqp.state != SQP_STOPPED) {
        app_say("STOP FIRST", "");
        return;
    }
    switch (ed.fn) {
    case F_LENGTH:
        if (!ed.beats) {
            app_say("NO LENGTH", "TURN K1");
            return;
        }
        r = ed.top ? sq_set_length_top(p, ed.beats, sq_scratch) : sq_set_length(p, ed.beats, sq_scratch);
        ed_say(r, "LENGTH SET");
        break;
    case F_ERASE:
        if (sqp.song_mode) {
            sq_song_erase(sqp.song);
            seq_song_step = 0;
            ed_song_reload();
            ed_say(0, "ERASED");
            break;
        }
        if (!ed.sel) {
            app_say("NO TRACKS", "");
            return;
        }
        r = sq_erase_filter(p, ed.sel, ed.filter, sq_scratch);
        ed_say(r, "ERASED");
        break;
    case F_COPY:
        if (sqp.song_mode)
            r = sq_song_append(sqp.song, ed.to, sq_scratch);
        else if (ed.src < 0)
            r = sq_append_part(p, ed.to, ed.filter, sq_scratch);
        else
            r = sq_copy_track(p, (uint32_t)ed.src, ed.to, (uint32_t)(ed.dst < 0 ? ed.src : ed.dst), ed.filter,
                              sq_scratch);
        ed_say(r, "COPIED");
        if (sqp.song_mode)
            ed_song_reload();
        break;
    case F_MERGE:
        if (ed.src < 0 || ed.dst < 0) {
            app_say("CHOOSE A TRACK", "AND ONE TO");
            return;
        }
        r = sq_merge(p, (uint32_t)ed.src, (uint32_t)ed.dst, sq_scratch);
        ed_say(r, "MERGED");
        break;
    case F_QUANT:
    case F_TRANSP:
        if (!ed.sel) {
            app_say("NO TRACKS", "");
            return;
        }
        if (ed.fn == F_QUANT)
            r = sq_quantize(p, ed.sel, GRID[ed.grid], ed.qmode, sq_scratch);
        else if (ed.semis)
            r = sq_transpose(p, ed.sel, ed.semis, sq_scratch);
        ed_say(r, ed.fn == F_QUANT ? "QUANTIZED" : "TRANSPOSED");
        break;
    case F_NAME:
        if (!sqp.song_mode && sq_part_empty(p)) {
            app_say("EMPTY PART", "NO NAME YET");
            return;
        }
        for (i = 0; i < SQ_NAME; i++) {
            if (sqp.song_mode)
                sq.song[sqp.song].name[i] = ed.name[i];
            else
                sq.part[p].name[i] = ed.name[i];
        }
        ed_say(0, "NAME STORED");
        break;
    case F_EDIT:
        if (sqp.song_mode)
            ed_rec_step(sel);
        else
            ed_rec_event(sel);
        return;
    default:
        return;
    }
    sq_dirty = 1;
    if (ed.fn != F_NAME)
        ed_load();                /* (LENGTH shows the length now) */
}

/* the channel an event plays on: its own, or (in brackets) its own under the track's */
static void ed_chan(char *t, uint32_t ch, uint32_t trk_chan)
{
    char n[4];
    t[0] = 0;
    str_cat(t, trk_chan ? "(" : "CH");
    fmt_uint(n, ch + 1u, 2, '0');
    str_cat(t, n);
    if (trk_chan)
        str_cat(t, ")");
}

/* EDIT, a part: "1 001.00 C#3 100" / "DUR 000.48 CH01" (the track, the place, the key, the velocity; the duration,
 * the channel); other events their values; past the last "1 END OF TRACK" / "ADD NOTE  001.00" */
static void ed_draw_event(char *a, char *b)
{
    uint32_t p = sqp.part, t = ed.trk, n = sq_events(p, t), st, i;
    const uint8_t *e;
    char x[12], c[8];
    a[0] = (char)('1' + t);
    a[1] = ' ';
    a[2] = 0;
    if (ed.ev >= n) {
        str_cat(a, "END OF TRACK");
        x[0] = 0;
        str_cat(x, "ADD ");
        str_cat(x, ADD_TXT[ed.add]);
        ed_pos(c, ed.add_tick, 1);
        ed_lr(b, x, c);
        return;
    }
    e = sq_event(p, t, ed.ev);
    st = e[2] & 0xF0u;
    ed_pos(x, sq_ev_tick(e), 1);
    str_cat(a, x);
    ed_chan(c, e[2] & 0x0Fu, sq.part[p].chan[t]);
    switch (st) {
    case 0x90:
        note_text(x, e[3]);
        for (i = 0; x[i]; i++)
            ;
        while (i < 4u)
            x[i++] = ' ';
        x[i] = 0;
        str_cat(a, " ");
        str_cat(a, x);
        fmt_uint(x, e[4], 3, '0');
        str_cat(a, x);
        b[0] = 0;
        str_cat(b, "DUR ");
        ed_pos(x, sq_ev_dur(e), 0);
        str_cat(b, x);
        str_cat(b, " ");
        str_cat(b, c);
        return;
    case 0xE0:
        str_cat(a, " BEND");
        fmt_signed(x, (int32_t)(e[3] | (uint32_t)e[4] << 7) - 8192, 4);
        ed_lr(b, x, c);
        return;
    case 0xB0:
        str_cat(a, " CTL ");
        break;
    case 0xC0:
        str_cat(a, " PRG ");
        break;
    case 0xD0:
        str_cat(a, " AFT ");
        break;
    case 0xA0:
        str_cat(a, " PAT ");
        note_text(x, e[3]);
        str_cat(a, x);
        break;
    default:
        str_cat(a, " ?");
        break;
    }
    if (st != 0xA0u) {
        fmt_uint(x, e[3], 3, '0');
        str_cat(a, x);
    }
    if (st == 0xB0u || st == 0xA0u) {
        char v[LCD_COLS + 1];
        ed_num(v, "VALUE ", e[4], 3);
        ed_lr(b, v, c);
    } else {
        ed_lr(b, "", c);
    }
}

/* a function's page on the LCD */
static void ed_draw(void)
{
    char a[LCD_COLS + 1], b[LCD_COLS + 1], x[LCD_COLS + 1], n[8];
    uint32_t i, song = sqp.song_mode;
    uint8_t part, mask;
    a[0] = b[0] = 0;
    ed_num(x, song ? "SONG " : "PART ", song ? sqp.song : sqp.part, 2);
    switch (ed.fn) {
    case F_LENGTH:
        ed_lr(a, "LENGTH", x);
        if (ed.beats)
            fmt_uint(n, ed.beats, 3, '0');
        else
            n[0] = 0, str_cat(n, "---");
        x[0] = 0;
        str_cat(x, "BEATS ");
        str_cat(x, n);
        ed_lr(b, x, ed.top ? "AT TOP" : "AT END");
        break;
    case F_ERASE:
        ed_lr(a, "ERASE", x);
        if (song) {
            for (i = 0; i < SQ_NAME; i++)
                b[i] = sq.song[sqp.song].name[i];
            b[SQ_NAME] = 0;
            if (!sq_song_steps(sqp.song))
                b[0] = 0, str_cat(b, "EMPTY SONG");
        } else if (ed.filter < SQ_F_CHAN) {
            str_cat(b, FILTER_TXT[ed.filter]);
        } else {
            ed_num(b, "CHANNEL ", ed.filter - SQ_F_CHAN + 1u, 2);
            str_cat(b, " ONLY");
        }
        break;
    case F_COPY:
        ed_num(a, song ? "COPY SONG " : "COPY ", song ? sqp.song : sqp.part, 2);
        ed_num(x, ">", ed.to, 2);
        str_cat(a, x);
        if (song) {
            for (i = 0; i < SQ_NAME; i++)
                b[i] = sq.song[ed.to].name[i];
            b[SQ_NAME] = 0;
            if (!sq_song_steps(ed.to))
                b[0] = 0, str_cat(b, "EMPTY SONG");
            break;
        }
        if (ed.src < 0) {
            str_cat(a, " ALL");
        } else {
            ed_num(x, " T", (uint32_t)ed.src + 1u, 1);
            str_cat(a, x);
            ed_num(x, ">T", (uint32_t)(ed.dst < 0 ? ed.src : ed.dst) + 1u, 1);
            str_cat(a, x);
        }
        if (ed.filter < SQ_F_CHAN) {
            str_cat(b, FILTER_TXT[ed.filter]);
        } else {
            ed_num(b, "CHANNEL ", ed.filter - SQ_F_CHAN + 1u, 2);
            str_cat(b, " ONLY");
        }
        break;
    case F_MERGE:
        ed_lr(a, "MERGE", x);
        if (ed.src < 0) {
            str_cat(b, "CHOOSE 2 TRACKS");
        } else {
            ed_num(b, "TRACK ", (uint32_t)ed.src + 1u, 1);
            str_cat(b, " > ");
            if (ed.dst < 0)
                str_cat(b, "?");
            else
                ed_num(x, "", (uint32_t)ed.dst + 1u, 1), str_cat(b, x);
        }
        break;
    case F_QUANT:
        ed_lr(a, "QUANTIZE", GRID_TXT[ed.grid]);
        str_cat(b, QMODE_TXT[ed.qmode]);
        break;
    case F_TRANSP:
        ed_num(x, "P", sqp.part, 2);
        ed_lr(a, "TRANSPOSE", x);
        fmt_signed(b, ed.semis, 2);
        str_cat(b, " SEMITONES");
        break;
    case F_CHAN:
        str_cat(a, "MIDI CHANNEL");
        ed_num(x, "TRACK ", ed.trk + 1u, 1);
        i = sq.part[sqp.part].chan[ed.trk];
        if (i)
            ed_num(n, "CH ", i, 2);
        else
            n[0] = 0, str_cat(n, "UNCHANGED");
        ed_lr(b, x, n);
        break;
    case F_NAME:
        ed_lr(a, "NAME", x);
        if (!song && sq_part_empty(sqp.part)) {
            str_cat(b, "EMPTY PART");
        } else {
            for (i = 0; i < SQ_NAME; i++)
                b[i] = ed.name[i];
            b[SQ_NAME] = 0;
        }
        break;
    case F_EDIT:
        if (!song) {
            ed_draw_event(a, b);
            break;
        }
        ed_num(a, "STEP ", seq_song_step + 1u, 3);
        if (ed_step(&part, &mask)) {
            ed_num(x, "PART ", part, 2);
            ed_lr(a, a, x);
            str_cat(b, "TRACKS  ");
            for (i = 0; i < SQ_TRACKS; i++) {
                char c[2] = {mask >> i & 1u ? (char)('1' + i) : '-', 0};
                str_cat(b, c);
            }
        } else {
            ed_lr(a, a, "END");
            ed_num(b, "REC ADDS PART ", seq_last_part, 2);
        }
        break;
    default:
        break;
    }
    lcd_row(0, a);
    lcd_row(1, b);
}

/* the LCD's cursor: NAME's, on a name that can change; else none (-1) */
static int32_t ed_cursor_row(void)
{
    return ed.fn == F_NAME && (sqp.song_mode || !sq_part_empty(sqp.part)) ? 1 : -1;
}

/* the tracks a function has chosen (lit) and the one it goes to (flashing) */
static void ed_lights(uint32_t *sel, uint32_t *to)
{
    uint8_t part, mask;
    *sel = *to = 0;
    switch (ed.fn) {
    case F_ERASE:
    case F_QUANT:
    case F_TRANSP:
        if (!sqp.song_mode)
            *sel = ed.sel;
        break;
    case F_COPY:
    case F_MERGE:
        if (sqp.song_mode)
            break;
        *sel = ed.src >= 0 ? 1u << ed.src : ed.fn == F_COPY ? 0xFFu : 0u;
        *to = ed.dst >= 0 ? 1u << ed.dst : 0u;
        break;
    case F_CHAN:
        *sel = 1u << ed.trk;
        break;
    case F_EDIT:
        if (!sqp.song_mode)
            *sel = 1u << ed.trk;
        else if (ed_step(&part, &mask))
            *sel = mask;
        break;
    default:
        break;
    }
}
