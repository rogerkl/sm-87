/* SM-87: the sampler's pages, as the S700's menus: a slot's points, its tuning, its key range in the program, its
 * OUT (filter, release, level and their velocity amounts) and its LFO. Two or four values a page, a column each on the
 * LCD, K1..K4 turning them; SELECT picks the slot, PRESET the program. Part of the unity build (app.c). */

enum {
    PV_BYTE,                      /* a value 0..max */
    PV_SIGNED,                    /* signed byte */
    PV_WORD,                      /* a point: START, END + 1, LOOP */
    PV_NOTE,                      /* a key, shown as a note */
    PV_MODE,                      /* scan mode */
    PV_KEYL,                      /* the program's: low key */
    PV_KEYH,
    PV_TRANSP,
    PV_CONST,
    PV_PRESSURE,                  /* the X7000's pressure sens, the slot's own (not in SPRM) */
};

typedef struct {
    const char *label;
    uint8_t kind;
    uint8_t k;                    /* the SPRM value */
    int16_t min, max;
} pval_t;

typedef struct {
    const char *name;             /* for the screen reader and the page's first message */
    uint8_t ncols;
    pval_t v[4];
} page_t;

enum { PG_HOME, PG_POINTS, PG_LOOP, PG_TUNE, PG_KEYS, PG_OUT1, PG_OUT2, PG_LFO, PG_N };

static const page_t PAGES[PG_N] = {
    [PG_HOME] = {"HOME", 0, {{0}}},
    [PG_POINTS] = {"POINTS", 2, {{"START", PV_WORD, S7_P_SSTRT, 0, 32552}, {"END", PV_WORD, S7_P_SEND, 200, 32752}}},
    [PG_LOOP] = {"LOOP", 2, {{"LOOP", PV_WORD, S7_P_SLOOP, 0, 32751}, {"MODE", PV_MODE, S7_P_RPMODE, 0, 3}}},
    [PG_TUNE] = {"TUNE",
                 4,
                 {{"ROOT", PV_NOTE, S7_P_RECNP, 24, 96},
                  {"FINE", PV_SIGNED, S7_P_SFNTUN, -16, 16},
                  {"RATE", PV_BYTE, S7_P_RECBW, 36, 77},
                  {"", PV_BYTE, 0, 0, -1}}},
    [PG_KEYS] = {"KEYS",
                 4,
                 {{"LO", PV_KEYL, 0, 0, 99}, {"HI", PV_KEYH, 0, 0, 99}, {"TRN", PV_TRANSP, 0, -60, 60},
                  {"CON", PV_CONST, 0, 0, 1}}},
    [PG_OUT1] = {"OUT",
                 4,
                 {{"FILT", PV_BYTE, S7_P_FLTFRQ, 0, 99},
                  {"REL", PV_BYTE, S7_P_RLEASE, 0, 99},
                  {"LEV", PV_BYTE, S7_P_LDNSS, 0, 99},
                  {"", PV_BYTE, 0, 0, -1}}},
    [PG_OUT2] = {"VELOCITY",
                 4,
                 {{"V>FL", PV_BYTE, S7_P_VELFIL, 0, 99},
                  {"V>RL", PV_BYTE, S7_P_VELRLS, 0, 99},
                  {"V>LV", PV_BYTE, S7_P_VELLDN, 0, 99},
                  {"", PV_BYTE, 0, 0, -1}}},
    [PG_LFO] = {"LFO",
                4,
                {{"SPD", PV_BYTE, S7_P_LFSP, 0, 99},
                 {"DPT", PV_BYTE, S7_P_LFDP, 0, 99},
                 {"DLY", PV_BYTE, S7_P_LFDL, 0, 99},
                 {"PRS", PV_PRESSURE, 0, 0, 99}}},
};
static const uint8_t MODE_CODE[4] = {S7_ONESHOT, S7_LOOP, S7_ALT, S7_DRUM};
static const char *const MODE_NAME[4] = {"ONE", "LOOP", "ALT", "DRUM"};

