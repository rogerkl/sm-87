/* SM-87: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c) */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SM-87"
#define APP_TAGLINE "MMT-8 + S700"
#define APP_SPLASH_RGB RGB(120, 200, 60)
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */
#define APP_SYSEX                        /* SysEx to the computer (sample dumps out) */
#define APP_SYSEX_BYTE                   /* every byte of a SysEx in: sample dumps longer than a frame */
#define APP_WEB_SX_IN 73728u             /* the browser emulator takes a whole dump in one frame: a sample (66,632 bytes),
                                          * an MMT-8's memory (73,442 at most) */

/* the store: the app's objects and their flash offsets (each an A/B pair of sectors). The sample pool is the flash
 * below 0xDC000 (s700/s7mem.h) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_SAMPLES, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xFC000u, 0xDE000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_sysex(const uint8_t *p, uint32_t n);
void app_sysex_byte(uint8_t b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
