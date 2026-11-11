/* SM-87: the sample memory, see s7mem.h */
#include "s7mem.h"
#include "s7bank.h"
#include "app.h"

#define DIR_MAGIC 0x37445331u     /* "1SD7" */
#define DIR_VERSION 2u
#define NONE 0xFFFFu
#define ROM 0xF000u               /* sector ROM | i: built-in sample i */

/* the samples built in: the bank at the top of the firmware's slot (s7bank.h, tools/mkrom.py). The host simulator and
 * the browser emulator have no slot: theirs is app/rom/emu_bank.h while it exists (tools/mkrom.py --emu) */
static s7_bank_t bank;
#ifdef PLAT_HOST
#if defined(__has_include)
#if __has_include("rom/emu_bank.h")
#include "rom/emu_bank.h"
#define EMU_BANK 1
#endif
#endif
static void bank_open(void)
{
#ifdef EMU_BANK
    s7bank_parse(S7_EMU_BANK + sizeof S7_EMU_BANK, (uint32_t)sizeof S7_EMU_BANK, &bank);
#else
    s7bank_parse(0, 0, &bank);
#endif
}
#else
extern const uint8_t _data_load[], _data_start[], _data_end[], _app_xip_end[];   /* (fm1-base device/app.ld) */
__attribute__((section(".rodata.s7bank_desc"))) const s7_bank_desc_t s7bank_desc = {
    S7B_DESC_MAGIC, S7B_VERSION, &s7bank_desc, _data_load, _data_start, _data_end, _app_xip_end};
static void bank_open(void)
{
    const s7_bank_desc_t *volatile dp = &s7bank_desc;    /* (read as the tools read it: kept in the image) */
    const s7_bank_desc_t *d = dp;
    uintptr_t end = (uintptr_t)d->data_load + ((uintptr_t)d->data_end - (uintptr_t)d->data_start);
    uintptr_t top = (uintptr_t)d->slot_end;
    s7bank_parse((const uint8_t *)top, top > end ? (uint32_t)(top - end) : 0u, &bank);
}
#endif

const s7_bank_t *s7mem_bank(void)
{
    static uint8_t opened;
    if (!opened) {
        bank_open();
        opened = 1;
    }
    return &bank;
}

uint32_t s7mem_rom_count(void) { return bank.n; }

typedef struct {
    uint16_t sector;              /* the first sector, from S7M_BASE; NONE: no sample of its own */
    uint16_t words;
    uint8_t pressure;
    uint8_t rsv;
    uint8_t p[S7_NPARAM];
    uint8_t pad;
} dir_slot_t;

typedef struct {
    uint32_t magic;
    uint16_t version, nslots;
    uint32_t rom_id;              /* the built-in samples it was saved with */
    dir_slot_t slot[S7_SLOTS];
} dir_t;                          /* 12 + 16 x 162 = 2604 bytes: one store object */

static dir_t dir;
static uint8_t dirty;
/* the slots' sectors as the store has them, until it is saved again: a sample cleared or replaced since is free in
 * dir, but a power cut would bring back the store's directory, pointing at it (s7mem_write) */
static uint16_t kept_sector[S7_SLOTS], kept_words[S7_SLOTS];

static void kept_take(void)
{
    uint32_t i;
    for (i = 0; i < S7_SLOTS; i++) {
        kept_sector[i] = dir.slot[i].sector;
        kept_words[i] = dir.slot[i].words;
    }
}

/* the X7000's test tone, in every slot nothing was loaded into: 360 words looping at rate code 48 (7849 Hz), root
 * C3. Ours: 12 cycles of 30 words, so C3 plays at 262 Hz; a mellow wave (a sine and two quieter harmonics). */
#define TONE_WORDS 360u
static uint8_t tone[S7_PACKED(TONE_WORDS)];

static void tone_make(void)
{
    /* one cycle, 30 words, offset binary: 2048 + 1400 sin + 350 sin 2x + 120 sin 3x (made once with a table of sines) */
    static const int16_t SIN30[30] = {0,    209,  407,  588,  743,  866,  951,  995,  995,  951,  866,  743,  588,  407,  209,
                                      0,    -209, -407, -588, -743, -866, -951, -995, -995, -951, -866, -743, -588, -407, -209};
    uint32_t i;
    for (i = 0; i < TONE_WORDS; i++) {
        uint32_t c = i % 30u;
        int32_t s = 1400 * SIN30[c] + 350 * SIN30[(2u * c) % 30u] + 120 * SIN30[(3u * c) % 30u];
        s7_put_w12(tone, i, (uint32_t)(2048 + s / 1000));
    }
}

static const uint8_t *sector_ptr(uint32_t sector) { return plat_data(S7M_BASE + sector * S7M_SECTOR); }

static int is_rom(uint32_t sector) { return sector != NONE && sector >= ROM; }