static int32_t pv_get(const s7_slot_t *s, const pval_t *v, uint32_t prog)
{
    const uint8_t *p = s->p;
    uint32_t t = p[S7_P_TRANSP(prog)], i;
    switch (v->kind) {
    case PV_SIGNED:
        return (int8_t)p[v->k];
    case PV_WORD:
        return (int32_t)s7_word(p, v->k);
    case PV_MODE:
        for (i = 0; i < 4u; i++)
            if (MODE_CODE[i] == p[v->k])
                return (int32_t)i;
        return 0;
    case PV_KEYL:
        return p[S7_P_KEYL(prog)];
    case PV_KEYH:
        return p[S7_P_KEYH(prog)];
    case PV_TRANSP:
        return (int32_t)(t & 0x7Fu) - ((t & 0x40u) ? 128 : 0);
    case PV_CONST:
        return (int32_t)(t >> 7);
    case PV_PRESSURE:
        return s->pressure;
    default:
        return p[v->k];
    }
}

/* a value set, the dependent ones kept right: START under END - 200, the loop under END, the two kept words */
static void pv_set(s7_slot_t *s, const pval_t *v, uint32_t prog, int32_t x)
{
    uint8_t *p = s->p;
    uint32_t t = p[S7_P_TRANSP(prog)];
    if (x < v->min)
        x = v->min;
    if (x > v->max)
        x = v->max;
    switch (v->kind) {
    case PV_SIGNED:
        p[v->k] = (uint8_t)(int8_t)x;
        break;
    case PV_WORD: {
        uint32_t st = s7_word(p, S7_P_SSTRT), en = s7_word(p, S7_P_SEND), lp = s7_word(p, S7_P_SLOOP);
        uint32_t words = s->words;
        if (v->k == S7_P_SEND) {
            en = (uint32_t)x > words ? words : (uint32_t)x;
            if (en < 200u)
                en = words < 200u ? words : 200u;
            if (st + 200u > en)
                st = en > 200u ? en - 200u : 0u;
        } else if (v->k == S7_P_SSTRT) {
            st = (uint32_t)x;
            if (st + 200u > en)
                st = en > 200u ? en - 200u : 0u;
        } else {
            lp = (uint32_t)x;
        }
        if (lp + 1u > en)
            lp = en ? en - 1u : 0u;
        s7_set_word(p, S7_P_SSTRT, st);
        s7_set_word(p, S7_P_SEND, en);
        s7_set_word(p, S7_P_SLOOP, lp);
        s7_set_word(p, S7_P_LEN1, en > st ? en - st - 1u : 0u);
        s7_set_word(p, S7_P_LEN2, en > lp ? en - lp - 1u : 0u);
        break;
    }
    case PV_MODE:
        p[v->k] = MODE_CODE[x & 3];
        break;
    case PV_TRANSP:            /* (LO and HI: keys_turn) */
        p[S7_P_TRANSP(prog)] = (uint8_t)((t & 0x80u) | ((uint32_t)x & 0x7Fu));
        break;
    case PV_CONST:
        p[S7_P_TRANSP(prog)] = (uint8_t)((t & 0x7Fu) | (x ? 0x80u : 0u));
        break;
    case PV_PRESSURE:
        s->pressure = (uint8_t)x;
        break;
    default:
        p[v->k] = (uint8_t)x;
        break;
    }
}

static void pv_text(char *b, const s7_slot_t *s, const pval_t *v, uint32_t prog)
{
    int32_t x = pv_get(s, v, prog);
    switch (v->kind) {
    case PV_SIGNED:
    case PV_TRANSP:
        fmt_signed(b, x, 3);
        break;
    case PV_WORD:
        fmt_uint(b, (uint32_t)x, 5, '0');
        break;
    case PV_NOTE:
    case PV_KEYL:
    case PV_KEYH:
        note_text(b, (uint32_t)x);
        break;
    case PV_MODE:
        b[0] = 0;
        str_cat(b, MODE_NAME[x & 3]);
        break;
    case PV_CONST:
        b[0] = x ? 'Y' : 'N';
        b[1] = 0;
        break;
    default:
        fmt_uint(b, (uint32_t)x, 2, '0');
        break;
    }
}

