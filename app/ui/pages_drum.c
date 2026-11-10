/* SM-87: the HR-16's page (ARP). The LCD as the HR-16 shows a pattern: its number, the beat playing; the sixteen
 * white keys its pads. PRESET picks the pattern (queued for the end of the one playing), K1 the tempo, K2 the
 * quantize, K3 the swing; REC arms recording (hits played go into the pattern as it loops, quantized and swung;
 * SEL + a pad held erases that pad's hits as they come by); PLAY plays the drums alone, SEL + PLAY continues.
 * ALGO turns to its functions, each done with REC: LENGTH, ERASE (a pad's hits, or the pattern), COPY (appended to
 * another), PAD SOUND (a pad's sound, tuning, level, pan; REC: as the kit has it), PAD MIDI (its note, group,
 * voices; the drums' channel). A pad key on a pad's function chooses it. Part of the unity build (app.c, after
 * ui/pages_seq.c). */

enum { DF_PLAY, DF_LENGTH, DF_ERASE, DF_COPY, DF_SOUND, DF_MIDI, DF_N };
static const char *const DF_TXT[DF_N] = {"", "LENGTH", "ERASE", "COPY", "PAD SOUND", "PAD MIDI"};

static struct {
    uint8_t fn;
    uint8_t pad;                  /* the pad's functions, ERASE (HR_PADS: the whole pattern) */
    uint8_t to;                   /* COPY: the pattern to */
    uint16_t beats;               /* LENGTH: what REC sets */
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
    if (fn == DF_ERASE)
        dr.pad = HR_PADS;         /* (the whole pattern, until a pad is chosen) */
    else if (dr.pad >= HR_PADS)
        dr.pad = 0;
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
    if (dr.fn == DF_SOUND || dr.fn == DF_MIDI || dr.fn == DF_ERASE)
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
    case DF_SOUND:
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
        set_dirty = 1;
        break;
    case DF_MIDI:
        if (enc == EN_K1)
            p->note = (uint8_t)clamp((int32_t)p->note + e, 0, 127);
        else if (enc == EN_K2)
            p->group = (uint8_t)clamp((int32_t)p->group + e, 0, 9);
        else if (enc == EN_K3)
            p->poly = (uint8_t)clamp((int32_t)p->poly + e, 1, 2);
        else if (enc == EN_K4)
            set.dr_channel = (uint8_t)clamp((int32_t)set.dr_channel + e, 0, 15);
        set_dirty = 1;
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
    case DF_SOUND:
    case DF_MIDI:                 /* the pad as the kit has it; SEL: all of them */
        for (i = 0; i < HR_PADS; i++)
            if ((btn >> B_SEL & 1u) || i == dr.pad)
                hr_pad_default(i);
        set_dirty = 1;
        app_say((btn >> B_SEL & 1u) ? "PADS AS THE KIT" : "PAD AS THE KIT", "");
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
        if (sel_held && hrp.rec) {
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
    case DF_SOUND:
        dr_pad_title(t, dr.pad);
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
    default:
        return;
    }
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
    uint32_t i, pct = (uint32_t)sq.used * 100u / SQ_ARENA;
    cv_begin(240, STATUS_H, C_BLACK);
    for (i = 0; i < HR_PADS; i++) {
        int32_t x = 6 + (int32_t)(i % 8u) * 29, y = 2 + (int32_t)(i / 8u) * 18;
        int has = hr_pad[i].sound != HR_NO_SOUND, lit = (int)(dr_lit_now >> i & 1u);
        int chosen = (dr.fn == DF_SOUND || dr.fn == DF_MIDI || dr.fn == DF_ERASE) && dr.pad == i;
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
    cv_text(8, 58, &FONT_XS, dr.fn ? DF_TXT[dr.fn] : "ALGO: FUNCTIONS", dr.fn ? C_HI : C_GRAY);
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

/* the pad keys' lights: lit as they sound (and the pad a function has), dim with a sound */
static void dr_key_lights(uint32_t *lit, uint32_t *dim)
{
    uint32_t i;
    for (i = 0; i < HR_PADS; i++) {
        int chosen = (dr.fn == DF_SOUND || dr.fn == DF_MIDI || dr.fn == DF_ERASE) && dr.pad == i;
        if ((dr_lit_now >> i & 1u) || chosen)
            *lit |= 1u << WHITE[i];
        else if (hr_pad[i].sound != HR_NO_SOUND)
            *dim |= 1u << WHITE[i];
    }
}
