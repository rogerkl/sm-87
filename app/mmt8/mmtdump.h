/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the MMT-8's memory as its SysEx dump has it (its SysEx document: documentation/manuals.md): F0 00 00 0E 00,
 * the memory from its address 400H packed seven bytes in eight (a stream of 7-bit bytes, the most significant bit
 * first), F7.
 *
 * The memory: 200H bytes of header (the parts' and songs' addresses, big-endian, +400H; the start and length of the
 * free memory at CFH and D3H, little-endian; bytes the document says not to alter), then the parts 00-99 and the songs
 * 00-99 there are, one after another. A part: its length (with its header), the offsets of tracks 8 .. 1, its beats
 * in BCD, the tracks' channels (8 .. 1), its name, then the tracks 8 .. 1, each to its end-of-track packet. A track's
 * events: a 7-byte packet with the clock (first byte's top bit set), 5-byte ones after it on the same clock. A song:
 * its length, tempo, name, then part and tracks a step, FFH.
 *
 * Two things the document leaves out, from polluxsynth's mmt8utils (a librarian that writes dumps MMT-8s take): the
 * BCD beats are the beats + 1, and a dump made from nothing confuses an MMT-8: the header's other bytes have to come
 * from one of its own dumps. So the header of the last dump taken in is kept (sq.mmt8) and sent back in ours.
 *
 * Pure code: the receiver takes bytes as they come (the USB interrupt); the rest converts in buffers. */
#pragma once
#include <stdint.h>
#include "seq.h"

#define MD_HDR SQ_MMT8_HDR
#define MD_BASE 0x400u            /* the MMT-8's address of the dump's first byte */
#define MD_END 0xFF00u            /* its memory's end */
#define MD_MAX (MD_END - MD_BASE) /* 64,256 bytes: the most a dump holds */
#define MD_PREFIX 5u              /* F0 00 00 0E 00 */
#define MD_PART_HDR 0x2Au
#define MD_SONG_HDR 0x11u

/* what an import kept and did not */
typedef struct {
    uint16_t parts, songs;        /* taken in */
    uint16_t lost_parts, lost_songs;   /* not */
    int16_t first_lost;           /* the first part of them, or -1 */
    /* why, any of: cut, the dump cut short (bytes lost on the way: some of it past the bytes that came, or, when fewer
     * came than its header says it has, not readable); bad, not readable in a whole dump; full, no room in the
     * sequencer's 64 KB (its notes take 7 bytes, the MMT-8's 5 on a clock shared) */
    uint8_t cut, bad, full;
    uint32_t got, want;           /* the bytes that came, and those its header says it has (0: not said) */
    uint32_t skipped;             /* events not kept: SysEx (the sequencer keeps none) */
} md_report_t;

/* a dump's length as its header says it (where the free memory starts, CFH), once n bytes of it have come; 0: not
 * yet, or not said. For the progress shown as it comes in, and to tell a dump cut short */
uint32_t md_want(const uint8_t *img, uint32_t n);

/* an unpacked dump (len bytes from the header) into the sequencer's memory, replacing all of it but the song offsets.
 * scratch: SQ_SCRATCH bytes. 0, or -1: not a dump */
int md_import(const uint8_t *img, uint32_t len, uint8_t *scratch, md_report_t *r);
/* the sequencer's memory as an MMT-8 dump's (unpacked) into img: its length, or -1 when it doesn't fit max (an MMT-8's
 * memory: MD_MAX) */
int32_t md_export(uint8_t *img, uint32_t max);
int md_has_header(void);          /* an MMT-8's header kept: a dump of ours an MMT-8 takes */

/* the packing: the MIDI bytes n data bytes take, and MIDI byte k of them (random access, for sending in pieces) */
uint32_t md_packed_len(uint32_t n);
uint8_t md_packed_byte(const uint8_t *img, uint32_t n, uint32_t k);

/* the receiver: every byte of every SysEx (F0 and F7 too), the dump's unpacked into buf as it comes. The HR-16's
 * dump is packed the same (hr16/hrdump.h): a receiver of its own, with its prefix */
enum { MD_RX_IDLE, MD_RX_PREFIX, MD_RX_DATA, MD_RX_DONE, MD_RX_SKIP };
typedef struct {
    volatile uint8_t st;
    uint8_t pre, bits, over;      /* over: more than buf holds (the dump not taken) */
    uint32_t acc, n, max;
    uint8_t *buf;
    const uint8_t *prefix;        /* the bytes before the data, F0 first (0: the MMT-8's, F0 00 00 0E 00) */
    uint8_t plen;
} md_rx_t;
/* buf: where a dump goes; the app's to give (0: none free now, a dump is refused). Returns 1 while it is one */
void md_rx_byte(md_rx_t *rx, uint8_t b, uint8_t *(*take_buf)(uint32_t *max));
