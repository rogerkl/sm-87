/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the MMT-8's page. The LCD as the MMT-8 shows a part or a song playing; the first eight white keys are its
 * eight track buttons while SEL is held (mutes; the ninth the drums), and choose the record track with REC held;
 * PRESET picks the part (or the song), queued while playing; K1 the tempo, K2 the record track's channel, K4 the
 * drum pattern that plays with the parts; in a song, SELECT its step, K3 the step's part (past 99: the song ends
 * there) and K4 its drum pattern (or none). ALGO turns to the MMT-8's functions (seq_edit.c). Part of the unity
 * build (app.c). */

static const uint8_t WHITE[16] = PLAT_WHITE_KEYS;

/* the white key's track, or -1 */
static int32_t key_track(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < SQ_TRACKS; i++)
        if (WHITE[i] == k)
            return (int32_t)i;
    return -1;
}

static uint32_t seq_song_step;    /* the step the song page edits */
static uint8_t seq_last_part;     /* the part last chosen in PART: a new song step starts with it */

/* the first eight white keys' bits: the track buttons */
static uint32_t track_key_bits(void)
{
    uint32_t i, m = 0;
    for (i = 0; i < SQ_TRACKS; i++)
        m |= 1u << WHITE[i];
    return m;
}

/* a song step's drum pattern as its row shows it: "D07", "D--" none */
static void step_drums_text(char *t, uint32_t d)
{
    char n[4];
    str_cat(t, "D");
    if (d >= SQ_PATTERNS) {
        str_cat(t, "--");
    } else {
        fmt_uint(n, d, 2, '0');
        str_cat(t, n);
    }
}

/* K4 on a song step: its drum pattern, none or 00..99 (stopped; the END has none: at says what makes a step) */
static void seq_step_drums(int32_t e, const char *at_end)
{
    uint32_t d;
    int32_t v;
    if (sqp.state != SQP_STOPPED) {
        app_say("STOP FIRST", "TO EDIT A STEP");
        return;
    }
    if (seq_song_step >= sq_song_steps(sqp.song)) {
        app_say("END OF SONG", at_end);
        return;
    }
    d = sq_song_drums(sqp.song, seq_song_step);
    v = clamp((d >= SQ_PATTERNS ? -1 : (int32_t)d) + e, -1, (int32_t)SQ_PATTERNS - 1);
    sq_song_set_drums(sqp.song, seq_song_step, v < 0 ? SQ_NO_DRUMS : (uint32_t)v);
    sq_dirty = 1;
}

#include "seq_edit.c"             /* the MMT-8's functions (ALGO) */

