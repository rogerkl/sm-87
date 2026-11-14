// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Roger Klaveness
// SM-87's own part of the emulator's worklet (fm1-base tools/build.py --web: built as app_own_worklet.js, before the
// parts). A firmware package's samples, loaded on the page (app.js: Load .fwsc), come with the load message and go
// into the FM-1 before it boots, in place of the emulator's own (s700/s7mem.c sm87_bank_*); once it runs, the page is
// told whether they checked and what they hold.

let told = false;

globalThis.fm1App = {
  load(fm1, bank) {
    const ex = fm1.ex;
    if (!(bank instanceof ArrayBuffer) || !ex.sm87_bank_buf)
      return;
    const b = new Uint8Array(bank);
    if (!b.length || b.length > ex.sm87_bank_max())
      return;                             // (the page took no bigger: the firmware's slot)
    new Uint8Array(fm1.mem.buffer, ex.sm87_bank_buf(), b.length).set(b);
    ex.sm87_bank_set(b.length);
  },
  process(fm1) {
    const ex = fm1.ex;
    if (told || !ex.sm87_bank_taken)
      return;
    told = true;
    fm1.port.postMessage({ type: "app", sm87bank: { taken: ex.sm87_bank_taken(), count: ex.sm87_bank_count(),
                                                    kit: ex.sm87_bank_kit() } });
  },
};
