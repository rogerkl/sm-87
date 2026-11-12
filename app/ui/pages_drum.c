/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the HR-16's page (ARP). The LCD as the HR-16 shows a pattern: its number, the beat playing; the sixteen
 * white keys its pads. PRESET picks the pattern (queued for the end of the one playing), K1 the tempo, K2 the
 * quantize, K3 the swing; REC arms recording (hits played go into the pattern as it loops, quantized and swung;
 * SEL + a pad held erases that pad's hits as they come by); OCT+ held is the HR-16's FILL (a pad held repeats at the
 * quantize rate, recorded too); PLAY plays the drums alone, SEL + PLAY continues.
 * ALGO turns to its functions: STEP EDIT (below), LENGTH, ERASE (a pad's hits, or the pattern), COPY (appended to
 * another), OFFSET (a pad's hits or all moved by clocks, -99..99, round at the ends), PAD SOUND, PAD MIDI (its note,
 * group, voices; the drums' channel; REC: the pad as the kit has it, SEL: all of them), PROGRAM CHANGE (K1: a program
 * change on the drums' channel chooses the pattern, or not; hr16/hr.h hrp_program), MIDI DUMP (the patterns out as an
 * HR-16's SysEx dump, hr16/hrdump.h; one coming in is taken any time), each done with REC. A pad key on a pad's
 * function chooses it. Part of the unity build (app.c, after ui/pages_seq.c).
 *
 * PAD SOUND: a pad's sound, tuning, level, pan, as the pattern playing (stopped: chosen) has them, the HR-16's VOICE,
 * TUNE and MIX: "01 KICK   GLOBAL" while it has none of its own (an edit is the global set-up's, every such
 * pattern's), "01 KICK   PAT 05" with its own (an edit, PAT 05*, the pads' until REC stores it; another pattern
 * taken, gone). REC: the pads' sounds kept with the pattern; SEL + REC: its own let go (the global set-up's again).
 *
 * STEP EDIT, the HR-16's: "STEP 001 + 00/96", the beat and the clock in it, over the hit there ("SNARE  V100 1/2",
 * EMPTY without one). SELECT goes from place to place: each step of the quantize grid (swung), and each hit, on or
 * off the grid; where there are more hits, one at a time; SEL: by beats; round at the ends. A pad played (a white
 * key, or MIDI in) goes in at the place, at its velocity (that pad's there already: its velocity replaced). K1 moves
 * the hit shown a clock (SEL: a step), K2 its velocity, K3 its pad, K4 the grid; REC erases it. PLAY leaves it and
 * plays from the top, as the HR-16. */

enum { DF_PLAY, DF_STEP, DF_LENGTH, DF_ERASE, DF_COPY, DF_OFFSET, DF_SOUND, DF_MIDI, DF_PROG, DF_DUMP, DF_N };
static const char *const DF_TXT[DF_N] = {"",          "STEP EDIT", "LENGTH",         "ERASE",    "COPY", "OFFSET",
                                         "PAD SOUND", "PAD MIDI",  "PROGRAM CHANGE", "MIDI DUMP"};
#define DF_HAS_PAD(fn) ((fn) == DF_SOUND || (fn) == DF_MIDI || (fn) == DF_ERASE || (fn) == DF_OFFSET)

static struct {
    uint8_t fn;
    uint8_t pad;                  /* the pad's functions, ERASE and OFFSET (HR_PADS: every pad) */
    int8_t off;                   /* OFFSET: clocks, -99..99 */
    uint8_t to;                   /* COPY: the pattern to */
    uint16_t beats;               /* LENGTH: what REC sets */
    uint16_t st;                  /* STEP EDIT: the place shown, clocks */
    uint8_t hi;                   /* the hit shown of those there */
} dr;

/* the white key's pad, or -1 */
static int32_t key_pad(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++)
        if (WHITE[i] == k)
            return (int32_t)i;
    return -1;
}

static uint32_t pad_key_bits(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < HR_PADS; i++)
        m |= 1u << WHITE[i];
    return m;
}

/* the pattern the page is about: the one queued, else its own */
static uint32_t dr_pattern(void) { return hrp.next != 0xFFu ? hrp.next : hrp.pattern; }

static int dr_song_playing(void) { return sqp.state == SQP_PLAYING && sqp.song_mode; }

static void dr_enter(uint32_t fn)
{
    dr.fn = (uint8_t)fn;
    dr.beats = (uint16_t)hr_beats(hrp.pattern);
    dr.to = hrp.pattern;
    if (fn == DF_ERASE || fn == DF_OFFSET)
        dr.pad = HR_PADS;         /* (the whole pattern, until a pad is chosen) */
    else if (dr.pad >= HR_PADS)
        dr.pad = 0;
    if (fn == DF_STEP) {          /* from the top; the pads go in here, not recorded */
        dr.st = 0;
        dr.hi = 0;
        hrp.rec = 0;
    }
}

