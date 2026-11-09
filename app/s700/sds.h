/* SM-87: the S700/X7000's MIDI: its sample dump (the Prophet 2000's, F0 7E) and Akai's exclusive messages for a
 * sample's parameters (F0 47 ... 41), as documentation/akai-s700-x7000/01-sample-dump.md and 02-sample-parameters.md
 * describe them. The FM-1 answers as an X7000 with 16 slots, so Axs7 sends and fetches its samples unchanged.
 *
 * Pure code (no platform calls): the receiver takes bytes as they come (the USB interrupt), the rest builds and
 * reads messages in buffers. */
#pragma once
#include <stdint.h>
#include "s7.h"

#define SDS_HEADER 19u
#define SDS_BLOCK 122u
#define SDS_WORDS_PER_BLOCK 60u
#define SDS_DUMP_MAX (SDS_HEADER + 546u * SDS_BLOCK + 1u)   /* 66,632 bytes */
#define SDS_BUFS 2u               /* dumps taken while the last is still being written to the flash */
#define SPRM_LEN 319u
#define OVS_LEN 33u
#define AKAI_ID 0x41u             /* the S700's identifier (Akai's "65", decimal) */

enum { SDS_FREE, SDS_FILLING, SDS_READY, SDS_HELD };   /* HELD: lent (sds_hold) */

/* a dump received, into one of the staging buffers */
typedef struct {
    volatile uint8_t state;
    uint8_t slot, bits, alternating;
    uint32_t total;               /* the header's words */
    uint32_t words;               /* words received */
    uint32_t loop_start, loop_end;
    uint32_t period_ns;
    uint32_t blocks, bad_blocks;  /* checksum or block number wrong */
    uint32_t seq;                 /* the order the dumps finished in */
    uint8_t *data;                /* packed words: S7_PACKED(S7_MAXWORDS) bytes */
} sds_dump_t;

extern sds_dump_t sds_buf[SDS_BUFS];
extern volatile uint32_t sds_refused;   /* dumps refused: no buffer free, or reception off (SECRD) */
extern volatile uint8_t sds_enabled;    /* SECRE / SECRD */

void sds_init(uint8_t *mem0, uint8_t *mem1);
void sds_rx_byte(uint8_t b);      /* every byte of a SysEx, F0 and F7 too (realtime bytes left out) */
/* both staging buffers lent, one after the other in memory (the MMT-8's dump, 64 KB): 1 = lent, 0 = one in use (a
 * dump coming or not yet in the flash). A sample dump meanwhile is refused. In the interrupt, or the main loop inside
 * plat_lock */
int sds_hold(void);
void sds_release(void);
uint32_t sds_rx_progress(void);   /* words of the dump coming in now (0: none) */
/* the length a received dump plays: to its loop end (the sampler takes it as the end), within what came */
uint32_t sds_length(const sds_dump_t *d);
/* a slot's parameters after a dump, as the sampler leaves them: START 0, END at the loop end, the loop and its mode,
 * root C3, the rate from the period (when it is one), the rest as they were */
void sds_apply(const sds_dump_t *d, uint8_t *p);
uint32_t sds_rate_code(uint32_t period_ns);   /* the nearest of the 41 rates; 0 if not a sampler's period */

/* a slot as a dump, as the sampler sends one: START to END, the loop from LOOP, the period of its rate code. put
 * gets the bytes in pieces, F0 .. F7; returns the bytes */
uint32_t sds_build(const s7_slot_t *s, uint32_t slot, void (*put)(const uint8_t *p, uint32_t n, void *ctx), void *ctx);
/* the same a piece at a time: the header (returns the blocks to follow), then each block, then F7 */
typedef struct {
    const s7_slot_t *s;
    uint32_t start, n;            /* the words sent: from START, n of them */
} sds_tx_t;
uint32_t sds_tx_header(sds_tx_t *t, const s7_slot_t *s, uint32_t slot, uint8_t hdr[SDS_HEADER]);
void sds_tx_block(const sds_tx_t *t, uint32_t b, uint8_t blk[SDS_BLOCK]);

/* Akai exclusive: p and n the bytes between F0 and F7 */
int akai_is(const uint8_t *p, uint32_t n, uint32_t channel, uint32_t fn);   /* 1: F0 47 cc fn 41 ..., our channel */
void sprm_build(uint8_t out[SPRM_LEN], uint32_t channel, uint32_t slot, const uint8_t *params);
/* an SPRM on our channel: its slot, or -1 (not one, too short, its checksum wrong) */
int32_t sprm_check(const uint8_t *p, uint32_t n, uint32_t channel);
/* its values into params; a value whose second byte is 2 or more is kept, as Akai's spec says */
void sprm_take(const uint8_t *p, uint8_t *params);
/* overall settings */
typedef struct {
    uint8_t channel, mode, ex_channel, omni, bend_range, prog_change_off, chomax, afsens;
} s7_ovs_t;
void ovs_build(uint8_t out[OVS_LEN], uint32_t channel, const s7_ovs_t *o);
int ovs_parse(const uint8_t *p, uint32_t n, s7_ovs_t *o);   /* 0 = taken */
