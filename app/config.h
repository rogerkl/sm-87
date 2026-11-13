/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Roger Klaveness */
/* SM-87's build options */
#pragma once

/* The effect: the Alesis MIDIVerb's 63 programs (fx/), on sends from the sampler's slots and the drums' pads. 0
 * leaves it all out: its code (about 95 KB of flash, room the samples built in then have), its 32 KB of memory and
 * its share of the audio interrupt */
#ifndef SM87_FX
#define SM87_FX 1
#endif
