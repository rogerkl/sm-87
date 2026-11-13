/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87: the effect, the Alesis MIDIVerb (1986), on a send. Its 63 programs as Eric Brombaugh compiled them from its
 * ROM (mv_progs.c, fx/LICENSE) run at the MIDIVerb's own rate, between its input and output filters as his
 * MIDIVerb_RE models them. In, mono (the MIDIVerb's two inputs summed); out, stereo. Only with SM87_FX (config.h) */
#pragma once
#include <stdint.h>

#define FX_PROGS 63u
#define FX_RATE 23437.5f          /* its rate: 3 MHz, 128 steps a sample (not measured on one: g081_audio runs at
                                   * 23,529 Hz) */

void fx_init(float fs);           /* fs: the FM-1's rate, 44,117.6 Hz (the filters are made for it) */
/* the program, 0..62 (the MIDIVerb's 1..63): it fades out, its memory is cleared, and the new one fades in (some
 * 35 ms; as g081_audio does it). The audio interrupt's, or inside plat_lock */
void fx_program(uint32_t prog);
uint32_t fx_program_now(void);    /* the one playing, or coming in */
const char *fx_name(uint32_t prog);   /* its name as the program chart has it: "2.0 M Wm" (2 s, medium, warm) */
/* the return: its output into the mix, 0..99 as a fader; 0 or off, it isn't run at all (nothing to hear: no time
 * taken), and its memory is cleared as it comes on again. Inside plat_lock */
void fx_set(uint32_t ret, int on);
/* the audio interrupt's: the send (mono, n frames, in the mix's units: 4096 full scale) through it, its output added
 * to l and r */
void fx_render(const float *send, float *l, float *r, uint32_t n);