static void seq_draw(int sel_held)
{
    char t[LCD_ROWBUF], n[8];
    uint32_t i, used = sq_part_used(sqp.part);
    const char *state = sqp.state == SQP_STOPPED ? " " : sqp.take_state == SQP_TAKE_RUNNING ? CH_REC : CH_PLAY;
    if (ed.fn) {
        ed_draw();
        return;
    }
    t[0] = 0;
    if (sqp.song_mode) {
        str_cat(t, "SONG ");
        fmt_uint(n, sqp.song, 2, '0');
        str_cat(t, n);
        str_cat(t, "  ST ");
        fmt_uint(n, (sqp.state == SQP_STOPPED ? seq_song_step : sqp.step) + 1u, 3, '0');
    } else {
        str_cat(t, "PART ");
        fmt_uint(n, sqp.part, 2, '0');
        str_cat(t, n);
        if (sqp.next_part != 0xFFu) {        /* "PART 05>07 B003": the next queued, as the drums' PATT shows it */
            str_cat(t, ">");
            fmt_uint(n, sqp.next_part, 2, '0');
            str_cat(t, n);
            str_cat(t, " B");
        } else {
            str_cat(t, "  BT ");
        }
        fmt_uint(n, sqp_beat(), 3, '0');
    }
    str_cat(t, n);
    str_cat(t, state);
    lcd_row(0, t);
    t[0] = 0;
    if (sel_held) {                          /* the tracks: on, muted, empty; the drums on or muted */
        str_cat(t, "TRK ");
        for (i = 0; i < SQ_TRACKS; i++) {
            char c[2] = {(char)('1' + i), 0};
            if (!(used >> i & 1u))
                c[0] = '.';
            else if (!(sqp.mask >> i & 1u))
                c[0] = '-';
            str_cat(t, c);
        }
        str_cat(t, hrp.mute ? "  --" : "  DR");
    } else if (sqp.state == SQP_COUNTIN) {
        str_cat(t, "COUNT IN    ");
        fmt_uint(n, (uint32_t)(sqp.countin + (int32_t)SQ_PPQN - 1) / SQ_PPQN, 1, ' ');
        str_cat(t, n);
    } else if (sqp.rec_armed || sqp.take_state == SQP_TAKE_RUNNING) {
        uint32_t ch = sq.part[sqp.part].chan[sqp.rec_track];
        str_cat(t, "REC TRK ");
        fmt_uint(n, sqp.rec_track + 1u, 1, ' ');
        str_cat(t, n);
        str_cat(t, ch ? " CH " : " UNCHG");
        if (ch) {
            fmt_uint(n, ch, 2, '0');
            str_cat(t, n);
        }
    } else if (sqp.song_mode && sqp.state == SQP_STOPPED) {
        uint32_t steps = sq_song_steps(sqp.song);
        if (seq_song_step < steps) {      /* "P05 12345678 D07": its part, tracks, drum pattern */
            uint8_t part, mask;
            sq_song_step(sqp.song, seq_song_step, &part, &mask);
            str_cat(t, "P");
            fmt_uint(n, part, 2, '0');
            str_cat(t, n);
            str_cat(t, " ");
            for (i = 0; i < SQ_TRACKS; i++) {
                char c[2] = {mask >> i & 1u ? (char)('1' + i) : '-', 0};
                str_cat(t, c);
            }
            str_cat(t, " ");
            step_drums_text(t, sq_song_drums(sqp.song, seq_song_step));
        } else {
            str_cat(t, "END OF SONG");
        }
    } else if (sqp.song_mode) {
        for (i = 0; i < SQ_NAME; i++)
            t[i] = sq.song[sqp.song].name[i];
        t[SQ_NAME] = 0;
    } else {
        for (i = 0; i < SQ_NAME; i++)
            t[i] = sq.part[sqp.part].name[i];
        t[SQ_NAME] = 0;
        if (sq_part_empty(sqp.part))
            t[0] = 0, str_cat(t, "EMPTY PART");
    }
    lcd_row(1, t);
}