/* ---------------------------------------------------------------------------------------------- STEP EDIT --- */
/* the hit shown (0: none there), and how many there are */
static const uint8_t *dr_step_hit(uint32_t *n_out)
{
    uint32_t first, n;
    if (dr.st >= hr_beats(hrp.pattern) * SQ_PPQN)
        dr.st = 0;                /* (made shorter) */
    n = hr_hits_at(hrp.pattern, dr.st, &first);
    if (dr.hi >= n)
        dr.hi = (uint8_t)(n ? n - 1u : 0u);
    if (n_out)
        *n_out = n;
    return n ? hr_hit_at(hrp.pattern, first + dr.hi) : 0;
}

/* the pads with a hit at the place, a bit each */
static uint32_t dr_step_pads(void)
{
    uint32_t first, n = hr_hits_at(hrp.pattern, dr.st, &first), i, m = 0;
    for (i = 0; i < n; i++)
        m |= 1u << hr_hit_at(hrp.pattern, first + i)[2];
    return m;
}

static int dr_has_hit(uint32_t tick, uint32_t pad)
{
    uint32_t first, n = hr_hits_at(hrp.pattern, tick, &first), i;
    for (i = 0; i < n; i++)
        if (hr_hit_at(hrp.pattern, first + i)[2] == pad)
            return 1;
    return 0;
}

static void dr_step_show(uint32_t pad)
{
    uint32_t first, n = hr_hits_at(hrp.pattern, dr.st, &first), i;
    for (i = 0; i < n; i++)
        if (hr_hit_at(hrp.pattern, first + i)[2] == pad)
            dr.hi = (uint8_t)i;
}

/* the hit shown heard: the drums only (not out over MIDI, not recorded) */
static void dr_step_hear(void)
{
    const uint8_t *h = dr_step_hit(0);
    if (h)
        ev_push(EV_INTERNAL | 0x90u | set.dr_channel, hr_pad[h[2]].note, h[3]);
}

/* SELECT: on (or back) a place, a hit at a time where there are more; by beats: from beat to beat */
static void dr_step_turn(int32_t e, int beats)
{
    uint32_t p = hrp.pattern, ticks = hr_beats(p) * SQ_PPQN, g = HR_GRID[hrp.quant % HR_GRIDS], n;
    for (; e > 0; e--) {
        (void)dr_step_hit(&n);
        if (!beats && dr.hi + 1u < n) {
            dr.hi++;
            continue;
        }
        dr.st = (uint16_t)(beats ? (dr.st / SQ_PPQN + 1u) * SQ_PPQN : hr_step_next(p, dr.st, g, hrp.swing));
        if (dr.st >= ticks)
            dr.st = 0;
        dr.hi = 0;
    }
    for (; e < 0; e++) {
        if (!beats && dr.hi > 0u) {
            dr.hi--;
            continue;
        }
        if (beats)
            dr.st = (uint16_t)(dr.st % SQ_PPQN ? dr.st / SQ_PPQN * SQ_PPQN : dr.st ? dr.st - SQ_PPQN : ticks - SQ_PPQN);
        else
            dr.st = (uint16_t)hr_step_prev(p, dr.st, g, hrp.swing);
        (void)dr_step_hit(&n);
        dr.hi = (uint8_t)(n ? n - 1u : 0u);
    }
    dr_step_hear();
}

/* K1 its place, K2 its velocity, K3 its pad (a place or pad where that pad has one already: passed over) */
static void dr_step_change(uint32_t enc, int32_t e, int sel)
{
    uint32_t p = hrp.pattern, ticks = hr_beats(p) * SQ_PPQN, pad, vel, g = HR_GRID[hrp.quant % HR_GRIDS];
    const uint8_t *h = dr_step_hit(0);
    int r = 0;
    if (!h)
        return;
    pad = h[2];
    vel = h[3];
    if (enc == EN_K1) {
        int32_t t = (int32_t)dr.st, d = e > 0 ? 1 : -1;
        for (; e; e -= d) {       /* a clock (SEL: a step) at a time, while the pad is free there */
            int32_t to = t + d * (sel ? (int32_t)g : 1);
            if (to < 0 || to >= (int32_t)ticks || dr_has_hit((uint32_t)to, pad))
                break;
            t = to;
        }
        if ((uint32_t)t != dr.st) {
            r = hr_move(p, dr.st, pad, (uint32_t)t, pad, sq_scratch);
            dr.st = (uint16_t)t;
            dr_step_show(pad);
        }
    } else if (enc == EN_K2) {
        r = hr_add(p, dr.st, pad, (uint32_t)clamp((int32_t)vel + e, 1, 127), sq_scratch);
        dr_step_hear();
    } else if (enc == EN_K3) {    /* the next pad without a hit here */
        uint32_t taken = dr_step_pads();
        int32_t np = (int32_t)pad, d = e > 0 ? 1 : -1;
        for (; e; e -= d) {
            int32_t q = np + d;
            while (q >= 0 && q < (int32_t)HR_PADS && (taken >> q & 1u))
                q += d;
            if (q < 0 || q >= (int32_t)HR_PADS)
                break;
            np = q;
        }
        if ((uint32_t)np != pad) {
            r = hr_move(p, dr.st, pad, dr.st, (uint32_t)np, sq_scratch);
            dr_step_show((uint32_t)np);
            dr_step_hear();
        }
    }
    if (r)
        app_say("MEMORY FULL", "");
    sq_dirty = 1;
}

