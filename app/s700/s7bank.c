/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the sample bank, see s7bank.h */
#include "s7bank.h"
#include <stddef.h>

_Static_assert(sizeof(s7_bank_entry_t) == 188u, "a bank entry: 188 bytes");
_Static_assert(sizeof(s7_bank_sound_t) == 16u, "a kit sound: 16 bytes");
_Static_assert(sizeof(s7_bank_pad_t) == 16u, "a kit pad: 16 bytes");
_Static_assert(sizeof(s7_bank_foot_t) == 32u, "the bank's footer: 32 bytes");

uint32_t s7bank_crc32(const void *p, uint32_t n)
{
    /* zlib's (reflected 0xEDB88320), four bits a step: a small table, about 15 ms for the whole slot at boot */
    static const uint32_t T[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
                                   0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                   0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

/* packed words at `at`, for so many words, inside a bank of size bytes whose entries take head */
static int words_in(uint32_t at, uint32_t words, uint32_t head, uint32_t size)
{
    return words && words <= S7_MAXWORDS && at >= head && !(at & 3u) && at <= size && S7_PACKED(words) <= size - at;
}

/* a name of up to chars characters in len bytes: NULs from there on */
static int name_ok(const char *n, uint32_t chars, uint32_t len)
{
    for (; chars < len; chars++)
        if (n[chars])
            return 0;
    return 1;
}

int s7bank_parse(const uint8_t *top, uint32_t room, s7_bank_t *b)
{
    const s7_bank_foot_t *f;
    const uint8_t *base;
    uint32_t i, head;
    s7_bank_t t = {0, 0, 0, 0, 0, 0};
    b->base = 0;
    b->n = b->id = b->kit_sounds = b->kit_pads = b->kit_id = 0;
    if (!top || room < S7B_FOOT || ((uintptr_t)top & 3u))
        return 0;
    f = (const s7_bank_foot_t *)(const void *)(top - S7B_FOOT);
    if (f->magic != S7B_MAGIC)
        return 0;                 /* (0xFF: none put in) */
    if (s7bank_crc32(f, offsetof(s7_bank_foot_t, fcrc)) != f->fcrc || f->version != S7B_VERSION ||
        f->count > S7_SLOTS || f->kit_sounds > S7B_KIT_SOUNDS || f->kit_pads > S7B_KIT_PADS || (f->size & 3u) ||
        f->size > room - S7B_FOOT)
        return -1;
    head = f->count * (uint32_t)sizeof(s7_bank_entry_t) + f->kit_sounds * (uint32_t)sizeof(s7_bank_sound_t) +
           f->kit_pads * (uint32_t)sizeof(s7_bank_pad_t);
    base = top - S7B_FOOT - f->size;
    if (head > f->size || s7bank_crc32(base, f->size) != f->crc)
        return -1;
    t.base = base;
    t.n = f->count;
    t.kit_sounds = f->kit_sounds;
    t.kit_pads = f->kit_pads;
    for (i = 0; i < t.n; i++) {
        const s7_bank_entry_t *e = s7bank_entry(&t, i);
        if (e->slot >= S7_SLOTS || !words_in(e->at, e->words, head, f->size) ||
            !name_ok(e->name, S7B_NAME - 1u, S7B_NAME))
            return -1;
    }
    for (i = 0; i < t.kit_sounds; i++) {
        const s7_bank_sound_t *s = s7bank_sound(&t, i);
        if (!words_in(s->at, s->words, head, f->size) || s->rate < S7B_RATE_LO || s->rate > S7B_RATE_HI ||
            s->vel > 99u || !name_ok(s->name, 6u, S7B_KIT_NAME))
            return -1;
    }
    for (i = 0; i < t.kit_pads; i++) {
        const s7_bank_pad_t *p = s7bank_pad(&t, i);
        if ((p->sound != 0xFFu && p->sound >= t.kit_sounds + S7_SLOTS) || p->tune < -24 || p->tune > 24 ||
            p->level > 99u || p->pan < -3 || p->pan > 3 || p->note > 127u || p->group > 9u || p->poly < 1u ||
            p->poly > 2u || !name_ok(p->name, 6u, S7B_KIT_NAME))
            return -1;
    }
    *b = t;
    b->id = f->crc ? f->crc : 1u;
    if (t.kit_sounds || t.kit_pads) {
        uint32_t c = s7bank_crc32(base + t.n * sizeof(s7_bank_entry_t),
                                  t.kit_sounds * (uint32_t)sizeof(s7_bank_sound_t) +
                                      t.kit_pads * (uint32_t)sizeof(s7_bank_pad_t));
        b->kit_id = c ? c : 1u;
    }
    return (int)f->count;
}