/* the strip under the LCD: the eight tracks, the tempo, the memory */
static void seq_status(void)
{
    char t[24], n[8];
    uint32_t i, used = sq_part_used(sqp.part), pct = (uint32_t)sq.used * 100u / SQ_ARENA, sel, to;
    ed_lights(&sel, &to);
    cv_begin(240, STATUS_H, C_BLACK);
    for (i = 0; i < SQ_TRACKS; i++) {
        int32_t x = 6 + (int32_t)i * 29;
        uint16_t c = !(used >> i & 1u) ? C_LINE : (sqp.mask >> i & 1u) ? C_HI : C_DIM;
        if (ed.fn)                /* a function's page: the tracks it takes (lit), the one it goes to */
            c = (to >> i & 1u) ? RGB(230, 60, 40) : (sel >> i & 1u) ? C_HI : (used >> i & 1u) ? C_DIM : C_LINE;
        cv_rect(x, 2, 26, 20, c);
        if ((sqp.rec_armed || sqp.take_state == SQP_TAKE_RUNNING) && sqp.rec_track == i) {
            cv_rect(x, 2, 26, 3, RGB(230, 50, 40));
            cv_rect(x, 19, 26, 3, RGB(230, 50, 40));
        }
        n[0] = (char)('1' + i);
        n[1] = 0;
        cv_text(x + 10, 5, &FONT_XS, n, c == C_HI || c == C_DIM || (to >> i & 1u) ? C_BLACK : C_GRAY);
    }
    t[0] = 0;
    str_cat(t, sqp.song_mode ? "SONG" : "PART");
    cv_text(8, 30, &FONT_XS, t, C_GRAY);
    cv_text(50, 30, &FONT_XS, sqp.clock_in ? "MIDI" : "TEMPO", C_GRAY);   /* (MIDI clock in: its tempo) */
    fmt_uint(n, sqp.clock_in ? sqp.ext_tempo : sqp.tempo, 0, ' ');
    cv_text(92, 30, &FONT_XS, sqp.clock_in && !sqp.ext_tempo ? "--" : n, C_HI);
    cv_text(130, 30, &FONT_XS, "MEM", C_GRAY);
    fmt_uint(n, pct, 0, ' ');
    str_cat(n, "%");
    cv_text(162, 30, &FONT_XS, n, C_HI);
    cv_text(8, 44, &FONT_XS, "SAMPLER", C_GRAY);
    s7_chan_text(t);
    cv_text(70, 44, &FONT_XS, t, C_HI);
    cv_text(130, 44, &FONT_XS, "CPU", C_GRAY);
    fmt_uint(n, plat_cpu_pct(), 0, ' ');
    str_cat(n, "%");
    cv_text(162, 44, &FONT_XS, n, C_HI);
    /* the function (ALGO); in EDIT the event of how many; else the drums with it */
    cv_text(8, 58, &FONT_XS, ed.fn == F_DUMP ? "IN: ANY TIME" : ed.fn ? F_TXT[ed.fn] : "ALGO: FUNCTIONS",
            ed.fn ? C_HI : C_GRAY);   /* (MIDI DUMP: the LCD says it; a dump in is taken any time) */
    if (!ed.fn) {
        uint32_t d = !sqp.song_mode ? hrp.pattern
                     : sq_song_drums(sqp.song, sqp.state == SQP_STOPPED ? seq_song_step : sqp.step);
        t[0] = 0;
        str_cat(t, "DRUMS ");
        if (hrp.mute) {
            str_cat(t, "MUTED");
        } else if (d >= SQ_PATTERNS) {
            str_cat(t, "NONE");
        } else {
            str_cat(t, "PATT ");
            fmt_uint(n, d, 2, '0');
            str_cat(t, n);
        }
        cv_text(130, 58, &FONT_XS, t, C_HI);
    }
    if (ed.fn == F_DUMP)          /* what an MMT-8 takes: ours with the header of one of its dumps */
        cv_text(100, 58, &FONT_XS, md_has_header() ? "HEADER: AN MMT-8'S" : "NO MMT-8 HEADER YET", C_HI);
    if (ed.fn == F_EDIT && !sqp.song_mode) {
        uint32_t ne = sq_events(sqp.part, ed.trk);
        t[0] = 0;
        str_cat(t, "EVENT ");
        fmt_uint(n, ed.ev < ne ? ed.ev + 1u : ne, 0, ' ');
        str_cat(t, n);
        str_cat(t, "/");
        fmt_uint(n, ne, 0, ' ');
        str_cat(t, n);
        cv_text(130, 58, &FONT_XS, t, C_HI);
    }
    cv_blit(0, LCD_Y + LCD_H + 10);
}

/* ---- the panel under the strip (this page and the drums'): what plays, and what comes after it, large ----
 * PRESET queues the next part (or drum pattern) while one plays, and the LCD shows the one playing until the change:
 * here both show (in a song, this step's and the next step's), with how far it has to go. Turning PRESET, NEXT
 * changes at once */
#define PANEL_Y (LCD_Y + LCD_H + 10 + STATUS_H + 2)   /* under the strip, to the screen's foot */
#define PANEL_H (240 - PANEL_Y)
#define PANEL_NONE 100u           /* a column's number: none (--) */
static uint8_t panel_shown;