/* a pad played in STEP EDIT (hr_service): in at the place */
static void dr_step_add(uint32_t pad, uint32_t vel)
{
    uint32_t p = hrp.pattern;
    (void)dr_step_hit(0);
    if (!sq.pattern[p].beats)     /* (its length now its own: the default it showed) */
        sq.pattern[p].beats = HR_DEFAULT_BEATS;
    if (hr_add(p, dr.st, pad, vel, sq_scratch) != 0) {
        app_say("MEMORY FULL", "");
        return;
    }
    dr_step_show(pad);
    sq_dirty = 1;
}

static void dr_algo(int32_t d)
{
    int32_t f = (int32_t)dr.fn + d;
    f = f < 0 ? 0 : f >= DF_N ? DF_N - 1 : f;
    if ((uint32_t)f != dr.fn)
        dr_enter((uint32_t)f);
}

/* a pad's key on the page: on a pad's function it chooses the pad (and plays) */
static void dr_pad_key(uint32_t pad)
{
    if (DF_HAS_PAD(dr.fn))
        dr.pad = (uint8_t)pad;
}

static void dr_pattern_turn(int32_t e)
{
    int32_t v = (int32_t)dr_pattern() + e;
    uint32_t f;
    v = v < 0 ? 0 : v > 99 ? 99 : v;
    if (hrp.rec && (sqp.aux || sqp.state != SQP_STOPPED)) {
        app_say("RECORDING", "STOP FIRST");   /* (the HR-16: no other pattern while recording) */
        return;
    }
    f = plat_lock();
    if (sqp.aux || (sqp.state != SQP_STOPPED && !sqp.song_mode))
        hrp.next = (uint8_t)v == hrp.pattern ? 0xFFu : (uint8_t)v;   /* playing: at the end of this one */
    else
        hrp.pattern = (uint8_t)v, hrp.next = 0xFFu;
    plat_unlock(f);
    dr.beats = (uint16_t)hr_beats(dr_pattern());
}