static void slot_point(uint32_t i)
{
    s7_slot_t *s = &s7_slot[i];
    if (is_rom(dir.slot[i].sector)) {
        s->data = s7bank_data(&bank, dir.slot[i].sector - ROM);
        s->words = s7bank_entry(&bank, dir.slot[i].sector - ROM)->words;
    } else if (dir.slot[i].sector != NONE && dir.slot[i].words) {
        s->data = sector_ptr(dir.slot[i].sector);
        s->words = dir.slot[i].words;
    } else {
        s->data = tone;
        s->words = TONE_WORDS;
    }
}

static int dir_valid(void)
{
    uint32_t i, used[(S7M_SECTORS + 31u) / 32u] = {0};
    if (dir.magic != DIR_MAGIC || dir.version != DIR_VERSION || dir.nslots != S7_SLOTS)
        return 0;
    for (i = 0; i < S7_SLOTS; i++) {
        uint32_t s = dir.slot[i].sector, n, k;
        if (s == NONE)
            continue;
        if (is_rom(s)) {          /* another build's built-in samples: checked below (s7mem_init) */
            if (dir.rom_id == bank.id &&
                (s - ROM >= bank.n || dir.slot[i].words != s7bank_entry(&bank, s - ROM)->words))
                return 0;
            continue;
        }
        n = S7M_SECTORS_FOR(dir.slot[i].words);
        if (!dir.slot[i].words || dir.slot[i].words > S7_MAXWORDS || s + n > S7M_SECTORS)
            return 0;
        for (k = s; k < s + n; k++) {      /* no two samples in one sector */
            if (used[k / 32u] >> (k % 32u) & 1u)
                return 0;
            used[k / 32u] |= 1u << (k % 32u);
        }
    }
    return 1;
}

static void dir_empty(void)
{
    uint32_t i;
    dir.magic = DIR_MAGIC;
    dir.version = DIR_VERSION;
    dir.nslots = S7_SLOTS;
    for (i = 0; i < S7_SLOTS; i++) {
        dir.slot[i].sector = NONE;
        dir.slot[i].words = TONE_WORDS;
        dir.slot[i].pressure = 0;
        s7_default_params(dir.slot[i].p);
    }
    /* as the X7000 after switching on: slot 1 in P1, keys 24-99 */
    dir.slot[0].p[S7_P_KEYL(0)] = 24;
    dir.slot[0].p[S7_P_KEYH(0)] = 99;
}

/* the built-in samples into the slots that hold nothing (the test tone): as they come, with their parameters */
static void rom_fill(void)
{
    uint32_t i, k;
    for (i = 0; i < bank.n; i++) {
        const s7_bank_entry_t *e = s7bank_entry(&bank, i);
        dir_slot_t *d = &dir.slot[e->slot % S7_SLOTS];
        if (d->sector != NONE)
            continue;
        d->sector = (uint16_t)(ROM + i);
        d->words = e->words;
        d->pressure = e->pressure;
        for (k = 0; k < S7_NPARAM; k++)
            d->p[k] = e->p[k];
    }
    if (bank.n && is_rom(dir.slot[0].sector) == 0 && dir.slot[0].sector == NONE)
        for (k = 0; k < S7_PROGRAMS; k++)   /* the test tone keeps out of the way of the built-in samples */
            dir.slot[0].p[S7_P_KEYH(k)] = 0, dir.slot[0].p[S7_P_KEYL(k)] = 24;
    dir.rom_id = bank.id;
}

void s7mem_init(void)
{
    uint32_t i;
    tone_make();
    s7mem_bank();
    if (plat_store_load(OBJ_SAMPLES, &dir, sizeof dir) != (int)sizeof dir || !dir_valid()) {
        dir_empty();
        rom_fill();
    } else if (dir.rom_id != bank.id) {
        /* another build's built-in samples: those slots empty again, then this build's into the empty ones */
        for (i = 0; i < S7_SLOTS; i++)
            if (is_rom(dir.slot[i].sector)) {
                dir.slot[i].sector = NONE;
                dir.slot[i].words = TONE_WORDS;
                dir.slot[i].pressure = 0;
                s7_default_params(dir.slot[i].p);
            }
        rom_fill();
    }
    for (i = 0; i < S7_SLOTS; i++) {
        uint32_t k;
        for (k = 0; k < S7_NPARAM; k++)
            s7_slot[i].p[k] = dir.slot[i].p[k];
        s7_slot[i].pressure = dir.slot[i].pressure;
        slot_point(i);
    }
    /* as the store has them (a built-in sample's slot changed above takes no sector): nothing at all when the store
     * held no directory of ours */
    kept_take();
    dirty = 0;
}

static int sector_used(uint32_t k, uint32_t except)
{
    uint32_t i;
    for (i = 0; i < S7_SLOTS; i++) {
        uint32_t s = dir.slot[i].sector;
        if (i != except && s != NONE && !is_rom(s) && k >= s && k < s + S7M_SECTORS_FOR(dir.slot[i].words))
            return 1;
    }
    return 0;
}

/* the store's directory points at one of sectors at .. at + n - 1 */
static int kept_in(uint32_t at, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < S7_SLOTS; i++) {
        uint32_t s = kept_sector[i];
        if (s != NONE && !is_rom(s) && s < at + n && at < s + S7M_SECTORS_FOR(kept_words[i]))
            return 1;
    }
    return 0;
}

