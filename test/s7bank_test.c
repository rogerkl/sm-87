/* SM-87: the sample bank's reader (app/s700/s7bank.c): a bank as tools/mkrom.py makes it found and read; one that
 * doesn't check refused, so the firmware does without rather than play from a damaged one */
#include <stdio.h>
#include <string.h>
#include "s7bank.h"

static int fails, checks;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            fails++;                                                                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static uint32_t mem[40000];       /* (4-byte aligned) */
static uint8_t *const M = (uint8_t *)mem;

/* the footer after size bytes at M, its CRCs made again */
static uint32_t seal(uint32_t size, uint32_t count)
{
    s7_bank_foot_t *f = (s7_bank_foot_t *)(void *)(M + size);
    memset(f, 0, sizeof *f);
    f->magic = S7B_MAGIC;
    f->version = S7B_VERSION;
    f->count = (uint16_t)count;
    f->size = size;
    f->crc = s7bank_crc32(M, size);
    f->fcrc = s7bank_crc32(f, 28u);
    return size + S7B_FOOT;
}

static void reseal(uint32_t size)
{
    s7_bank_foot_t *f = (s7_bank_foot_t *)(void *)(M + size);
    f->crc = s7bank_crc32(M, size);
    f->fcrc = s7bank_crc32(f, 28u);
}

/* two samples: slot 3, 100 words, "PIANO"; slot 15, 33 words. -> the bytes before the footer */
static uint32_t make(void)
{
    s7_bank_entry_t *e = (s7_bank_entry_t *)(void *)M;
    uint32_t at = 2u * (uint32_t)sizeof *e, i;
    memset(mem, 0xA5, sizeof mem);
    memset(e, 0, 2u * sizeof *e);
    e[0].slot = 3, e[0].pressure = 20, e[0].words = 100, e[0].at = at;
    strcpy(e[0].name, "PIANO");
    for (i = 0; i < S7_NPARAM; i++)
        e[0].p[i] = (uint8_t)i;
    at += (S7_PACKED(100u) + 3u) & ~3u;
    e[1].slot = 15, e[1].words = 33, e[1].at = at;
    strcpy(e[1].name, "12345678901234567890123");
    at += (S7_PACKED(33u) + 3u) & ~3u;
    return at;
}

static int parse(uint32_t len, uint32_t room, s7_bank_t *b) { return s7bank_parse(M + len, room, b); }

int main(void)
{
    s7_bank_t b;
    uint32_t size, len;
    s7_bank_entry_t *e = (s7_bank_entry_t *)(void *)M;
    s7_bank_foot_t *f;

    CHECK(s7bank_crc32("123456789", 9) == 0xCBF43926u, "CRC-32 of 123456789: %08X", s7bank_crc32("123456789", 9));
    CHECK(s7bank_crc32("", 0) == 0u, "CRC-32 of nothing");

    /* none: no slot, too little room, the padding (0xFF) */
    CHECK(s7bank_parse(0, 0, &b) == 0 && !b.base && !b.n && !b.id, "no slot: none");
    memset(mem, 0xFF, sizeof mem);
    CHECK(parse(4096, 4096, &b) == 0 && !b.base && !b.id, "all 0xFF: none");
    CHECK(parse(4096, 31, &b) == 0, "room for no footer: none");

    /* a good one, and read */
    size = make();
    len = seal(size, 2);
    f = (s7_bank_foot_t *)(void *)(M + size);
    CHECK(parse(len, len, &b) == 2 && b.base == M && b.n == 2 && b.id == f->crc, "two samples");
    CHECK(s7bank_entry(&b, 0)->slot == 3 && s7bank_entry(&b, 0)->words == 100 && s7bank_entry(&b, 0)->pressure == 20 &&
              !strcmp(s7bank_entry(&b, 0)->name, "PIANO") && s7bank_entry(&b, 0)->p[154] == 154,
          "the first entry");
    CHECK(s7bank_data(&b, 1) == M + e[1].at && s7bank_entry(&b, 1)->slot == 15, "the second's words");
    CHECK(parse(len, len + 5000, &b) == 2, "more room: the same");
    CHECK(parse(len, len - 1, &b) == -1 && !b.base && !b.n && !b.id, "a byte less room than it takes: refused");
    CHECK(s7bank_parse(M + len + 1, len + 1, &b) == 0, "a top not on a word: none");

    /* the footer */
    f->magic ^= 1u;
    CHECK(parse(len, len, &b) == 0, "another magic: none");
    f->magic ^= 1u;
    f->rsv[1] = 1;
    CHECK(parse(len, len, &b) == -1, "the footer changed, its CRC not: refused");
    f->rsv[1] = 0;
    f->version = 2;
    f->fcrc = s7bank_crc32(f, 28u);
    CHECK(parse(len, len, &b) == -1, "another version: refused");
    seal(size, 17);
    CHECK(parse(len, len, &b) == -1, "17 samples: refused");
    seal(size, 2);
    f->size = size - 2;
    f->fcrc = s7bank_crc32(f, 28u);
    CHECK(s7bank_parse(M + len, len, &b) == -1, "a size not in words: refused");
    seal(size, 2);
    CHECK(parse(len, len, &b) == 2, "sealed again: good");

    /* the body */
    M[e[1].at + 5] ^= 0x10;
    CHECK(parse(len, len, &b) == -1, "a word changed, the CRC not: refused");
    M[e[1].at + 5] ^= 0x10;
    CHECK(parse(len, len, &b) == 2, "changed back: good");

    /* the entries, each sealed so only the check of the entry is left */
#define ENTRY_BAD(stmt, undo, what)                                                                                    \
    do {                                                                                                               \
        stmt;                                                                                                          \
        reseal(size);                                                                                                  \
        CHECK(parse(len, len, &b) == -1 && !b.n, what);                                                                \
        undo;                                                                                                          \
        reseal(size);                                                                                                  \
    } while (0)
    ENTRY_BAD(e[1].slot = 16, e[1].slot = 15, "slot 17: refused");
    ENTRY_BAD(e[0].words = 0, e[0].words = 100, "no words: refused");
    ENTRY_BAD(e[0].words = S7_MAXWORDS + 1u, e[0].words = 100, "more words than a sample has: refused");
    ENTRY_BAD(e[1].words = 35, e[1].words = 33, "words past the bank's end: refused");   /* (34 packs as 33) */
    ENTRY_BAD(e[0].at = 2u * sizeof *e - 4u, e[0].at = 2u * sizeof *e, "words among the entries: refused");
    ENTRY_BAD(e[0].at += 2u, e[0].at -= 2u, "words not on a word: refused");
    ENTRY_BAD(e[1].at = size + 4u, e[1].at = 2u * sizeof *e + ((S7_PACKED(100u) + 3u) & ~3u),
              "words past the end: refused");
    ENTRY_BAD(e[1].name[23] = 'X', e[1].name[23] = 0, "a name without its end: refused");
    CHECK(parse(len, len, &b) == 2, "all put back: good");

    /* the count: a bank of one */
    seal(size, 1);
    CHECK(parse(len, len, &b) == 1 && b.n == 1, "count 1: the first only");
    seal(size, 0);
    CHECK(parse(len, len, &b) == 0 && b.n == 0, "count 0: no samples");

    printf("s7bank_test: %d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}