/* an encoder on the drum page */
static void dr_turn(uint32_t enc, int32_t e, uint32_t btn)
{
    hr_pad_t *p = &hr_pad[dr.pad % HR_PADS];
    if (enc == EN_PRESET) {
        dr_pattern_turn(e);
        return;
    }
    if (enc == EN_SELECT && (dr.fn == DF_SOUND || dr.fn == DF_MIDI)) {
        dr.pad = (uint8_t)clamp((int32_t)dr.pad + e, 0, HR_PADS - 1);
        return;
    }
    switch (dr.fn) {
    case DF_STEP:
        if (enc == EN_SELECT) {
            dr_step_turn(e, (int)(btn >> B_SEL & 1u));
        } else if (enc == EN_K4) {   /* the grid: the steps it stops at */
            set.dr_quant = (uint8_t)clamp((int32_t)set.dr_quant + e, 0, HR_GRIDS - 1);
            hrp.quant = set.dr_quant;
            set_dirty = 1;
        } else {
            dr_step_change(enc, e, (int)(btn >> B_SEL & 1u));
        }
        break;
    case DF_PLAY:
        if (enc == EN_K1) {
            uint32_t f = plat_lock();
            sqp_set_tempo((uint32_t)((int32_t)sqp.tempo + e));
            plat_unlock(f);
        } else if (enc == EN_K2) {
            set.dr_quant = (uint8_t)clamp((int32_t)set.dr_quant + e, 0, HR_GRIDS - 1);
            hrp.quant = set.dr_quant;
            set_dirty = 1;
        } else if (enc == EN_K3) {
            set.dr_swing = (uint8_t)clamp((int32_t)set.dr_swing + e, 50, HR_SWING_MAX);
            hrp.swing = set.dr_swing;
            set_dirty = 1;
        }
        break;
    case DF_LENGTH:
        if (enc == EN_K1)         /* SEL held: ten at a time */
            dr.beats = (uint16_t)clamp((int32_t)dr.beats + e * ((btn >> B_SEL & 1u) ? 10 : 1), 1, SQ_MAX_BEATS);
        break;
    case DF_ERASE:
        if (enc == EN_K1)         /* the pads, then the whole pattern */
            dr.pad = (uint8_t)clamp((int32_t)dr.pad + e, 0, HR_PADS);
        break;
    case DF_COPY:
        if (enc == EN_K1)
            dr.to = (uint8_t)clamp((int32_t)dr.to + e, 0, SQ_PATTERNS - 1);
        break;
    case DF_OFFSET:
        if (enc == EN_K1)         /* SEL held: ten at a time */
            dr.off = (int8_t)clamp((int32_t)dr.off + e * ((btn >> B_SEL & 1u) ? 10 : 1), -99, 99);
        else if (enc == EN_K2)    /* the pads, then all of them */
            dr.pad = (uint8_t)clamp((int32_t)dr.pad + e, 0, HR_PADS);
        break;
    case DF_SOUND: {              /* (the player changes the pads' sounds as the pattern changes: held) */
        uint32_t f = plat_lock();
        if (enc == EN_K1) {       /* none, the kit's, the slots */
            int32_t s = p->sound == HR_NO_SOUND ? -1 : (int32_t)p->sound;
            s = clamp(s + e, -1, (int32_t)hr_sounds() - 1);
            p->sound = s < 0 ? HR_NO_SOUND : (uint8_t)s;
        } else if (enc == EN_K2) {
            p->tune = (int8_t)clamp(p->tune + e, -HR_TUNE_MAX, HR_TUNE_MAX);
        } else if (enc == EN_K3) {
            p->level = (uint8_t)clamp((int32_t)p->level + e, 0, 99);
        } else if (enc == EN_K4) {
            p->pan = (int8_t)clamp(p->pan + e, -HR_PAN_MAX, HR_PAN_MAX);
        }
        hr_pad_changed(dr.pad % HR_PADS);
        if (!hr_snd.own)
            set_dirty = 1;        /* (the global set-up's) */
        plat_unlock(f);
        break;
    }
    case DF_MIDI: {
        uint32_t f = plat_lock();
        if (enc == EN_K1)
            p->note = (uint8_t)clamp((int32_t)p->note + e, 0, 127);
        else if (enc == EN_K2)
            p->group = (uint8_t)clamp((int32_t)p->group + e, 0, 9);
        else if (enc == EN_K3)
            p->poly = (uint8_t)clamp((int32_t)p->poly + e, 1, 2);
        else if (enc == EN_K4)
            set.dr_channel = (uint8_t)clamp((int32_t)set.dr_channel + e, 0, 15);
        hr_pad_changed(dr.pad % HR_PADS);
        plat_unlock(f);
        set_dirty = 1;
        break;
    }
    case DF_PROG:                 /* K1: on or off (at once; SAVE keeps it) */
        if (enc == EN_K1 && (uint32_t)clamp((int32_t)set.dr_prog + e, 0, 1) != set.dr_prog) {
            set.dr_prog = (uint8_t)!set.dr_prog;
            set_dirty = 1;
        }
        break;
    default:
        break;
    }
}

static void dr_say(int r, const char *done)
{
    if (r == 0)
        app_say(done, "");
    else if (r == SQ_E_LONG)
        app_say("TOO MANY BEATS", "682 AT MOST");
    else if (r == SQ_E_NONE)
        app_say("NOTHING TO COPY", "");
    else
        app_say("MEMORY FULL", "");
}

/* REC on a function's page: do it */
static void dr_rec(uint32_t btn)
{
    uint32_t p = hrp.pattern, i;
    int r = 0;
    int stopped = sqp.state == SQP_STOPPED && !sqp.aux;
    switch (dr.fn) {
    case DF_STEP: {               /* the hit shown out */
        const uint8_t *h = dr_step_hit(0);
        if (!h)
            return;
        r = hr_remove(p, dr.st, h[2], sq_scratch);
        if (r == 0)
            (void)dr_step_hit(0);
        break;
    }
    case DF_LENGTH:
        if (!stopped) {
            app_say("STOP FIRST", "");
            return;
        }
        r = hr_set_length(p, dr.beats, sq_scratch);
        dr_say(r, "LENGTH SET");
        break;
    case DF_ERASE:
        r = hr_erase(p, dr.pad, sq_scratch);
        dr_say(r, dr.pad < HR_PADS ? "PAD ERASED" : "PATTERN ERASED");
        break;
    case DF_COPY:
        if (!stopped) {
            app_say("STOP FIRST", "");
            return;
        }
        r = hr_copy(p, dr.to, sq_scratch);
        dr_say(r, "COPY COMPLETE");
        break;
    case DF_OFFSET:               /* (again: as much again) */
        if (!dr.off)
            return;
        r = hr_offset(p, dr.pad, dr.off, sq_scratch);
        dr_say(r, dr.pad < HR_PADS ? "PAD MOVED" : "PATTERN MOVED");
        break;
    case DF_SOUND: {              /* the pads' sounds kept with the pattern; SEL: its own let go */
        char n[4], t[LCD_COLS + 1];
        uint32_t sp = hr_snd.p;
        if (sp >= SQ_PATTERNS)
            return;
        t[0] = 0;
        str_cat(t, "PATT ");
        fmt_uint(n, sp, 2, '0');
        str_cat(t, n);
        if (btn >> B_SEL & 1u) {
            r = hr_sounds_drop(sp);
            app_say("GLOBAL SOUNDS", t);
        } else {
            if ((r = hr_sounds_store(sp)) != 0) {
                app_say("MEMORY FULL", "");
                return;
            }
            app_say("SOUNDS STORED", t);
        }
        break;
    }
    case DF_MIDI: {               /* the pad as the kit has it; SEL: all of them */
        uint32_t f = plat_lock();
        for (i = 0; i < HR_PADS; i++)
            if ((btn >> B_SEL & 1u) || i == dr.pad) {
                hr_pad_default(i);
                hr_pad_changed(i);
            }
        plat_unlock(f);
        set_dirty = 1;
        app_say((btn >> B_SEL & 1u) ? "PADS AS THE KIT" : "PAD AS THE KIT", "");
        return;
    }
    case DF_DUMP:                 /* the patterns out, as an HR-16's dump (it goes on while playing) */
        hd_send();
        return;
    default:
        return;
    }
    if (r == 0)
        sq_dirty = 1;
}