/* the first run of n free sectors (the slot's own counted free), or -1 */
static int32_t find_run(uint32_t n, uint32_t except)
{
    uint32_t k, run = 0;
    for (k = 0; k < S7M_SECTORS; k++) {
        run = sector_used(k, except) ? 0u : run + 1u;
        if (run == n)
            return (int32_t)(k + 1u - n);
    }
    return -1;
}

uint32_t s7mem_free_sectors(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < S7M_SECTORS; k++)
        n += !sector_used(k, S7_SLOTS);
    return n;
}

uint32_t s7mem_largest_free(void)
{
    uint32_t k, run = 0, best = 0;
    for (k = 0; k < S7M_SECTORS; k++) {
        run = sector_used(k, S7_SLOTS) ? 0u : run + 1u;
        if (run > best)
            best = run;
    }
    return best;
}

uint32_t s7mem_words_in(uint32_t sectors)
{
    uint32_t w = (sectors * S7M_SECTOR - 1u) / 3u * 2u;
    return w > S7_MAXWORDS ? S7_MAXWORDS : w;
}

int s7mem_has_sample(uint32_t slot) { return slot < S7_SLOTS && dir.slot[slot].sector != NONE; }
int s7mem_is_rom(uint32_t slot) { return slot < S7_SLOTS && is_rom(dir.slot[slot].sector); }
const char *s7mem_name(uint32_t slot)
{
    return s7mem_is_rom(slot) ? s7bank_entry(&bank, dir.slot[slot].sector - ROM)->name : 0;
}

static void sync_params_to_dir(void)
{
    uint32_t i, k;
    for (i = 0; i < S7_SLOTS; i++) {
        for (k = 0; k < S7_NPARAM; k++)
            dir.slot[i].p[k] = s7_slot[i].p[k];
        dir.slot[i].pressure = s7_slot[i].pressure;
    }
}

int s7mem_save(void)
{
    sync_params_to_dir();
    if (plat_store_save(OBJ_SAMPLES, &dir, sizeof dir) != 0)
        return -1;
    kept_take();
    dirty = 0;
    return 0;
}

void s7mem_touch(void) { dirty = 1; }
int s7mem_dirty(void) { return dirty; }

/* the slot's voices stopped and its pointer moved, with the audio interrupt held off */
static void slot_swap(uint32_t slot, uint16_t sector, uint16_t words)
{
    uint32_t f = plat_lock();
    s7_kill_slot(slot);
    dir.slot[slot].sector = sector;
    dir.slot[slot].words = words;
    slot_point(slot);
    plat_unlock(f);
}

int s7mem_write(uint32_t slot, const uint8_t *packed, uint32_t words)
{
    uint32_t n, bytes, k;
    int32_t at;
    static uint8_t sec[S7M_SECTOR];
    if (slot >= S7_SLOTS || !words)
        return -1;
    if (words > S7_MAXWORDS)
        words = S7_MAXWORDS;
    bytes = S7_PACKED(words);
    n = S7M_SECTORS_FOR(words);
    /* the slot's old sample is let go first: its sectors may take the new one */
    slot_swap(slot, NONE, TONE_WORDS);
    at = find_run(n, S7_SLOTS);
    if (at < 0)
        return -1;
    /* sectors the store's directory still points at (a sample cleared or replaced since it was saved, this slot's
     * own old one too): the directory saved first, without it. Else a power cut before the next save (3 s after a
     * dump) would bring back a slot pointing at this sample's data; now it finds that slot empty */
    if (kept_in((uint32_t)at, n) && s7mem_save() != 0)
        return -2;
    for (k = 0; k < n; k++) {
        uint32_t off = S7M_BASE + ((uint32_t)at + k) * S7M_SECTOR, m = bytes - k * S7M_SECTOR, i;
        if (m > S7M_SECTOR)
            m = S7M_SECTOR;
        for (i = 0; i < S7M_SECTOR; i++)
            sec[i] = i < m ? packed[k * S7M_SECTOR + i] : 0xFFu;
        /* XDATA0's sectors: the update loader reads "AT" at 0xF06 as an update record (plat.h). One bit of one word
         * changed keeps it from ever reading so; inaudible */
        if (off < PLAT_XDATA0_HI && sec[0xF06] == 'A' && sec[0xF07] == 'T')
            sec[0xF06] ^= 1u;
        if (plat_data_erase(off) != 0 || plat_data_write(off, sec, S7M_SECTOR) != 0)
            return -2;
    }
    slot_swap(slot, (uint16_t)at, (uint16_t)words);
    dirty = 1;
    return 0;
}

void s7mem_clear(uint32_t slot)
{
    uint32_t i;
    if (slot >= S7_SLOTS)
        return;
    slot_swap(slot, NONE, TONE_WORDS);
    {
        uint32_t f = plat_lock();
        s7_default_params(s7_slot[slot].p);
        for (i = 0; i < S7_PROGRAMS; i++)
            s7_slot[slot].p[S7_P_KEYH(i)] = 0;
        plat_unlock(f);
    }
    dirty = 1;
}