/* the page on the LCD: the labels above, the values under them */
static void page_s700_draw(uint32_t pg, uint32_t slot, uint32_t prog)
{
    const page_t *g = &PAGES[pg];
    const s7_slot_t *s = &s7_slot[slot];
    char top[LCD_COLS + 1], bot[LCD_COLS + 1], t[12];
    uint32_t c, w = LCD_COLS / g->ncols, i;
    for (i = 0; i < LCD_COLS; i++)
        top[i] = bot[i] = ' ';
    top[LCD_COLS] = bot[LCD_COLS] = 0;
    for (c = 0; c < g->ncols; c++) {
        const pval_t *v = &g->v[c];
        if (v->max < v->min)
            continue;
        for (i = 0; v->label[i] && i < w; i++)
            top[c * w + i] = v->label[i];
        pv_text(t, s, v, prog);
        if ((v->kind == PV_KEYL || v->kind == PV_KEYH) && s->p[S7_P_KEYL(prog)] > s->p[S7_P_KEYH(prog)]) {
            t[0] = t[1] = '-';      /* not in this program (KEYL > KEYH, as the S700 leaves it) */
            t[2] = 0;
        }
        for (i = 0; t[i] && i + (w > 4u ? 0u : 1u) < w; i++)   /* a space between four columns */
            bot[c * w + i] = t[i];
    }
    /* the slot at the top right, where the page has room */
    if (g->ncols == 2u || g->v[3].max < g->v[3].min) {
        fmt_uint(t, slot + 1u, 2, '0');
        top[LCD_COLS - 3] = 'S';
        top[LCD_COLS - 2] = t[0];
        top[LCD_COLS - 1] = t[1];
        if (g->ncols == 4u) {      /* the fourth column free: the program under it */
            fmt_uint(t, prog + 1u, 2, '0');
            bot[LCD_COLS - 3] = 'P';
            bot[LCD_COLS - 2] = t[0];
            bot[LCD_COLS - 1] = t[1];
        }
    }
    lcd_row(0, top);
    lcd_row(1, bot);
}

/* LO or HI turned: the slot's key range in the program. Out of the program (KEYL > KEYH, shown "--"):
 *  - as the sampler leaves it (24 and 0, or any LO more than one above HI) the first detent of either puts the slot in
 *    with the whole keyboard, C0-D#6 (24-99, as slot 1 is in P1 after switching on), the rest of the turn going on
 *    from there;
 *  - LO turned one past HI, or HI one under LO, takes it out; back one detent puts it in again with the same keys.
 * 1 = changed */
#define KEYS_LO 24
#define KEYS_HI 99
static int keys_turn(s7_slot_t *s, uint32_t prog, int high, int32_t d)
{
    int32_t lo = s->p[S7_P_KEYL(prog)], hi = s->p[S7_P_KEYH(prog)];
    uint32_t f;
    if (!d)
        return 0;
    if (lo > hi + 1) {
        lo = KEYS_LO;
        hi = KEYS_HI;
        d -= d > 0 ? 1 : -1;
    }
    if (high) {
        hi += d;
        hi = hi > 99 ? 99 : hi < lo - 1 ? lo - 1 : hi;
        hi = hi < 0 ? 0 : hi;
    } else {
        lo += d;
        lo = lo < 0 ? 0 : lo > hi + 1 ? hi + 1 : lo;
        lo = lo > 99 ? 99 : lo;
    }
    if (lo == s->p[S7_P_KEYL(prog)] && hi == s->p[S7_P_KEYH(prog)])
        return 0;
    f = plat_lock();              /* (the audio interrupt reads them at a note on) */
    s->p[S7_P_KEYL(prog)] = (uint8_t)lo;
    s->p[S7_P_KEYH(prog)] = (uint8_t)hi;
    plat_unlock(f);
    return 1;
}

/* an encoder turned on a sampler page: column c's value by d detents (faster turns go further on the points) */
static int page_s700_turn(uint32_t pg, uint32_t slot, uint32_t prog, uint32_t c, int32_t d)
{
    const page_t *g = &PAGES[pg];
    const pval_t *v;
    s7_slot_t *s = &s7_slot[slot];
    int32_t x;
    uint32_t f;
    if (c >= g->ncols)
        return 0;
    v = &g->v[c];
    if (v->max < v->min)
        return 0;
    if (v->kind == PV_KEYL || v->kind == PV_KEYH) {
        if (!keys_turn(s, prog, v->kind == PV_KEYH, d))
            return 0;
        s7mem_touch();
        return 1;
    }
    if (v->kind == PV_WORD)       /* 32752 words: a detent moves 1, fast turns 10 or 100 a detent */
        d *= (d > 2 || d < -2) ? 100 : (d > 1 || d < -1) ? 10 : 1;
    x = pv_get(s, v, prog) + d;
    f = plat_lock();              /* (the audio interrupt reads the values at a note on) */
    pv_set(s, v, prog, x);
    plat_unlock(f);
    s7mem_touch();
    return 1;
}