/* REC on the main page: recording armed or not (on: the hits played go in while the pattern loops) */
static void dr_rec_arm(void)
{
    if (!hrp.rec && dr_song_playing()) {
        app_say("RECORD A PATTERN", "SEQ: PART MODE");
        return;
    }
    hrp.rec = (uint8_t)!hrp.rec;
    hrp.next = 0xFFu;
}

/* ------------------------------------------------------------------------------------------------ the LCD --- */
static void dr_pad_title(char *t, uint32_t pad)
{
    char n[4];
    t[0] = 0;
    str_cat(t, "PAD ");
    fmt_uint(n, pad + 1u, 2, '0');
    str_cat(t, n);
    str_cat(t, " ");
    str_cat(t, hr_pad_name(pad));
}

static void dr_draw(int sel_held)
{
    char t[LCD_COLS + 1], n[12];
    uint32_t p = dr_pattern(), i;
    const hr_pad_t *pd = &hr_pad[dr.pad % HR_PADS];
    const char *state = sqp.state == SQP_STOPPED && !sqp.aux ? " " : hrp_recording() ? CH_REC : CH_PLAY;
    t[0] = 0;
    switch (dr.fn) {
    case DF_PLAY:
        if (dr_song_playing()) {
            uint32_t sp = sq_song_drums(sqp.song, sqp.step);
            if (sp >= SQ_PATTERNS) {
                str_cat(t, "SONG: NO DRUMS");
            } else {
                str_cat(t, "SNG PATT ");
                fmt_uint(n, sp, 2, '0');
                str_cat(t, n);
                str_cat(t, " B");
                fmt_uint(n, hrp.at / SQ_PPQN + 1u, 2, '0');
                str_cat(t, n);
            }
        } else {
            str_cat(t, "PATT ");
            fmt_uint(n, hrp.pattern, 2, '0');
            str_cat(t, n);
            if (hrp.next != 0xFFu) {
                str_cat(t, ">");
                fmt_uint(n, hrp.next, 2, '0');
                str_cat(t, n);
                str_cat(t, " B");
            } else {
                str_cat(t, "  BT ");
            }
            fmt_uint(n, sqp.state == SQP_STOPPED && !sqp.aux ? 1u : hrp.at / SQ_PPQN + 1u, 3, '0');
            str_cat(t, n);
        }
        for (i = 0; t[i]; i++)    /* the state at the right */
            ;
        for (; i < LCD_COLS - 1u; i++)
            t[i] = ' ';
        t[LCD_COLS - 1] = state[0];
        t[LCD_COLS] = 0;
        lcd_row(0, t);
        t[0] = 0;
        if (hrp.fill) {           /* OCT+ held: FILL at the quantize rate */
            str_cat(t, "FILL ");
            str_cat(t, hrp.quant % HR_GRIDS == HR_GRIDS - 1u ? "1/32" : HR_GRID_TXT[hrp.quant % HR_GRIDS]);
            str_cat(t, ": A PAD");
        } else if (sel_held && hrp.rec) {
            str_cat(t, "SEL+PAD: ERASE");
        } else if (hrp.rec) {
            str_cat(t, "REC ");
            str_cat(t, HR_GRID_TXT[hrp.quant % HR_GRIDS]);
            str_cat(t, " SW ");
            fmt_uint(n, hrp.swing, 2, '0');
            str_cat(t, n);
            str_cat(t, "%");
        } else {
            fmt_uint(n, hr_beats(p), 0, ' ');
            str_cat(t, n);
            str_cat(t, hr_beats(p) == 1u ? " BEAT " : " BEATS ");
            if (hr_hits(p)) {
                fmt_uint(n, hr_hits(p), 0, ' ');
                str_cat(t, n);
                str_cat(t, " HITS");
            } else {
                str_cat(t, "EMPTY");
            }
        }
        lcd_row(1, t);
        return;
    case DF_STEP: {               /* "STEP 001 + 00/96", "SNARE  V100 1/2" */
        uint32_t nh;
        const uint8_t *h = dr_step_hit(&nh);
        str_cat(t, "STEP ");
        fmt_uint(n, dr.st / SQ_PPQN + 1u, 3, '0');
        str_cat(t, n);
        str_cat(t, " + ");
        fmt_uint(n, dr.st % SQ_PPQN, 2, '0');
        str_cat(t, n);
        str_cat(t, "/96");
        lcd_row(0, t);
        t[0] = 0;
        if (!h) {
            str_cat(t, "EMPTY");
        } else {
            const char *nm = hr_pad_name(h[2]);
            for (i = 0; i < HR_NAME; i++)   /* the pad's name, six wide */
                t[i] = ' ';
            for (i = 0; i < HR_NAME && nm[i]; i++)
                t[i] = nm[i];
            t[HR_NAME] = 0;
            str_cat(t, " V");
            fmt_uint(n, h[3], 3, ' ');
            str_cat(t, n);
            if (nh > 1u) {
                str_cat(t, " ");
                fmt_uint(n, dr.hi + 1u, 0, ' ');
                str_cat(t, n);
                str_cat(t, "/");
                fmt_uint(n, nh, 0, ' ');
                str_cat(t, n);
            }
        }
        lcd_row(1, t);
        return;
    }
    case DF_LENGTH:
        str_cat(t, "LENGTH  PATT ");
        fmt_uint(n, hrp.pattern, 2, '0');
        str_cat(t, n);
        lcd_row(0, t);
        t[0] = 0;
        fmt_uint(n, dr.beats, 3, ' ');
        str_cat(t, n);
        str_cat(t, " BEATS   REC");
        lcd_row(1, t);
        return;
    case DF_ERASE:
        str_cat(t, "ERASE   PATT ");
        fmt_uint(n, hrp.pattern, 2, '0');
        str_cat(t, n);
        lcd_row(0, t);
        t[0] = 0;
        if (dr.pad < HR_PADS) {
            dr_pad_title(t, dr.pad);
        } else {
            str_cat(t, "ALL OF IT");
        }
        lcd_row(1, t);
        return;
    case DF_COPY:
        str_cat(t, "COPY PATT ");
        fmt_uint(n, hrp.pattern, 2, '0');
        str_cat(t, n);
        str_cat(t, ">");
        fmt_uint(n, dr.to, 2, '0');
        str_cat(t, n);
        lcd_row(0, t);
        lcd_row(1, hr_hits(dr.to) ? "REC: APPENDED" : "REC: COPIED");
        return;
    case DF_OFFSET:               /* "OFFSET  PATT 05" / "KICK     +03/384" */
        str_cat(t, "OFFSET  PATT ");
        fmt_uint(n, hrp.pattern, 2, '0');
        str_cat(t, n);
        lcd_row(0, t);
        fmt_signed(n, dr.off, 2);
        str_cat(n, "/384");
        ed_lr(t, dr.pad < HR_PADS ? hr_pad_name(dr.pad) : "ALL PADS", n);
        lcd_row(1, t);
        return;
    case DF_SOUND: {              /* "01 KICK   GLOBAL", "06 RIDE  PAT 05*" */
        const char *nm = hr_pad_name(dr.pad % HR_PADS);
        char l[LCD_COLS + 1];
        fmt_uint(l, dr.pad % HR_PADS + 1u, 2, '0');
        if (!(nm[0] == 'P' && nm[1] == 'A' && nm[2] == 'D' && nm[3] == ' ')) {   /* (unnamed: its number) */
            str_cat(l, " ");
            str_cat(l, nm);
        }
        n[0] = 0;
        if (hr_snd.own) {
            str_cat(n, "PAT ");
            fmt_uint(n + 4, hr_snd.p, 2, '0');
            str_cat(n, hr_snd.edited ? "*" : "");
        } else {
            str_cat(n, "GLOBAL");
        }
        ed_lr(t, l, n);
        lcd_row(0, t);
        for (i = 0; i < LCD_COLS; i++)   /* "CYMBL  -3 85 <>" */
            t[i] = ' ';
        t[LCD_COLS] = 0;
        hr_sound_name(pd->sound, n);
        for (i = 0; n[i]; i++)
            t[i] = n[i];
        fmt_signed(n, pd->tune, 2);
        for (i = 0; n[i] && i < 3u; i++)
            t[7 + i] = n[i];
        fmt_uint(n, pd->level, 2, ' ');
        t[11] = n[0], t[12] = n[1];
        if (pd->pan < 0) {        /* the HR-16's <3 .. <> .. 3> */
            t[14] = '<', t[15] = (char)('0' - pd->pan);
        } else if (pd->pan > 0) {
            t[14] = (char)('0' + pd->pan), t[15] = '>';
        } else {
            t[14] = '<', t[15] = '>';
        }
        lcd_row(1, t);
        return;
    }
    case DF_MIDI:
        dr_pad_title(t, dr.pad);
        lcd_row(0, t);
        t[0] = 0;
        note_text(n, pd->note);
        str_cat(t, n);
        str_cat(t, pd->group ? " GRP" : " GRP-");
        if (pd->group) {
            fmt_uint(n, pd->group, 1, ' ');
            str_cat(t, n);
        }
        str_cat(t, pd->poly > 1u ? " V2 CH" : " V1 CH");
        fmt_uint(n, set.dr_channel + 1u, 2, '0');
        str_cat(t, n);
        lcd_row(1, t);
        return;
    case DF_PROG:                 /* "PROGRAM CHANGE" / "PATT SELECT  OFF", as the HR-16's */
        lcd_row(0, "PROGRAM CHANGE");
        ed_lr(t, "PATT SELECT", set.dr_prog ? "ON" : "OFF");
        lcd_row(1, t);
        return;
    case DF_DUMP:                 /* "MIDI DUMP  HR-16" / "REC SENDS ALL" */
        ed_lr(t, "MIDI DUMP", "HR-16");
        lcd_row(0, t);
        lcd_row(1, "REC SENDS ALL");
        return;
    default:
        return;
    }
}

