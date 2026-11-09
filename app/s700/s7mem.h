/* SM-87: the sample memory. Samples live packed (12 bits, two words in three bytes) in the flash, read in place by
 * the engine; their parameters in RAM (s7_slot[].p), kept in a store object with the directory.
 *
 * The pool is the flash from 0x93000 (XDATA0, then the data area) to the store objects at 0xDC000: 73 sectors of
 * 4 KiB, 299,008 bytes, about 199,000 words: six full length samples (32,752 words, 12 sectors each), as many slots
 * as the S700 has, or all 16 slots with shorter ones. A sample takes whole sectors, first fit. Main loop only. */
#pragma once
#include <stdint.h>
#include "s7.h"

#define S7M_BASE 0x93000u
#define S7M_END 0xDC000u
#define S7M_SECTOR 4096u
#define S7M_SECTORS ((S7M_END - S7M_BASE) / S7M_SECTOR)
#define S7M_SECTORS_FOR(words) ((S7_PACKED(words) + S7M_SECTOR - 1u) / S7M_SECTOR)

/* samples built into the firmware (tools/mkrom.py writes app/rom/rom_samples.h; a build without it has none): the
 * slots' factory contents, played in place from the firmware's flash. A dump sent to such a slot replaces it */
typedef struct {
    uint8_t slot, pressure;
    uint16_t words;
    const uint8_t *data;          /* packed */
    const char *name;
    uint8_t p[S7_NPARAM];         /* its SPRM values */
} s7_rom_t;
uint32_t s7mem_rom_count(void);
int s7mem_is_rom(uint32_t slot);  /* the slot plays a built-in sample */
const char *s7mem_name(uint32_t slot);   /* a built-in sample's name, else 0 */

void s7mem_init(void);            /* boot: the directory (or the X7000's empty memory), the slots pointed at */
/* a sample into a slot, written to the flash (erases: the sound stops for a moment). The slot's voices stop first.
 * packed: S7_PACKED(words) bytes. Its parameters are not changed. 0 = done, -1 no room, -2 flash failed */
int s7mem_write(uint32_t slot, const uint8_t *packed, uint32_t words);
void s7mem_clear(uint32_t slot);  /* the slot back to the test tone, in no program; its sectors free */
uint32_t s7mem_free_sectors(void);
uint32_t s7mem_largest_free(void);   /* sectors in the largest free run: the longest sample that fits now */
uint32_t s7mem_words_in(uint32_t sectors);
int s7mem_has_sample(uint32_t slot);  /* a sample of its own (not the test tone) */
/* the directory and every slot's parameters into the store (a flash write). 0 = saved */
int s7mem_save(void);
void s7mem_touch(void);           /* a parameter changed: SAVE lights */
int s7mem_dirty(void);
