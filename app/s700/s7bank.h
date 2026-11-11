/* SM-87: the sample bank, the samples built into the firmware: at the top of the firmware's slot, above the image,
 * where the package's padding was. tools/mkrom.py puts it into a firmware package (.fwsc) as it is, a release's or a
 * build's, with no compiler (web/sm87bank.js in a browser): the package opened, the bank written, its checksums made
 * again (documentation/sample-bank.md). The firmware finds it at boot (s7mem.c), plays its samples in place and gives
 * them to the slots as their factory contents.
 *
 * A bank may hold a drum kit too: sounds of the HR-16's own, apart from the sampler's slots, and its pads' set-up. It
 * then replaces the kit the firmware was built with (hr16/hrkit.c hr_kit_bank).
 *
 * Little-endian, every part 4-byte aligned. From the bank's first byte:
 *   count samples (s7_bank_entry_t, 188 bytes each)
 *   kit_sounds sounds (s7_bank_sound_t, 16 bytes each), kit_pads pads (s7_bank_pad_t, 16 bytes each)
 *   the packed words of each (S7_PACKED, padded to 4), where their entries say
 *   the footer (s7_bank_foot_t, 32 bytes), its last byte the slot's last
 * A slot without one is all 0xFF: no bank. */
#pragma once
#include <stdint.h>
#include "s7.h"

#define S7B_MAGIC 0x4B423753u     /* "S7BK" */
#define S7B_VERSION 2u
#define S7B_NAME 24u              /* a sample's name: up to 23 characters, then NULs */
#define S7B_KIT_SOUNDS 64u        /* a kit's sounds, at most */
#define S7B_KIT_PADS 16u          /* its pads (hr16/hr.h HR_PADS) */
#define S7B_KIT_NAME 8u           /* a kit sound's or pad's name: up to 6 characters (HR_NAME), then NULs */
#define S7B_RATE_LO 36u           /* the sampler's rate codes (RECBW) */
#define S7B_RATE_HI 76u

typedef struct {
    uint8_t slot, pressure;       /* slot 0-15 */
    uint16_t words;               /* 1 .. S7_MAXWORDS */
    uint32_t at;                  /* its packed words, from the bank's first byte */
    char name[S7B_NAME];
    uint8_t p[S7_NPARAM];         /* its SPRM values */
    uint8_t rsv;
} s7_bank_entry_t;

typedef struct {                  /* a kit's sound: played whole, once, at its rate (the pads tune it) */
    uint16_t words;               /* 1 .. S7_MAXWORDS */
    uint8_t rate;                 /* its rate code, S7B_RATE_LO .. S7B_RATE_HI */
    uint8_t vel;                  /* velocity's say in its level, 0..99 (VELLDN) */
    uint32_t at;                  /* its packed words, from the bank's first byte */
    char name[S7B_KIT_NAME];
} s7_bank_sound_t;

typedef struct {                  /* a pad as the kit sets it up (hr16/hr.h hr_pad_t), the pads in order */
    char name[S7B_KIT_NAME];      /* empty: "PAD nn" */
    uint8_t sound;                /* the kit's 0..kit_sounds-1, then the slots (kit_sounds + 0..15); 0xFF none */
    int8_t tune;                  /* semitones, -24..24 */
    uint8_t level;                /* 0..99 */
    int8_t pan;                   /* -3 (left) .. 3 */
    uint8_t note;                 /* its MIDI note, 0..127 */
    uint8_t group;                /* 0, or a group (0..9): a hit cuts the others of its group */
    uint8_t poly;                 /* its voices at once: 1 or 2 */
    uint8_t rsv;
} s7_bank_pad_t;

typedef struct {
    uint32_t magic;
    uint16_t version, count;
    uint32_t size;                /* the bank's bytes below the footer */
    uint32_t crc;                 /* their CRC-32 (zlib's) */
    uint16_t kit_sounds, kit_pads;   /* a kit: both 0 for none (the firmware's own stays) */
    uint32_t rsv[2];              /* 0 */
    uint32_t fcrc;                /* the CRC-32 of the footer's 28 bytes before it */
} s7_bank_foot_t;

#define S7B_FOOT ((uint32_t)sizeof(s7_bank_foot_t))

/* the firmware tells the tools where its image ends, so a bank goes above it: this, once in the image (s7mem.c).
 * The image ends at data_load + (data_end - data_start) (fm1-base device/app.ld: .data's load image last), the slot
 * at slot_end; self is where this is, so a tool finds the addresses' base from where it found the magic */
#define S7B_DESC_MAGIC "SM87 S7 BANK"
typedef struct {
    char magic[12];               /* S7B_DESC_MAGIC, without its NUL */
    uint32_t version;             /* the bank's format this firmware reads (S7B_VERSION) */
    const void *self;
    const void *data_load, *data_start, *data_end;
    const void *slot_end;
} s7_bank_desc_t;

typedef struct s7_bank_s {
    const uint8_t *base;          /* the bank's first byte; 0: none */
    uint32_t n;                   /* its samples */
    uint32_t id;                  /* its CRC-32 (never 0); 0: none */
    uint32_t kit_sounds, kit_pads;   /* its kit (both 0: none) */
    uint32_t kit_id;              /* the CRC-32 of the kit's entries (never 0); 0: no kit */
} s7_bank_t;

uint32_t s7bank_crc32(const void *p, uint32_t n);
/* the bank ending at top, in the room bytes below it (from the image's end): its samples' count, 0 without a bank,
 * -1 one that doesn't check (b says none) */
int s7bank_parse(const uint8_t *top, uint32_t room, s7_bank_t *b);
static inline const s7_bank_entry_t *s7bank_entry(const s7_bank_t *b, uint32_t i)
{
    return (const s7_bank_entry_t *)(const void *)b->base + i;
}
static inline const uint8_t *s7bank_data(const s7_bank_t *b, uint32_t i) { return b->base + s7bank_entry(b, i)->at; }
static inline const s7_bank_sound_t *s7bank_sound(const s7_bank_t *b, uint32_t i)
{
    return (const s7_bank_sound_t *)(const void *)(b->base + b->n * sizeof(s7_bank_entry_t)) + i;
}
static inline const s7_bank_pad_t *s7bank_pad(const s7_bank_t *b, uint32_t i)
{
    return (const s7_bank_pad_t *)(const void *)(b->base + b->n * sizeof(s7_bank_entry_t) +
                                                 b->kit_sounds * sizeof(s7_bank_sound_t)) + i;
}