/* the pads a function has: its pad; in STEP EDIT those with a hit at the place */
static uint32_t dr_chosen(void)
{
    if (dr.fn == DF_STEP)
        return dr_step_pads();
    if (DF_HAS_PAD(dr.fn) && dr.pad < HR_PADS)
        return 1u << dr.pad;
    return 0;
}

/* the strip under the LCD: the sixteen pads (lit as they sound, dim with a sound, dark without), the settings */
static uint32_t dr_lit_now;       /* pads lit, and when they were */
static uint32_t dr_lit_ms[HR_PADS];

static void dr_lights_take(void)
{
    uint32_t f = plat_lock(), m = hr_lit, i, now = plat_ms();
    hr_lit = 0;
    plat_unlock(f);
    for (i = 0; i < HR_PADS; i++)
        if (m >> i & 1u)
            dr_lit_ms[i] = now;
    dr_lit_now = 0;
    for (i = 0; i < HR_PADS; i++)
        if (now - dr_lit_ms[i] < 90u)
            dr_lit_now |= 1u << i;
}

static void dr_status(void)
{
    char t[24], n[8];
    uint32_t i, pct = (uint32_t)sq.used * 100u / SQ_ARENA, ch = dr_chosen();
    cv_begin(240, STATUS_H, C_BLACK);
    for (i = 0; i < HR_PADS; i++) {
        int32_t x = 6 + (int32_t)(i % 8u) * 29, y = 2 + (int32_t)(i / 8u) * 18;
        int has = hr_pad[i].sound != HR_NO_SOUND, lit = (int)(dr_lit_now >> i & 1u);
        int chosen = (int)(ch >> i & 1u);
        uint16_t c = lit ? RGB(240, 200, 60) : chosen ? C_HI : has ? C_DIM : C_LINE;
        const char *nm = hr_pad_name(i);
        cv_rect(x, y, 26, 16, c);
        if (nm[0] == 'P' && nm[1] == 'A' && nm[2] == 'D' && nm[3] == ' ') {
            n[0] = nm[4], n[1] = nm[5], n[2] = 0;   /* a pad the kit doesn't name: its number */
        } else {
            n[0] = nm[0];         /* its name's first three */
            n[1] = nm[0] ? nm[1] : 0;
            n[2] = n[1] ? nm[2] : 0;
            n[3] = 0;
        }
        cv_text(x + 3, y + 3, &FONT_XS, n, lit || chosen || has ? C_BLACK : C_GRAY);
    }
    cv_text(8, 40, &FONT_XS, "TEMPO", C_GRAY);
    fmt_uint(n, sqp.clock_in ? sqp.ext_tempo : sqp.tempo, 0, ' ');
    cv_text(50, 40, &FONT_XS, n, C_HI);
    cv_text(84, 40, &FONT_XS, "Q", C_GRAY);
    cv_text(96, 40, &FONT_XS, HR_GRID_TXT[hrp.quant % HR_GRIDS], C_HI);
    cv_text(140, 40, &FONT_XS, "SWING", C_GRAY);
    fmt_uint(n, hrp.swing, 0, ' ');
    str_cat(n, "%");
    cv_text(186, 40, &FONT_XS, n, C_HI);
    cv_text(8, 58, &FONT_XS, dr.fn == DF_DUMP ? "IN: ANY TIME" : dr.fn ? DF_TXT[dr.fn] : "ALGO: FUNCTIONS",
            dr.fn ? C_HI : C_GRAY);   /* (MIDI DUMP: the LCD says it; a dump in is taken any time) */
    if (dr.fn == DF_DUMP) {       /* what an HR-16 takes: ours with the header of one of its dumps */
        cv_text(100, 58, &FONT_XS, hd_has_header() ? "HEADER: AN HR-16'S" : "NO HR-16 HEADER YET", C_HI);
        cv_blit(0, LCD_Y + LCD_H + 10);
        return;
    }
    t[0] = 0;
    str_cat(t, "CH ");
    fmt_uint(n, set.dr_channel + 1u, 0, ' ');
    str_cat(t, n);
    str_cat(t, "  MEM ");
    fmt_uint(n, pct, 0, ' ');
    str_cat(t, n);
    str_cat(t, "%");
    cv_text(130, 58, &FONT_XS, t, C_HI);
    cv_blit(0, LCD_Y + LCD_H + 10);
}