static void panel_begin(void)
{
    cv_begin(240, PANEL_H, C_BLACK);
    panel_shown = 1;
}

static void panel_end(void) { cv_blit(0, PANEL_Y); }

/* another page: the panel's place black again (once) */
static void panel_off(void)
{
    if (!panel_shown)
        return;
    panel_shown = 0;
    cv_begin(240, PANEL_H, C_BLACK);
    cv_blit(0, PANEL_Y);
}

/* a column: its label, a number large (PANEL_NONE: --; lit: dark on a lit box, as a track button on), and a line
 * under it */
static void panel_col(int32_t x, const char *label, uint32_t num, uint16_t c, int lit, const char *under)
{
    char n[4];
    cv_text(x, 0, &FONT_XS, label, C_GRAY);
    if (num < PANEL_NONE) {
        fmt_uint(n, num, 2, '0');
        if (lit)
            cv_rect(x - 4, 15, text_w(&FONT_L, n) + 8, 28, c);   /* (round the digits: rows 18-38) */
        cv_text(x, 14, &FONT_L, n, lit ? C_BLACK : c);
    } else {
        cv_text(x, 14, &FONT_L, "--", C_LINE);
    }
    if (under)
        cv_text(x, 48, &FONT_XS, under, lit ? c : C_GRAY);
}

/* how far to the change (tick of ticks): the beats to go beside NEXT's number (counting), and a bar along the foot */
static void panel_progress(uint32_t tick, uint32_t ticks, int counting, int queued)
{
    char t[12], n[8];
    if (!ticks)
        return;
    if (counting && tick < ticks) {
        uint32_t left = (ticks - tick + SQ_PPQN - 1u) / SQ_PPQN;
        t[0] = 0;
        str_cat(t, "IN ");
        fmt_uint(n, left, 0, ' ');
        str_cat(t, n);
        cv_text(172, 18, &FONT_XS, t, queued ? C_HI : C_GRAY);
        cv_text(172, 32, &FONT_XS, left == 1u ? "BEAT" : "BEATS", queued ? C_HI : C_GRAY);
    }
    cv_rect(8, PANEL_H - 6, 224, 4, C_LINE);
    cv_rect(8, PANEL_H - 6, (int32_t)((tick < ticks ? tick : ticks) * 224u / ticks), 4, queued ? C_HI : C_DIM);
}

/* LOOP/CLICK's LOOP beside PLAYING's number, "LOOP" over "PARTS" (ALL, OFF): lit when what plays goes round (a part,
 * or in SONG the song), dim when not (PARTS in a song, OFF) */
static void panel_loop(void)
{
    static const char *const T[LOOP_N] = {"OFF", "PARTS", "ALL"};
    int on = sqp.song_mode ? sqp.loop_song : sqp.loop_part;
    cv_text(52, 18, &FONT_XS, "LOOP", C_GRAY);
    cv_text(52, 32, &FONT_XS, T[set.loop % LOOP_N], on ? C_HI : C_DIM);
}

/* "STEP 012" */
static void step_label(char *t, const char *what, uint32_t step)
{
    char n[4];
    t[0] = 0;
    str_cat(t, what);
    str_cat(t, "STEP ");
    fmt_uint(n, step + 1u, 3, '0');
    str_cat(t, n);
}

/* a song's next step after step i (it loops: the first), or -1: the song ends */
static int32_t song_next_step(uint32_t i)
{
    uint32_t steps = sq_song_steps(sqp.song);
    return i + 1u < steps ? (int32_t)(i + 1u) : sqp.loop_song && steps ? 0 : -1;
}

/* a part's name, its spaces at the end gone (EMPTY PART for one with nothing) */
static const char *part_name(char *t, uint32_t p)
{
    uint32_t i;
    if (sq_part_empty(p))
        return "EMPTY PART";
    for (i = 0; i < SQ_NAME; i++)
        t[i] = sq.part[p].name[i];
    t[SQ_NAME] = 0;
    for (i = SQ_NAME; i > 0 && t[i - 1] == ' '; i--)
        t[i - 1] = 0;
    return t;
}

