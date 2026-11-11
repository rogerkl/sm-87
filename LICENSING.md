# Licensing

**SM-87 is MPL-2.0** (the full text is in `LICENSE`), file by file: its source files carry an
`SPDX-License-Identifier: MPL-2.0` header. If you change one of them and distribute it, publish your change to that
file under MPL-2.0. Files of your own that you combine with SM-87 may use any licence.

| What | Licence | Where |
| --- | --- | --- |
| SM-87's code, tools, tests and documentation | MPL-2.0, Copyright (C) 2026 Roger Klaveness | `app/`, `tools/`, `test/`, `web/`, `documentation/`, `README.md` |
| The drum kit's sounds, built into every firmware (Roger Klaveness's) | MPL-2.0, as the rest | `resources/kit/` |
| The illustration on the README and the site, and its square and 2:1 crops, made with Google's Gemini image model from Roger Klaveness's prompt (a word on a prop manual in it retouched out) | MPL-2.0, as the rest | `app/site/img/` |
| fm1-base, the platform SM-87 is built on | MPL-2.0 (its `LICENSING.md` lists its parts' origins) | `fm1-base/`, a git submodule |
| felucca-bsp, fm1-base's board support | MPL-2.0 | `fm1-base/bsp/`, a git submodule of fm1-base |
| fm1-base's changes for the browser emulator's SysEx, until they are in fm1-base | MPL-2.0, as the files they change | `patches/fm1-base-emu-sysex.patch` |

## What a firmware package holds

A firmware package (`sm87.fwsc`, `sm87-X.Y.fwsc`) is built from SM-87, fm1-base and felucca-bsp, which are all
MPL-2.0. It also holds three files from JieLi's AC79 SDK, which every FM-1 package needs: `uboot.boot`,
`cfg_tool.bin` and `eq_cfg_hw.bin`, Copyright (c) Zhuhai Jieli Technology Co., Ltd., under the Apache License 2.0
(`fm1-base/LICENSES/Apache-2.0.txt`). The build reads them from an SDK checkout, and they are never in this
repository. SM-87 is built without Bluetooth (`ble = false`), so none of JieLi's Bluetooth or WiFi libraries are in
it.

The browser emulator's screen reader voice, eSpeak NG, is GPL-3.0-or-later. It is a separate program that the
emulator's page loads, and it is not part of SM-87.

## Not in this repository

The manuals of the machines SM-87 recreates are Alesis's and Akai's, under their copyright.
[documentation/manuals.md](documentation/manuals.md) lists them, with what SM-87 takes from each and where a copy can
be found. SM-87's own documents describe the machines' behaviour in its own words.

Samples built into a firmware by `tools/mkrom.py` are yours, or a library's. They are never in this repository:
`app/rom/` is left out of it.