/* a pattern under its number: "8 BEATS 24 HITS", "8 BEATS EMPTY" */
static const char *patt_under(char *t, uint32_t p)
{
    char n[8];
    uint32_t b = hr_beats(p), h = hr_hits(p);
    t[0] = 0;
    fmt_uint(n, b, 0, ' ');
    str_cat(t, n);
    str_cat(t, b == 1u ? " BEAT " : " BEATS ");
    if (!h) {
        str_cat(t, "EMPTY");
    } else {
        fmt_uint(n, h, 0, ' ');
        str_cat(t, n);
        str_cat(t, h == 1u ? " HIT" : " HITS");
    }
    return t;
}

/* the panel under the strip (pages_seq.c's): the pattern playing and the next, large. Its own loop: PRESET queues the
 * next for the end of the one playing; in a song, each step's pattern, the next step's after it */
static void dr_panel(void)
{
    char a[24], b[24], ua[24], ub[24];
    uint32_t now, next = PANEL_NONE, tick, ticks;
    int running = sqp.aux || sqp.state != SQP_STOPPED, queued = 0;
    uint16_t next_c = C_DIM;
    const char *next_under = 0;
    panel_begin();
    if (dr.fn) {                  /* a function's page: its own, nothing here */
        panel_end();
        return;
    }
    a[0] = b[0] = 0;
    str_cat(b, "NEXT");
    if (dr_song_playing()) {      /* the song's: this step's pattern, then the next step's */
        int32_t j = song_next_step(sqp.step);
        now = sq_song_drums(sqp.song, sqp.step);
        step_label(a, "", sqp.step);
        if (j >= 0) {
            next = sq_song_drums(sqp.song, (uint32_t)j);
            next_c = C_HI;
            step_label(b, "NEXT ", (uint32_t)j);
            next_under = next < SQ_PATTERNS ? patt_under(ub, next) : "NO DRUMS";
        } else {
            next_under = "THE SONG ENDS";
        }
        tick = sqp.tick;          /* (the step goes on at its part's end) */
        ticks = sq_part_ticks(sqp.part);
    } else {
        now = hrp.pattern;
        str_cat(a, !running ? "PATTERN" : sqp.state == SQP_PLAYING && hrp.mute ? "MUTED" : "PLAYING");
        if (hrp.next != 0xFFu) {  /* queued: at this pattern's end */
            next = hrp.next;
            next_c = C_HI;
            queued = 1;
            next_under = patt_under(ub, next);
        } else if (running) {     /* none: it goes round again */
            next = hrp.pattern;
            next_under = "PLAYS AGAIN";
        } else {
            next_under = "WHILE PLAYING";
        }
        tick = running ? hrp.at : 0u;
        ticks = hr_beats(now) * SQ_PPQN;
    }
    panel_col(8, a, now, C_HI, 0, now < SQ_PATTERNS ? patt_under(ua, now) : "NO DRUMS");
    panel_col(128, b, next, next_c, queued, next_under);
    panel_progress(tick, ticks, running && next < PANEL_NONE, queued);
    panel_end();
}

/* the pad keys' lights: lit as they sound (and the pad a function has), dim with a sound */
static void dr_key_lights(uint32_t *lit, uint32_t *dim)
{
    uint32_t i, ch = dr_chosen();
    for (i = 0; i < HR_PADS; i++) {
        int chosen = (int)(ch >> i & 1u);
        if ((dr_lit_now >> i & 1u) || chosen)
            *lit |= 1u << WHITE[i];
        else if (hr_pad[i].sound != HR_NO_SOUND)
            *dim |= 1u << WHITE[i];
    }
}
