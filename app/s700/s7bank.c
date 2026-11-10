/* SM-87: the sample bank, see s7bank.h */
#include "s7bank.h"
#include <stddef.h>

_Static_assert(sizeof(s7_bank_entry_t) == 188u, "a bank entry: 188 bytes");
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

int s7bank_parse(const uint8_t *top, uint32_t room, s7_bank_t *b)
{
    const s7_bank_foot_t *f;
    const uint8_t *base;
    uint32_t i, head;
    b->base = 0;
    b->n = 0;
    b->id = 0;
    if (!top || room < S7B_FOOT || ((uintptr_t)top & 3u))
        return 0;
    f = (const s7_bank_foot_t *)(const void *)(top - S7B_FOOT);
    if (f->magic != S7B_MAGIC)
        return 0;                 /* (0xFF: none put in) */
    if (s7bank_crc32(f, offsetof(s7_bank_foot_t, fcrc)) != f->fcrc || f->version != S7B_VERSION ||
        f->count > S7_SLOTS || (f->size & 3u) || f->size > room - S7B_FOOT)
        return -1;
    head = f->count * (uint32_t)sizeof(s7_bank_entry_t);
    base = top - S7B_FOOT - f->size;
    if (head > f->size || s7bank_crc32(base, f->size) != f->crc)
        return -1;
    for (i = 0; i < f->count; i++) {
        const s7_bank_entry_t *e = (const s7_bank_entry_t *)(const void *)base + i;
        if (e->slot >= S7_SLOTS || !e->words || e->words > S7_MAXWORDS || e->at < head || (e->at & 3u) ||
            e->at > f->size || S7_PACKED(e->words) > f->size - e->at || e->name[S7B_NAME - 1u])
            return -1;
    }
    b->base = base;
    b->n = f->count;
    b->id = f->crc ? f->crc : 1u;
    return (int)f->count;
}