static void seq_panel(void)
{
    char a[24], b[24], na[SQ_NAME + 1], nb[SQ_NAME + 1];
    uint32_t now = PANEL_NONE, next = PANEL_NONE, tick = sqp.tick, playing = sqp.state != SQP_STOPPED;
    uint16_t next_c = C_DIM;
    int queued = 0;
    const char *next_under = 0;
    panel_begin();
    if (ed.fn) {                  /* a function's page: its own, nothing here */
        panel_end();
        return;
    }
    a[0] = b[0] = 0;
    str_cat(b, "NEXT");
    if (sqp.song_mode) {
        uint32_t i = playing ? sqp.step : seq_song_step;
        uint8_t part, mask;
        if (i < sq_song_steps(sqp.song)) {
            int32_t j = song_next_step(i);
            sq_song_step(sqp.song, i, &part, &mask);
            now = part;
            step_label(a, "", i);
            if (j >= 0) {
                sq_song_step(sqp.song, (uint32_t)j, &part, &mask);
                next = part;
                next_c = C_HI;
                step_label(b, "NEXT ", (uint32_t)j);
            } else {
                next_under = "THE SONG ENDS";
            }
        } else {
            str_cat(a, "END OF SONG");
        }
        if (!playing)
            tick = 0;
    } else {
        now = sqp.part;
        str_cat(a, playing ? "PLAYING" : "PART");
        if (sqp.next_part != 0xFFu) {     /* queued: at this part's end */
            next = sqp.next_part;
            next_c = C_HI;
            queued = 1;
        } else if (playing && !sq.part[sqp.part].beats) {   /* an empty part: no end, beats counted */
            next_under = "STARTS NEXT BEAT";
        } else if (playing && !sqp.loop_part) {   /* LOOP off: it stops at its end */
            next_under = "THE PART ENDS";
        } else if (playing) {             /* none: it goes round again */
            next = sqp.part;
            next_under = "PLAYS AGAIN";
        } else {
            next_under = "WHILE PLAYING";
        }
    }
    panel_col(8, a, now, C_HI, 0, now < SQ_PARTS ? part_name(na, now) : 0);
    panel_loop();
    panel_col(128, b, next, next_c, queued, next_under ? next_under : next < SQ_PARTS ? part_name(nb, next) : 0);
    if (!sqp.song_mode && playing && now < SQ_PARTS && !sq_part_ticks(now))   /* an empty part: to the next beat */
        panel_progress(tick % SQ_PPQN, queued ? SQ_PPQN : 0u, queued, queued);
    else
        panel_progress(tick, now < SQ_PARTS ? sq_part_ticks(now) : 0u, playing && next < SQ_PARTS, queued);
    panel_end();
}

/* the track keys' lights while SEL or REC is held: lit on, dim muted, dark empty */
static void seq_track_lights(uint32_t *lit, uint32_t *dim)
{
    uint32_t i, used = sq_part_used(sqp.part);
    if (ed.fn) {                  /* a function's: what it takes lit, where it goes flashing, the others with data dim */
        uint32_t sel, to;
        ed_lights(&sel, &to);
        for (i = 0; i < SQ_TRACKS; i++) {
            if ((sel >> i & 1u) || ((to >> i & 1u) && (plat_ms() / 200u & 1u)))
                *lit |= 1u << WHITE[i];
            else if (used >> i & 1u)
                *dim |= 1u << WHITE[i];
        }
        return;
    }
    for (i = 0; i < SQ_TRACKS; i++) {
        if (!(used >> i & 1u) && !(sqp.rec_armed && sqp.rec_track == i))
            continue;
        if ((sqp.mask >> i & 1u) || (sqp.rec_armed && sqp.rec_track == i))
            *lit |= 1u << WHITE[i];
        else
            *dim |= 1u << WHITE[i];
    }
}
